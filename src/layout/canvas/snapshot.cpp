/**
 * @file layout/canvas/snapshot.cpp
 * @brief Canvas snapshot capture, persistence, and restore flows.
 *
 * Keeps session-persistence concerns isolated from the live Hyprland callback
 * glue in `core.cpp`. All lane/stack/window reconstruction uses unique
 * ownership so partial restores can never leak model objects.
 */
#include <algorithm>
#include <memory>
#include <unordered_map>
#include <vector>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <spdlog/spdlog.h>

#include "core/hyprland_runtime.h"
#include "core/layout_snapshot.h"
#include "core/monitor_geometry_runtime.h"
#include "core/window_key.h"
#include "layout/canvas/internal.h"
#include "layout/canvas/layout.h"
#include "layout/canvas/layout_repository.h"
#include "layout/lane/lane.h"

namespace {

Mode restored_mode_or_default(int value, Mode fallback) {
    switch (static_cast<Mode>(value)) {
    case Mode::Row:
    case Mode::Column:
        return static_cast<Mode>(value);
    default:
        return fallback;
    }
}

CanvasModel::WindowHeight restored_height_or_default(int value) {
    switch (static_cast<CanvasModel::WindowHeight>(value)) {
    case CanvasModel::WindowHeight::OneThird:
    case CanvasModel::WindowHeight::OneHalf:
    case CanvasModel::WindowHeight::TwoThirds:
    case CanvasModel::WindowHeight::One:
    case CanvasModel::WindowHeight::Free:
    case CanvasModel::WindowHeight::Auto:
        return static_cast<CanvasModel::WindowHeight>(value);
    default:
        return CanvasModel::WindowHeight::One;
    }
}

std::vector<PHLWINDOW> live_tiled_workspace_windows(PHLWORKSPACE workspace) {
    std::vector<PHLWINDOW> windows;
    if (!workspace)
        return windows;

    windows.reserve(CanvasCore::HyprlandRuntime::windows().size());
    for (const auto &window : CanvasCore::HyprlandRuntime::windows()) {
        if (!window ||
            CanvasCore::HyprlandRuntime::windowWorkspaceId(window) != CanvasCore::HyprlandRuntime::workspaceId(workspace) ||
            window->isFloating() || !window->mapped() || window->isHidden())
            continue;
        windows.push_back(window);
    }
    return windows;
}

} // namespace

std::optional<CanvasLayoutSnapshot::CanvasSnapshot> CanvasLayout::captureSnapshot() const {
    const auto workspace = getLayoutWorkspace();
    if (!workspace)
        return std::nullopt;

    CanvasLayoutSnapshot::CanvasSnapshot snapshot;
    snapshot.workspaceId = CanvasCore::HyprlandRuntime::workspaceId(workspace);

    size_t storedLaneIndex = 0;
    bool foundActiveLane = false;
    for (auto laneNode = lanes.first(); laneNode != nullptr; laneNode = laneNode->next()) {
        auto *lane = laneNode->data().get();
        if (!lane || lane->empty())
            continue;

        if (laneNode == activeLane) {
            snapshot.activeLaneIndex = storedLaneIndex;
            foundActiveLane = true;
        }

        snapshot.lanes.push_back(lane->capture_snapshot());
        ++storedLaneIndex;
    }

    if (snapshot.lanes.empty())
        return std::nullopt;

    if (!foundActiveLane)
        snapshot.activeLaneIndex = 0;
    return snapshot;
}

void CanvasLayout::persistSnapshot() {
    if (restoringSnapshot)
        return;

    const auto workspace = getLayoutWorkspace();
    if (!workspace)
        return;

    const auto liveWindows = live_tiled_workspace_windows(workspace);
    if (!liveWindows.empty()) {
        const auto fullyManaged = std::all_of(liveWindows.begin(), liveWindows.end(), [this](const auto &window) {
            return getLaneForWindow(window) != nullptr;
        });
        if (!fullyManaged) {
            spdlog::debug("persistSnapshot: preserving last complete snapshot during partial detach workspace={} live_windows={}",
                          CanvasCore::HyprlandRuntime::workspaceId(workspace),
                          liveWindows.size());
            return;
        }
    }

    if (const auto snapshot = captureSnapshot()) {
        CanvasLayoutState::repository().upsert(*snapshot);
        return;
    }

    clearPersistedSnapshot();
}

void CanvasLayout::persistCurrentSnapshot() {
    persistSnapshot();
}

void CanvasLayout::clearPersistedSnapshot() {
    const auto workspace = getLayoutWorkspace();
    if (!workspace)
        return;

    CanvasLayoutState::repository().erase(CanvasCore::HyprlandRuntime::workspaceId(workspace));
}

