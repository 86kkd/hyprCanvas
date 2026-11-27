/**
 * @file core.cpp
 * @brief Canvas lifecycle, lane ownership, and Hyprland tiled-algorithm glue.
 *
 * This file owns the non-directional core of `CanvasLayout`: lane list
 * management, relayout of the whole canvas, Hyprland target callbacks, and
 * removal/creation flows that keep canvas state coherent.
 *
 * Reading guide for new contributors:
 * - `layout.h` explains the ownership hierarchy (`CanvasLayout` -> `Lane` -> `Stack`)
 * - this file answers "who owns what and when do we relayout?"
 * - `focus.cpp` answers "where should focus move next?"
 *
 * In practice, this file is the state-maintenance layer around the model:
 * - it owns the ordered lane list for one workspace
 * - it keeps cache structures in sync with that list
 * - it attaches/detaches workspace listeners
 * - it translates monitor/workspace changes into lane relayout calls
 */
#include <algorithm>
#include <cassert>
#include <memory>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/config/shared/complex/ComplexDataTypes.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/layout/algorithm/Algorithm.hpp>
#include <hyprland/src/layout/space/Space.hpp>
#include <hyprland/src/managers/fullscreen/handler/FullscreenHandler.hpp>
#include <spdlog/spdlog.h>

#include "core/core.h"
#include "core/layout_profile.h"
#include "core/monitor_geometry_runtime.h"
#include "core/window_key.h"
#include "layout/lane/lane.h"
#include "layout.h"
#include "internal.h"
#include "route.h"

using namespace CanvasCore;

namespace {

void resize_unmanaged_window(PHLWINDOW window, const Vector2D &delta) {
    if (!window)
        return;

    if (window->sizeAnimation()) {
        *window->sizeAnimation() = Vector2D(
            std::max((window->sizeAnimation()->goal() + delta).x, 20.0),
            std::max((window->sizeAnimation()->goal() + delta).y, 20.0));
    }
    window->presentation().updateDecorations();
}

} // namespace

CanvasLayout::CanvasLayout() {
    // Keep canvas-local active-lane state aligned with Hyprland's focused
    // window. `resetRuntimeState` can tear this listener down when a canvas is
    // detached from a workspace and later rebuild it on demand.
    m_focusCallback = Event::bus()->m_events.window.active.listen([this](PHLWINDOW window, Desktop::eFocusReason) {
        onWindowFocusChange(window);
    });
}

CanvasLayout::~CanvasLayout() {
    resetRuntimeState();
}

void CanvasLayout::resetRuntimeState() {
    // Drop every workspace-bound runtime seam. This is the "canvas instance is
    // no longer attached to a live workspace" cleanup path, so it must clear
    // both listeners and all cached ownership/handoff state.
    if (!restoringSnapshot)
        persistSnapshot();

    m_focusCallback = nullptr;
    m_workspaceActiveCallback = nullptr;
    workspaceRuntimeId = WORKSPACE_INVALID;
    lanes.clear();
    activeLane = nullptr;
    laneByWindow.clear();
    handoffState.reset();
    specialEphemeralLaneRestorePending = false;
    restoringSnapshot = false;
    snapshotRestoreAttempted = false;
}

void CanvasLayout::ensureWorkspaceRuntime() {
    // A `CanvasLayout` can outlive the concrete workspace it is currently bound
    // to. Rebuild listeners lazily so command paths can safely call this before
    // touching workspace-bound state.
    if (!m_focusCallback) {
        m_focusCallback = Event::bus()->m_events.window.active.listen([this](PHLWINDOW window, Desktop::eFocusReason) {
            onWindowFocusChange(window);
        });
    }

    const auto workspace = getLayoutWorkspace();
    if (!workspace) {
        m_workspaceActiveCallback = nullptr;
        workspaceRuntimeId = WORKSPACE_INVALID;
        return;
    }

    if (workspaceRuntimeId == CanvasCore::HyprlandRuntime::workspaceId(workspace) && m_workspaceActiveCallback)
        return;

    snapshotRestoreAttempted = false;
    workspaceRuntimeId = CanvasCore::HyprlandRuntime::workspaceId(workspace);
    m_workspaceActiveCallback = workspace->m_events.activeChanged.listen([this] {
        // Special workspaces can disappear without their hidden canvas ticking.
        // When the active state flips, resync hidden-canvas bookkeeping first,
        // then relayout if the visibility transition changed the lane state.
        const auto workspace = getLayoutWorkspace();
        if (!workspace)
            return;
        syncHiddenSpecialWorkspaceCanvases();
        const auto monitor = CanvasLayoutInternal::visible_monitor_for_workspace(workspace);
        if (syncSpecialWorkspaceVisibilityState(monitor) && monitor)
            relayoutCanvas(monitor, !CanvasCore::HyprlandRuntime::isSpecialWorkspace(workspace));
    });
}

