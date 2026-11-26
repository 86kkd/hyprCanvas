/**
 * @file commands.cpp
 * @brief Dispatcher-facing command wrappers for canvas operations.
 *
 * These methods are intentionally thin. Their job is to normalize workspace
 * focus state, resolve the active lane, and then delegate the actual layout
 * work to `Lane` or shared canvas helpers.
 */
#include <memory>
#include <string>
#include <utility>

#include <hyprland/src/Compositor.hpp>
#include <spdlog/spdlog.h>

#include "core/direction.h"
#include "core/layout_profile.h"
#include "core/monitor_geometry_runtime.h"
#include "core/window_key.h"
#include "core/workspace_selector.h"
#include "layout/lane/lane.h"
#include "layout.h"
#include "internal.h"
#include "route.h"

bool CanvasLayout::handoffMoveWindowAcrossMonitor(int workspace, Direction direction, Lane *sourceLane,
                                                  PHLWINDOW currentWindow, PHLMONITOR sourceMonitor,
                                                  PHLMONITOR targetMonitor) {
    if (!sourceLane || !currentWindow || !sourceMonitor || !targetMonitor)
        return false;
    auto *sourceLaneNode = getLaneNode(sourceLane);
    if (!sourceLaneNode)
        return false;

    const auto workspaceId = CanvasLayoutInternal::preferred_workspace_id(targetMonitor, workspace);
    const auto targetWorkspace = ScrollerCore::HyprlandRuntime::workspaceById(workspaceId);
    const auto selector = ScrollerCore::workspace_selector(targetWorkspace);
    if (!CanvasLayoutInternal::can_invoke_dispatcher("movetoworkspacesilent", selector, "move_window_cross_monitor"))
        return false;

    auto *targetLayout = CanvasLayoutInternal::get_canvas_for_workspace(workspaceId);
    if (!targetLayout)
        return false;

    // Phase 1: resolve the destination anchor before we detach ownership from
    // the source lane, so the insertion point still reflects the target
    // canvas's current focus/geometry state.
    auto *targetLane = static_cast<Lane *>(nullptr);
    const auto targetAnchorWindow = resolveCrossMonitorFocusTarget(
        targetLayout,
        targetMonitor,
        workspaceId,
        direction,
        currentWindow,
        &targetLane,
        nullptr);

    const auto restorePlan = sourceLane->capture_active_window_restore_plan(direction);
    auto payload = sourceLane->extract_active_window_payload();
    if (!payload)
        return true;

    const auto insertDirection = ScrollerCore::opposite_direction(direction);
    // Phase 2: tell target callbacks that the backend workspace move is part of
    // an explicit scroller handoff. That keeps target hooks from auto-claiming
    // the window before we finish reinserting the transferred payload.
    // Tell target callbacks that this window is being rehomed manually. The
    // Hyprland dispatcher still moves the backend window across workspaces, but
    // scroller keeps ownership transfer explicit so it can preserve lane/stack
    // geometry and roll back cleanly on failure.
    const auto currentWindowKey = ScrollerCore::window_key(currentWindow);
    targetLayout->handoffState.rememberManualCrossMonitorInsertion(currentWindowKey);

    if (!CanvasLayoutInternal::invoke_dispatcher("movetoworkspacesilent", selector, "move_window_cross_monitor")) {
        spdlog::warn("move_window_cross_monitor: dispatcher failed workspace={} direction={} window={}",
                     workspace,
                     ScrollerCore::direction_name(direction),
                     static_cast<const void*>(currentWindow.get()));
        targetLayout->handoffState.forgetManualCrossMonitorInsertion(currentWindowKey);
        sourceLane->restore_active_window_payload(std::move(payload), restorePlan);
        debugVerifyLaneCache();
        return false;
    }

    // Phase 3: re-resolve the destination lane after Hyprland has moved the
    // backend window, then consume the payload into the target lane and update
    // both caches before any relayout runs.
    targetLane = targetAnchorWindow ? targetLayout->getLaneForWindow(targetAnchorWindow) : nullptr;
    if (!targetLane)
        targetLane = targetLayout->getActiveLane();
    if (!targetLane) {
        const auto targetMode = ScrollerCore::default_mode_for_monitor(targetMonitor);
        targetLane = targetLayout->ensureActiveLane(targetMonitor, targetMode);
    }

    if (targetAnchorWindow && targetLane->has_window(targetAnchorWindow) && !targetLane->is_active(targetAnchorWindow))
        targetLane->focus_window(targetAnchorWindow);
    targetLane->insert_window_payload(std::move(payload), insertDirection);
    forgetWindowLane(currentWindow);
    targetLayout->rememberWindowLane(currentWindow, targetLane);
    targetLayout->handoffState.forgetManualCrossMonitorInsertion(currentWindowKey);
    targetLayout->setActiveLane(targetLane);

    // Phase 4: prune the emptied source lane, relayout both canvases, and end
    // with focus on the transferred compositor window.
    if (!dropEmptyLane(sourceLaneNode, nullptr, sourceMonitor))
        relayoutVisibleCanvas(sourceMonitor);

    targetLayout->relayoutVisibleCanvas(targetMonitor);
    persistSnapshot();
    targetLayout->persistCurrentSnapshot();
    targetLayout->focusManagedWindow(currentWindow, true, "move_window_cross_monitor", true);
    debugVerifyLaneCache();
    targetLayout->debugVerifyLaneCache();
    return true;
}