bool CanvasLayout::restoreSnapshot(const CanvasLayoutSnapshot::CanvasSnapshot &snapshot) {
    const auto workspace = getLayoutWorkspace();
    if (!workspace || snapshot.workspaceId != CanvasCore::HyprlandRuntime::workspaceId(workspace))
        return false;

    const auto liveWindows = live_tiled_workspace_windows(workspace);
    if (liveWindows.empty())
        return false;

    std::unordered_map<uintptr_t, PHLWINDOW> windowsByKey;
    windowsByKey.reserve(liveWindows.size());
    for (const auto &window : liveWindows)
        windowsByKey.emplace(CanvasCore::snapshot_window_key(
                                 window,
                                 snapshot.version,
                                 CanvasLayoutSnapshot::kStableWindowKeyFormatVersion),
                             window);

    const auto visibleMonitor = getVisibleCanvasMonitor();
    const auto fallbackMonitor = visibleMonitor ? visibleMonitor : CanvasCore::HyprlandRuntime::monitorById(liveWindows.front()->monitorID());
    if (!fallbackMonitor)
        return false;

    restoredTargetsAwaitingCallback.clear();
    for (const auto &window : liveWindows)
        restoredTargetsAwaitingCallback.insert(CanvasCore::window_key(window));
    restoredGeometryActive = true;

    const auto bounds = CanvasLayoutInternal::compute_canvas_bounds(fallbackMonitor);
    restoringSnapshot = true;

    lanes.clear();
    activeLane = nullptr;
    laneByWindow.clear();

    Lane *restoredActiveLane = nullptr;
    for (size_t laneIndex = 0; laneIndex < snapshot.lanes.size(); ++laneIndex) {
        const auto &laneSnapshot = snapshot.lanes[laneIndex];
        auto lane = std::make_unique<Lane>(
            fallbackMonitor,
            restored_mode_or_default(laneSnapshot.mode, CanvasCore::default_mode_for_monitor(fallbackMonitor)));
        lane->set_ephemeral(laneSnapshot.ephemeral);

        for (size_t stackIndex = 0; stackIndex < laneSnapshot.stacks.size(); ++stackIndex) {
            const auto &stackSnapshot = laneSnapshot.stacks[stackIndex];
            auto stack = std::make_unique<CanvasModel::Stack>(bounds.max.w, bounds.max.h, lane->get_mode());

            for (const auto &windowSnapshot : stackSnapshot.windows) {
                const auto it = windowsByKey.find(windowSnapshot.key);
                if (it == windowsByKey.end())
                    continue;

                auto window = std::make_unique<CanvasModel::Window>(it->second, windowSnapshot.geomH, lane->get_mode());
                window->restore_state(restored_height_or_default(windowSnapshot.heightMode),
                                      windowSnapshot.geomY,
                                      windowSnapshot.geomH,
                                      windowSnapshot.memY,
                                      windowSnapshot.memH);
                stack->append_restored_window(std::move(window));
                windowsByKey.erase(it);
            }

            if (stack->size() == 0)
                continue;

            stack->restore_state(CanvasModel::StackWidth::from_snapshot(stackSnapshot.width),
                                 stackSnapshot.geom,
                                 stackSnapshot.memGeom,
                                 stackSnapshot.fullscreened,
                                 stackSnapshot.maximized);
            if (stackSnapshot.activeWindowKey != 0) {
                const auto activeWindowKey = stackSnapshot.activeWindowKey;
                stack->for_each_window([&](PHLWINDOW window) {
                    if (CanvasCore::snapshot_window_key(
                            window,
                            snapshot.version,
                            CanvasLayoutSnapshot::kStableWindowKeyFormatVersion) == activeWindowKey)
                        stack->focus_window(window);
                });
            }
            lane->append_restored_stack(std::move(stack));
        }

        if (lane->empty())
            continue;

        auto *lanePtr = lane.get();
        lanePtr->set_active_stack_by_index(laneSnapshot.activeStackIndex);
        auto *laneNode = insertLaneNode(std::move(lane), Direction::End);
        rememberLaneWindows(lanePtr);
        if (laneIndex == snapshot.activeLaneIndex)
            restoredActiveLane = lanePtr;
        if (!activeLane)
            activeLane = laneNode;
    }

    if (restoredActiveLane)
        setActiveLane(restoredActiveLane);
    else
        (void)getActiveLane();

    for (const auto &[_, extraWindow] : windowsByKey) {
        auto *lane = getActiveLane();
        if (!lane) {
            auto created = std::make_unique<Lane>(extraWindow);
            lane = created.get();
            activeLane = insertLaneNode(std::move(created), Direction::End);
        }
        lane->add_active_window(extraWindow);
        rememberWindowLane(extraWindow, lane);
    }

    const auto restoredActiveWindow = activeLane && activeLane->data() ? activeLane->data()->get_active_window() : nullptr;
    relayoutCanvas(fallbackMonitor, !CanvasCore::HyprlandRuntime::isSpecialWorkspace(workspace), true);
    if (restoredActiveWindow)
        focusManagedWindow(restoredActiveWindow, false, "restoreSnapshot");
    debugVerifyLaneCache();

    restoringSnapshot = false;
    persistSnapshot();
    spdlog::info("restore_snapshot: workspace={} restored_lanes={} live_windows={}",
                 CanvasCore::HyprlandRuntime::workspaceId(workspace),
                 snapshot.lanes.size(),
                 liveWindows.size());
    return true;
}

bool CanvasLayout::maybeRestoreWorkspaceSnapshot() {
    if (snapshotRestoreAttempted)
        return false;

    snapshotRestoreAttempted = true;
    const auto workspace = getLayoutWorkspace();
    if (!workspace || !lanes.empty())
        return false;

    CanvasLayoutState::repository().initialize();
    const auto snapshot = CanvasLayoutState::repository().find(CanvasCore::HyprlandRuntime::workspaceId(workspace));
    if (!snapshot)
        return false;

    return restoreSnapshot(*snapshot);
}