CanvasLayoutInternal::CanvasBounds CanvasLayoutInternal::compute_canvas_bounds(PHLMONITOR monitor) {
    static auto PGAPSINDATA = CConfigValue<Config::IComplexConfigValue>("general:gaps_in");
    static auto PGAPSOUTDATA = CConfigValue<Config::IComplexConfigValue>("general:gaps_out");
    auto *const PGAPSIN = dynamic_cast<Config::CCssGapData *>(PGAPSINDATA.ptr());
    auto *const PGAPSOUT = dynamic_cast<Config::CCssGapData *>(PGAPSOUTDATA.ptr());

    // Canvas uses one shared interpretation of monitor space. `full` is the
    // logical monitor rectangle and `max` is the workarea after reserved areas
    // and outer gaps are applied. Every lane relayout path should go through
    // this helper so lane and stack code agree on the same coordinate space.
    const auto gaps_in = PGAPSIN ? PGAPSIN->m_top : 0;
    const auto gaps_out = PGAPSOUT ? PGAPSOUT->m_top : 0;

    return {
        .full = CanvasCore::logical_monitor_box(monitor),
        .max = CanvasCore::logical_workarea_box(monitor, gaps_out),
        .gap = static_cast<int>(gaps_in),
    };
}

PHLWORKSPACE CanvasLayout::getLayoutWorkspace() const {
    const auto algorithm = m_parent.lock();
    const auto space = algorithm ? algorithm->space() : nullptr;
    return space ? space->workspace() : nullptr;
}

Lane *CanvasLayout::getActiveLane() {
    // New readers often expect "no active lane" to remain null. In practice we
    // default to the first lane so command paths have a stable current lane as
    // soon as the canvas contains anything.
    if (activeLane)
        return activeLane->data().get();
    if (lanes.first()) {
        activeLane = lanes.first();
        return activeLane->data().get();
    }
    return nullptr;
}

CanvasLayout::LaneNode *CanvasLayout::getLaneNode(Lane *lane) const {
    if (!lane)
        return nullptr;

    for (auto node = lanes.first(); node != nullptr; node = node->next()) {
        if (node->data().get() == lane)
            return node;
    }

    return nullptr;
}

int CanvasLayout::laneIndexOf(Lane *lane) const {
    auto index = 0;
    for (auto node = lanes.first(); node != nullptr; node = node->next(), ++index) {
        if (node->data().get() == lane)
            return index;
    }

    return -1;
}

size_t CanvasLayout::laneCount() const {
    return lanes.size();
}

CanvasLayout::LaneNode *CanvasLayout::laneNodeAt(size_t index) const {
    auto *node = lanes.first();
    for (size_t i = 0; node != nullptr && i < index; ++i)
        node = node->next();
    return node;
}

CanvasLayoutInternal::TopologyIndex CanvasLayout::topologyIndex(Lane *lane) const {
    const auto laneIndex = lane ? laneIndexOf(lane) : -1;
    if (laneIndex < 0)
        return {};

    return {
        .mode = lane->get_mode(),
        .laneCount = laneCount(),
        .laneIndex = static_cast<size_t>(laneIndex),
        .stackCount = lane->stack_count(),
        .stackIndex = lane->active_stack_index(),
        .windowCount = lane->active_stack_window_count(),
        .windowIndex = lane->active_stack_window_index(),
    };
}

void CanvasLayout::setActiveLane(Lane *lane) {
    activeLane = getLaneNode(lane);
}

void CanvasLayout::rememberWindowLane(PHLWINDOW window, Lane *lane) {
    if (!window) {
        return;
    }

    if (!lane) {
        forgetWindowLane(window);
        return;
    }

    // The owner index is an optimization only. Every write path still has to
    // treat it as a cache and keep it synchronized with the authoritative lane
    // list.
    laneByWindow.remember(CanvasCore::window_key(window), lane);
}