void CanvasLayout::transferMoveWindowToAdjacentLane(Lane *sourceLane, PHLWINDOW currentWindow,
                                                    LaneNode *targetLaneNode, Direction direction,
                                                    PHLMONITOR sourceMonitor) {
    if (!sourceLane || !currentWindow || !targetLaneNode)
        return;
    auto *sourceLaneNode = getLaneNode(sourceLane);
    if (!sourceLaneNode)
        return;

    auto payload = sourceLane->extract_active_window_payload();
    if (!payload)
        return;

    targetLaneNode->data()->insert_window_payload(std::move(payload), direction);
    rememberWindowLane(currentWindow, targetLaneNode->data().get());
    activeLane = targetLaneNode;
    finishLaneTransfer(sourceLaneNode, sourceMonitor, false, true);
    debugVerifyLaneCache();
}

void CanvasLayout::transferMoveWindowToNewLane(Lane *sourceLane, PHLWINDOW currentWindow,
                                               PHLMONITOR sourceMonitor, Mode mode, Direction direction,
                                               LaneNode *anchor) {
    if (!sourceLane || !currentWindow)
        return;
    auto sourceLaneNode = getLaneNode(sourceLane);
    if (!sourceLaneNode)
        return;

    auto payload = sourceLane->extract_active_window_payload();
    if (!payload)
        return;

    auto newLane = std::make_unique<Lane>(sourceMonitor, mode);
    auto *newLanePtr = newLane.get();
    auto newLaneNode = insertLaneNode(std::move(newLane), direction, anchor ? anchor : sourceLaneNode);

    newLanePtr->insert_window_payload(std::move(payload), direction);
    rememberWindowLane(currentWindow, newLanePtr);
    activeLane = newLaneNode;
    finishLaneTransfer(sourceLaneNode, sourceMonitor, false, true);
    debugVerifyLaneCache();
}

void CanvasLayout::applyLocalWindowMove(Lane *lane, CanvasLayoutInternal::LocalMove move, Direction direction) {
    if (!lane)
        return;

    const char *context = nullptr;
    switch (move) {
    case CanvasLayoutInternal::LocalMove::ReorderWindow:
        lane->move_active_stack(direction);
        context = "move_window_within_stack";
        break;
    case CanvasLayoutInternal::LocalMove::ReorderStack:
        lane->move_active_stack(direction);
        context = "move_window_stack";
        break;
    case CanvasLayoutInternal::LocalMove::JoinStack:
        lane->move_active_window_to_adjacent_stack(direction);
        context = "move_window_adjacent_stack";
        break;
    case CanvasLayoutInternal::LocalMove::SplitStack:
        lane->move_active_window_to_new_stack(direction);
        context = "move_window_split_stack";
        break;
    case CanvasLayoutInternal::LocalMove::Focus:
        return;
    }
    focusManagedWindow(lane->get_active_window(), false, context);
}

// Make `visibleStacks` the session-wide stack count: every stack on every
// canvas spans `workarea / visibleStacks`, and new stacks follow it too.
void CanvasLayout::setWindowSize(int workspace, int visibleStacks)
{
    (void)workspace;
    const auto width = ScrollerModel::StackWidth::visible(visibleStacks);
    ScrollerModel::visible_stacks_override() = width.visible_stacks();

    syncActiveStateFromWorkspaceFocus();
    for (const auto &workspaceRef : ScrollerCore::HyprlandRuntime::workspaces()) {
        const auto member = workspaceRef.lock();
        auto *layout = member ? CanvasLayoutInternal::get_canvas_for_workspace(ScrollerCore::HyprlandRuntime::workspaceId(member)) : nullptr;
        if (layout)
            layout->applyStackWidth(width);
    }
}

void CanvasLayout::applyStackWidth(ScrollerModel::StackWidth width)
{
    for (auto lane = lanes.first(); lane != nullptr; lane = lane->next())
        lane->data()->apply_stack_width(width);
    relayoutVisibleCanvas();
    persistSnapshot();
}