void CanvasLayout::forgetWindowLane(PHLWINDOW window) {
    if (!window)
        return;

    laneByWindow.forget(CanvasCore::window_key(window));
}

void CanvasLayout::rememberLaneWindows(Lane *lane) {
    if (!lane)
        return;

    laneByWindow.remember_owner(lane, [&](auto &&remember) {
        lane->for_each_window([&](PHLWINDOW window) {
            remember(CanvasCore::window_key(window));
        });
    });
}

void CanvasLayout::forgetLaneWindows(Lane *lane) {
    if (!lane)
        return;

    laneByWindow.forget_owner(lane);
}

void CanvasLayout::debugVerifyLaneCache() const {
#ifndef NDEBUG
    assert(laneByWindow.matches_expected([&](auto &&addExpected) {
        for (auto laneNode = lanes.first(); laneNode != nullptr; laneNode = laneNode->next()) {
            auto *lane = laneNode->data().get();
            lane->for_each_window([&](PHLWINDOW window) {
                addExpected(CanvasCore::window_key(window), lane);
            });
        }
    }));
#endif
}

CanvasLayout::LaneNode *CanvasLayout::insertLaneNode(std::unique_ptr<Lane> lane, Direction direction, LaneNode *anchor) {
    if (!lane)
        return nullptr;

    const auto mode = lane->get_mode();
    lanes.push_back(std::move(lane));
    auto node = lanes.last();
    if (!anchor || anchor == node)
        return node;

    // Direction semantics are orientation-aware. "insert left" and "insert up"
    // both mean "before current lane" once the lane mode is resolved.
    if (CanvasLayoutInternal::direction_inserts_before_current(mode, direction))
        lanes.move_before(anchor, node);
    else
        lanes.move_after(anchor, node);

    return node;
}

Lane *CanvasLayout::ensureActiveLane(PHLMONITOR monitor, Mode mode) {
    if (auto *lane = getActiveLane())
        return lane;

    auto lane = std::make_unique<Lane>(monitor, mode);
    auto *lanePtr = lane.get();
    activeLane = insertLaneNode(std::move(lane), Direction::End);
    return lanePtr;
}

bool CanvasLayout::focusManagedWindow(PHLWINDOW window, bool warpCursor, const char *context, bool suppressWorkspaceSync) {
    if (!window)
        return false;

    if (suppressWorkspaceSync)
        handoffState.requestWorkspaceFocusSyncSuppression();

    if (CanvasLayoutInternal::switch_to_window(window, warpCursor))
        return true;

    if (suppressWorkspaceSync)
        (void)handoffState.consumeActiveLaneSyncPolicy();

    const auto *ctx = context ? context : "focusManagedWindow";
    spdlog::warn("{}: failed to focus managed window canvas_ws={} window={} target_workspace={} target_monitor={}",
                 ctx,
                 CanvasLayoutInternal::get_workspace_id(),
                 static_cast<const void*>(window.get()),
                 CanvasCore::HyprlandRuntime::windowWorkspaceId(window),
                 window->monitorID());
    syncActiveStateFromWorkspaceFocus();
    return false;
}

void CanvasLayout::finishLaneTransfer(LaneNode *sourceLaneNode, PHLMONITOR sourceMonitor, bool ephemeralOnly, bool warpCursor) {
    // Payload transfer helpers in focus/move-window paths call this as the last
    // phase: prune an empty source lane if needed, relayout the visible canvas,
    // then let Hyprland focus follow the lane's new active window.
    if (!dropEmptyLane(sourceLaneNode, activeLane ? activeLane->data().get() : nullptr, sourceMonitor, ephemeralOnly))
        relayoutVisibleCanvas(sourceMonitor);

    if (const auto lane = getActiveLane()) {
        if (const auto window = lane->get_active_window())
            focusManagedWindow(window, warpCursor, "finishLaneTransfer");
    }

    persistSnapshot();
}

Lane *CanvasLayout::getLaneForWindow(PHLWINDOW window) {
    if (!window)
        return nullptr;

    // This is the lane-level sibling of `Lane::getStackForWindow`: consult the
    // cache first, validate the cached owner, then fall back to a full scan.
    const auto key = CanvasCore::window_key(window);
    if (auto *cachedLane = laneByWindow.find_valid(key, [&](Lane *owner) {
            return owner && getLaneNode(owner) && owner->has_window(window);
        }))
        return cachedLane;

    for (auto lane = lanes.first(); lane != nullptr; lane = lane->next()) {
        if (lane->data()->has_window(window)) {
            rememberWindowLane(window, lane->data().get());
            return lane->data().get();
        }
    }
    return nullptr;
}

// Resolve the monitor currently displaying this canvas.
PHLMONITOR CanvasLayout::getVisibleCanvasMonitor(PHLMONITOR fallbackMonitor) const {
    const auto workspace = getLayoutWorkspace();
    if (!workspace)
        return fallbackMonitor;

    const auto visibleMonitor = CanvasLayoutInternal::visible_monitor_for_workspace(workspace);
    return visibleMonitor ? visibleMonitor : fallbackMonitor;
}

void CanvasLayout::prepareForActionContext() {
    // Dispatcher-facing entrypoints call this before mutating the canvas. It is
    // the "make sure hidden special workspaces and listeners are not stale"
    // checkpoint shared by command code.
    ensureWorkspaceRuntime();

    syncHiddenSpecialWorkspaceCanvases();
    const auto workspace = getLayoutWorkspace();
    const auto monitor = getVisibleCanvasMonitor();
    if (syncSpecialWorkspaceVisibilityState(monitor) && workspace && monitor)
        relayoutCanvas(monitor, !CanvasCore::HyprlandRuntime::isSpecialWorkspace(workspace));
}

// Relayout the canvas on the monitor currently showing it.
void CanvasLayout::relayoutVisibleCanvas(PHLMONITOR fallbackMonitor) {
    const auto workspace = getLayoutWorkspace();
    const auto monitor = getVisibleCanvasMonitor(fallbackMonitor);
    if (workspace && monitor)
        relayoutCanvas(monitor, !CanvasCore::HyprlandRuntime::isSpecialWorkspace(workspace));
}

// Remove an empty lane and repair active-lane state around the removal point.
bool CanvasLayout::dropEmptyLane(LaneNode *laneNode, Lane *preferredLane, PHLMONITOR fallbackMonitor, bool ephemeralOnly) {
    if (!laneNode || !laneNode->data())
        return false;

    auto *lane = laneNode->data().get();
    if (ephemeralOnly && !lane->is_ephemeral())
        return false;
    if (!lane->empty())
        return false;

    // Once a lane is removed we must still leave the canvas pointing at a valid
    // active lane. Prefer the explicit caller choice, otherwise pick an
    // adjacent lane when the removed lane used to be active.
    Lane *fallbackLane = preferredLane == lane ? nullptr : preferredLane;
    if (!fallbackLane && activeLane == laneNode) {
        if (laneNode->next())
            fallbackLane = laneNode->next()->data().get();
        else if (laneNode->prev())
            fallbackLane = laneNode->prev()->data().get();
    }

    forgetLaneWindows(lane);
    lanes.erase(laneNode);
    setActiveLane(fallbackLane);
    relayoutVisibleCanvas(fallbackMonitor);
    debugVerifyLaneCache();
    return true;
}

// Choose the lane that should become active after a lane was removed.
Lane *CanvasLayout::resolveActiveLaneAfterRemoval(LaneNode *laneNode, PHLWINDOW removedWindow) {
    const auto workspaceHandle = getLayoutWorkspace();
    if (workspaceHandle) {
        const auto focusedWindow = workspaceHandle->getLastFocusedWindow();
        if (focusedWindow && focusedWindow != removedWindow) {
            if (auto *preferredLane = getLaneForWindow(focusedWindow))
                return preferredLane;
        }
    }

    if (activeLane == laneNode) {
        if (laneNode->next())
            return laneNode->next()->data().get();
        if (laneNode->prev())
            return laneNode->prev()->data().get();
        return nullptr;
    }

    return activeLane ? activeLane->data().get() : nullptr;
}