// Move the focused window or stack according to lane/mode routing rules.
// Reading guide:
// `dispatch_movewindow` resolves the active `CanvasLayout` and lands here.
// The destination comes from the same topology neighbor query `moveFocus`
// uses; this function then runs the payload transfer for that answer.
void CanvasLayout::moveWindow(int workspace, Direction direction) {
    withActiveLane(ActiveLaneSyncPolicy::WorkspaceFocus, [&](Lane *lane) {
        const auto mode = lane->get_mode();
        const auto currentWindow = lane->get_active_window();
        const auto sourceMonitor = currentWindow ? ScrollerCore::HyprlandRuntime::monitorById(currentWindow->monitorID()) : getVisibleCanvasMonitor();
        if (!currentWindow || !sourceMonitor) {
            if (CanvasLayoutInternal::direction_moves_between_lanes(mode, direction))
                CanvasLayoutInternal::dispatch_directional_builtin("movewindow", direction);
            return;
        }

        const auto targetMonitor = CanvasLayoutInternal::resolve_monitor_in_direction(sourceMonitor, direction);
        const auto next = CanvasLayoutInternal::neighbor(topologyIndex(lane), {
            .intent = CanvasLayoutInternal::NeighborIntent::MoveWindow,
            .direction = direction,
            .monitorInDirection = targetMonitor != nullptr,
        });

        switch (next.kind) {
        case CanvasLayoutInternal::NeighborKind::NoOp:
            return;
        case CanvasLayoutInternal::NeighborKind::LocalTarget:
            applyLocalWindowMove(lane, next.localMove, direction);
            return;
        case CanvasLayoutInternal::NeighborKind::AdjacentLane:
            transferMoveWindowToAdjacentLane(lane, currentWindow, laneNodeAt(next.laneIndex), direction, sourceMonitor);
            return;
        case CanvasLayoutInternal::NeighborKind::CreateEphemeral:
            transferMoveWindowToNewLane(lane, currentWindow, sourceMonitor, mode, direction, laneNodeAt(next.laneIndex));
            return;
        case CanvasLayoutInternal::NeighborKind::CrossMonitor:
            if (!handoffMoveWindowAcrossMonitor(workspace, direction, lane, currentWindow, sourceMonitor, targetMonitor))
                CanvasLayoutInternal::dispatch_directional_builtin("movewindow", direction);
            return;
        }
    });
    persistSnapshot();
}

// Bypass local topology so a monitor shortcut always transfers directly to the
// neighboring screen. Reuse the handoff transaction to preserve stack ownership
// and focus; a missing destination must leave the current layout untouched.
void CanvasLayout::moveWindowToMonitor(int workspace, Direction direction) {
    withActiveLane(ActiveLaneSyncPolicy::WorkspaceFocus, [&](Lane *lane) {
        const auto window = lane->get_active_window();
        if (!window)
            return;
        const auto sourceMonitor = ScrollerCore::HyprlandRuntime::monitorById(window->monitorID());
        const auto targetMonitor = CanvasLayoutInternal::resolve_monitor_in_direction(sourceMonitor, direction);
        if (!targetMonitor || targetMonitor == sourceMonitor)
            return;
        handoffMoveWindowAcrossMonitor(workspace, direction, lane, window, sourceMonitor, targetMonitor);
    });
}

void CanvasLayout::swapWindow(int workspace, Direction direction) {
    (void)workspace;
    bool swapped = false;
    withActiveLane(ActiveLaneSyncPolicy::WorkspaceFocus, [&](Lane *lane) {
        const auto next = CanvasLayoutInternal::swap_window_neighbor(topologyIndex(lane), direction);
        if (next.kind != CanvasLayoutInternal::NeighborKind::LocalTarget)
            return;
        const auto window = lane->get_active_window();
        if (!lane->swap_active_window_with_stack(next.stackIndex))
            return;
        swapped = true;
        focusManagedWindow(window, true, "swap_window", true);
        debugVerifyLaneCache();
    });
    if (swapped)
        persistSnapshot();
}

// Change the active lane traversal mode.
void CanvasLayout::setMode(int workspace, Mode mode) {
    (void)workspace;
    withActiveLane(ActiveLaneSyncPolicy::WorkspaceFocus, [mode](Lane *lane) {
        lane->set_mode(mode);
    });
    relayoutVisibleCanvas();
    persistSnapshot();
}

// Toggle scroller-managed fullscreen/expanded behavior.
void CanvasLayout::toggleFullscreen(int workspace) {
    (void)workspace;
    const auto syncPolicy = handoffState.consumeActiveLaneSyncPolicy();
    withActiveLane(syncPolicy, [](Lane *lane) {
        lane->toggle_fullscreen_active_window();
    });
    persistSnapshot();
}