// Recalculate every lane inside this canvas against one visible monitor.
void CanvasLayout::relayoutCanvas(PHLMONITOR monitor, bool honor_fullscreen, bool preserve_restored_geometry) {
    const auto workspace = getLayoutWorkspace();
    if (!workspace || !monitor || lanes.empty())
        return;

    // Default-handled fullscreen/maximized targets are owned by Hyprland's
    // handler. Normal model relayout would overwrite the handler's target box.
    if (honor_fullscreen && Fullscreen::controller()->hasFullscreen(workspace, true)) {
        m_defaultFullscreenHandler->syncTargetSizeAndPosition();
        return;
    }

    // Single-lane canvases delegate the whole monitor to one lane. Multi-lane
    // canvases instead treat lanes like pages inside a larger logical strip.
    if (lanes.size() == 1) {
        if (preserve_restored_geometry) {
            const auto bounds = CanvasLayoutInternal::compute_canvas_bounds(monitor);
            lanes.first()->data()->set_restored_canvas_geometry(bounds.full, bounds.max, bounds.gap);
            lanes.first()->data()->commit_restored_geometry();
            return;
        }
        CanvasLayoutInternal::recalculate_workspace_lane(lanes.first()->data().get(), monitor, workspace, honor_fullscreen);
        return;
    }

    const auto bounds = CanvasLayoutInternal::compute_canvas_bounds(monitor);
    const auto& full = bounds.full;
    const auto& max = bounds.max;

    const auto mode = getActiveLane() ? getActiveLane()->get_mode() : Mode::Row;
    // Lanes represent full-workarea pages on the canvas, offset relative to the
    // active lane so focus/move commands page between them predictably.
    const auto activeIndex = static_cast<size_t>(std::max(0, laneIndexOf(activeLane ? activeLane->data().get() : lanes.first()->data().get())));
    size_t index = 0;
    for (auto lane = lanes.first(); lane != nullptr; lane = lane->next(), ++index) {
        const auto delta = static_cast<double>(index) - static_cast<double>(activeIndex);
        const Box laneBox = CanvasCore::mode_pages_lanes_vertically(mode)
            ? Box(max.x, max.y + delta * full.h, max.w, max.h)
            : Box(max.x + delta * full.w, max.y, max.w, max.h);

        if (preserve_restored_geometry) {
            lane->data()->set_restored_canvas_geometry(full, laneBox, bounds.gap);
            lane->data()->commit_restored_geometry();
        } else {
            lane->data()->set_canvas_geometry(full, laneBox, bounds.gap);
            lane->data()->recalculate_lane_geometry();
        }
    }
}

// Hyprland callback: add a new tiled target into the current canvas.
// New reader map:
// Hyprland knows a window now belongs to the `canvas` layout, so it calls
// this target hook. From here canvas converts the compositor target into its
// own model by forwarding into `onWindowCreatedTiling`.
void CanvasLayout::newTarget(SP<Layout::ITarget> target) {
    ensureWorkspaceRuntime();

    if (!target)
        return;

    auto window = target->window();
    if (!window)
        return;

    if (handoffState.hasPendingManualCrossMonitorInsertion(CanvasCore::window_key(window))) {
        spdlog::debug("newTarget: deferring manual cross-monitor registration window={} workspace={}",
                      static_cast<const void*>(window.get()),
                      CanvasCore::HyprlandRuntime::windowWorkspaceId(window));
        return;
    }

    spdlog::info("newTarget: window={} workspace={}", static_cast<const void*>(window.get()), CanvasCore::HyprlandRuntime::windowWorkspaceId(window));
    if (onWindowCreatedTiling(window, Math::DIRECTION_DEFAULT)) {
        CanvasLayoutInternal::sync_new_window_pointer(window);
        // During newTarget(), CWindow::layoutTarget() is not guaranteed to be
        // bound yet, and CWindow::setBox() does not retain geometry this early.
        // Commit the authoritative model box through the callback target itself.
        if (auto* lane = getLaneForWindow(window)) {
            for (const auto& entry : lane->capture_window_boxes()) {
                if (entry.window != window)
                    continue;
                target->setPositionGlobal(Hyprutils::Math::CBox(
                    Vector2D(entry.box.x, entry.box.y),
                    Vector2D(entry.box.w, entry.box.h)));
                break;
            }
        }
    }
}

// Hyprland callback: target re-entered tiling flow and should be owned again.
void CanvasLayout::movedTarget(SP<Layout::ITarget> target, std::optional<Vector2D>)
{
    ensureWorkspaceRuntime();

    if (!target)
        return;

    auto window = target->window();
    if (!window)
        return;

    if (handoffState.hasPendingManualCrossMonitorInsertion(CanvasCore::window_key(window))) {
        spdlog::debug("movedTarget: deferring manual cross-monitor registration window={} workspace={}",
                      static_cast<const void*>(window.get()),
                      CanvasCore::HyprlandRuntime::windowWorkspaceId(window));
        return;
    }

    (void)onWindowCreatedTiling(window, Math::DIRECTION_DEFAULT);
}

// Hyprland callback: remove a tiled target from canvas ownership.
void CanvasLayout::removeTarget(SP<Layout::ITarget> target)
{
    ensureWorkspaceRuntime();

    if (!target)
        return;

    onWindowRemovedTiling(target->window());
}

// Hyprland callback: resize the active window inside the owning lane.
void CanvasLayout::resizeTarget(const Vector2D &delta, SP<Layout::ITarget> target, Layout::eRectCorner)
{
    auto window = windowFromTarget(target);
    if (!window)
        return;

    auto lane = getLaneForWindow(window);
    if (lane == nullptr) {
        resize_unmanaged_window(window, delta);
        return;
    }

    lane->focus_window(window);
    lane->resize_active_window(delta);
    persistSnapshot();
}

// Hyprland callback: relayout the whole canvas after monitor/workspace changes.
void CanvasLayout::recalculate(Layout::eRecalculateReason reason)
{
    ensureWorkspaceRuntime();

    const auto workspace = getLayoutWorkspace();
    if (!workspace)
        return;

    syncHiddenSpecialWorkspaceCanvases();
    const auto monitor = getVisibleCanvasMonitor();
    (void)syncSpecialWorkspaceVisibilityState(monitor);
    if (!monitor)
        return;

    const bool initializationRefresh = reason == Layout::RECALCULATE_REASON_UNKNOWN ||
                                       reason == Layout::RECALCULATE_REASON_RENDER_MONITOR;
    const bool preserveRestoredGeometry = restoredGeometryActive && initializationRefresh;
    spdlog::debug("recalculate: reason={} restored_geometry_active={} preserve_restored_geometry={}",
                  static_cast<int>(reason), restoredGeometryActive, preserveRestoredGeometry);
    if (!preserveRestoredGeometry)
        restoredGeometryActive = false;
    relayoutCanvas(monitor, true, preserveRestoredGeometry);
}

// Explicitly reject layout messages until the plugin defines a supported protocol.
Config::ErrorResult CanvasLayout::layoutMsg(const std::string_view& message)
{
    spdlog::warn("layoutMsg: unsupported message='{}'", message);
    return Config::configError("layout messages are not supported", Config::eConfigErrorLevel::ERROR, Config::eConfigErrorCode::INVALID_ARGUMENT);
}

// Predict the size of a new tiled target using the active lane if present.
std::optional<Vector2D> CanvasLayout::predictSizeForNewTarget()
{
    auto monitor = monitorForKeyboardAction();
    if (!monitor)
        return {};

    auto lane = getActiveLane();
    if (!lane) {
        const auto bounds = CanvasLayoutInternal::compute_canvas_bounds(monitor);
        return Vector2D(bounds.max.w, bounds.max.h);
    }

    return lane->predict_window_size();
}

// Return the next target candidate using the active window of the active lane.
SP<Layout::ITarget> CanvasLayout::getNextCandidate(SP<Layout::ITarget> /*old*/)
{
    auto lane = getActiveLane();
    if (!lane)
        return {};

    const auto active = lane->get_active_window();
    if (!active)
        return {};

    return active->layoutTarget();
}

// Swap two targets when they live inside the same lane/stack context.
void CanvasLayout::swapTargets(SP<Layout::ITarget> a, SP<Layout::ITarget> b)
{
    auto wa = windowFromTarget(a);
    auto wb = windowFromTarget(b);
    auto sa = getLaneForWindow(wa);
    auto sb = getLaneForWindow(wb);
    if (!wa || !wb || !sa || !sb || sa != sb)
        return;

    sa->swapWindows(wa, wb);
    persistSnapshot();
}

// Hyprland target-level move entrypoint reused by drag/move style operations.
void CanvasLayout::moveTargetInDirection(SP<Layout::ITarget> t, Math::eDirection direction, bool)
{
    auto window = windowFromTarget(t);
    auto s = getLaneForWindow(window);
    if (!s || !window)
        return;

    switch (direction) {
        case Math::DIRECTION_LEFT:
            onWindowFocusChange(window);
            moveWindow(CanvasCore::HyprlandRuntime::windowWorkspaceId(window), Direction::Left);
            break;
        case Math::DIRECTION_RIGHT:
            onWindowFocusChange(window);
            moveWindow(CanvasCore::HyprlandRuntime::windowWorkspaceId(window), Direction::Right);
            break;
        case Math::DIRECTION_UP:
            onWindowFocusChange(window);
            moveWindow(CanvasCore::HyprlandRuntime::windowWorkspaceId(window), Direction::Up);
            break;
        case Math::DIRECTION_DOWN:
            onWindowFocusChange(window);
            moveWindow(CanvasCore::HyprlandRuntime::windowWorkspaceId(window), Direction::Down);
            break;
        default:
            return;
    }
}

// Insert a newly mapped tiled window into the active lane, creating one if
// needed.
// This is the main "window joins the model" step:
// 1. reject duplicates if the window is already owned
// 2. find or create the active lane for this canvas
// 3. let the lane decide where the new window goes locally
// 4. update the window -> lane cache used by later focus/move operations
bool CanvasLayout::onWindowCreatedTiling(PHLWINDOW window, Math::eDirection)
{
    if (!window)
        return false;

    (void)maybeRestoreWorkspaceSnapshot();

    if (getLaneForWindow(window) != nullptr) {
        spdlog::debug("onWindowCreatedTiling: window already managed window={} workspace={}",
                      static_cast<const void*>(window.get()),
                      CanvasCore::HyprlandRuntime::windowWorkspaceId(window));
        if (restoredTargetsAwaitingCallback.erase(CanvasCore::window_key(window)) > 0) {
            if (auto* lane = getLaneForWindow(window))
                lane->commit_restored_geometry();
            return false;
        }
        recalculateWindow(window);
        return false;
    }

    restoredGeometryActive = false;

    auto lane = getActiveLane();
    if (lane == nullptr) {
        auto created = std::make_unique<Lane>(window);
        lane = created.get();
        activeLane = insertLaneNode(std::move(created), Direction::End);
    }
    lane->add_active_window(window);
    rememberWindowLane(window, lane);
    debugVerifyLaneCache();
    persistSnapshot();
    return true;
}

// Remove a tiled window and delete the lane if it becomes empty.
void CanvasLayout::onWindowRemovedTiling(PHLWINDOW window)
{
    const auto windowPtr = static_cast<const void*>(window.get());
    const auto workspace = window ? CanvasCore::HyprlandRuntime::windowWorkspaceId(window) : WORKSPACE_INVALID;
    restoredTargetsAwaitingCallback.erase(CanvasCore::window_key(window));
    restoredGeometryActive = false;
    spdlog::info("onWindowRemovedTiling: window={} workspace={}", windowPtr, workspace);

    CanvasCore::marksRegistry().remove(window);

    auto s = getLaneForWindow(window);
    if (s == nullptr) {
        spdlog::debug("onWindowRemovedTiling: no lane found for window={} workspace={}", windowPtr, workspace);
        return;
    }

    forgetWindowLane(window);
    if (s->remove_window(window)) {
        // Membership changes can also change lane-wide sizing (for example,
        // the last surviving window must fill its lane). Refresh and damage
        // the visible canvas here instead of relying on a later focus event.
        if (const auto monitor = getVisibleCanvasMonitor())
            recalculateMonitor(monitor->m_id);
        debugVerifyLaneCache();
        persistSnapshot();
        return;
    }

    auto lane = getLaneNode(s);
    if (!lane) {
        spdlog::warn("onWindowRemovedTiling: empty lane missing from list lane={} workspace={}",
                     static_cast<const void*>(s), workspace);
        return;
    }

    auto *nextActiveLane = resolveActiveLaneAfterRemoval(lane, window);

    auto *doomed = lane->data().get();
    spdlog::info("onWindowRemovedTiling: deleting empty lane={} workspace={}",
                 static_cast<const void*>(doomed), workspace);
    forgetLaneWindows(doomed);
    lanes.erase(lane);

    setActiveLane(nextActiveLane);
    relayoutVisibleCanvas();
    debugVerifyLaneCache();
    persistSnapshot();
}

// Return whether this canvas currently manages a given window.
bool CanvasLayout::isWindowTiled(PHLWINDOW window)
{
    return getLaneForWindow(window) != nullptr;
}

// Recalculate only the lane that owns a given window.
void CanvasLayout::recalculateWindow(PHLWINDOW window)
{
    auto lane = getLaneForWindow(window);
    if (lane == nullptr)
        return;

    lane->recalculate_lane_geometry();
}
