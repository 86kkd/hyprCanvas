/**
 * @file commands.cpp
 * @brief Dispatcher-facing command wrappers for canvas operations.
 *
 * These methods are intentionally thin. Their job is to normalize workspace
 * focus state, resolve the active lane, and then delegate the actual layout
 * work to `Lane` or shared canvas helpers.
 */
#include <string>
#include <utility>

#include <hyprland/src/Compositor.hpp>
#include <spdlog/spdlog.h>

#include "../../core/direction.h"
#include "../../core/layout_profile.h"
#include "../../core/monitor_geometry_runtime.h"
#include "../../core/workspace_selector.h"
#include "../lane/lane.h"
#include "layout.h"
#include "internal.h"
#include "route.h"

PHLMONITOR CanvasLayout::directionalMoveTargetMonitor(PHLMONITOR sourceMonitor, Direction direction) const {
    return CanvasLayoutInternal::resolve_monitor_in_direction(sourceMonitor, direction);
}

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
    targetLayout->rememberManualCrossMonitorInsertion(currentWindow);

    if (!CanvasLayoutInternal::invoke_dispatcher("movetoworkspacesilent", selector, "move_window_cross_monitor")) {
        spdlog::warn("move_window_cross_monitor: dispatcher failed workspace={} direction={} window={}",
                     workspace,
                     ScrollerCore::direction_name(direction),
                     static_cast<const void*>(currentWindow.get()));
        targetLayout->forgetManualCrossMonitorInsertion(currentWindow);
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
    targetLayout->forgetManualCrossMonitorInsertion(currentWindow);
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
                                                    ListNode<Lane *> *targetLaneNode, Direction direction,
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
    rememberWindowLane(currentWindow, targetLaneNode->data());
    activeLane = targetLaneNode;
    finishLaneTransfer(sourceLaneNode, sourceMonitor, false, true);
    debugVerifyLaneCache();
}

void CanvasLayout::transferMoveWindowToNewLane(Lane *sourceLane, PHLWINDOW currentWindow,
                                               PHLMONITOR sourceMonitor, Mode mode, Direction direction) {
    if (!sourceLane || !currentWindow)
        return;
    auto sourceLaneNode = getLaneNode(sourceLane);
    if (!sourceLaneNode)
        return;

    auto payload = sourceLane->extract_active_window_payload();
    if (!payload)
        return;

    auto *newLane = new Lane(sourceMonitor, mode);
    auto newLaneNode = insertLaneNode(newLane, direction, sourceLaneNode);

    newLane->insert_window_payload(std::move(payload), direction);
    rememberWindowLane(currentWindow, newLane);
    activeLane = newLaneNode;
    finishLaneTransfer(sourceLaneNode, sourceMonitor, false, true);
    debugVerifyLaneCache();
}

void CanvasLayout::handleMoveWindowWithinLane(int workspace, Direction direction, Lane *lane,
                                              PHLWINDOW currentWindow, PHLMONITOR sourceMonitor) {
    if (!lane)
        return;

    const auto mode = lane->get_mode();
    const auto backward = ScrollerCore::local_item_backward_direction(mode);
    const auto forward = ScrollerCore::local_item_forward_direction(mode);
    const bool movesBetweenStacks = direction == backward || direction == forward;
    if (!movesBetweenStacks) {
        lane->move_active_stack(direction);
        focusManagedWindow(lane->get_active_window(), false, "move_window_stack");
        return;
    }

    const auto targetMonitor = directionalMoveTargetMonitor(sourceMonitor, direction);
    const bool sourceStackHasMultipleWindows = lane->active_stack_has_multiple_windows();
    if (sourceStackHasMultipleWindows) {
        // Splitting a multi-window stack stays local even at the lane edge.
        // Cross-monitor handoff is reserved for single-window edge stacks.
        lane->move_active_window_to_new_stack(direction);
        focusManagedWindow(lane->get_active_window(), false, "move_window_split_stack");
        return;
    }

    if (!lane->active_item_at_edge(direction)) {
        lane->move_active_window_to_adjacent_stack(direction);
        focusManagedWindow(lane->get_active_window(), false, "move_window_adjacent_stack");
        return;
    }

    if (targetMonitor) {
        if (!handoffMoveWindowAcrossMonitor(workspace, direction, lane, currentWindow, sourceMonitor, targetMonitor))
            CanvasLayoutInternal::dispatch_directional_builtin("movewindow", direction);
        return;
    }
}

void CanvasLayout::handleMoveWindowAcrossLanes(int workspace, Direction direction, Lane *lane,
                                               PHLWINDOW currentWindow, PHLMONITOR sourceMonitor,
                                               Mode mode) {
    if (!lane)
        return;

    const auto handoffPlan = currentWindow && sourceMonitor
        ? CanvasLayoutInternal::plan_directional_handoff(
              lanes, activeLane, sourceMonitor, mode, direction, true, CanvasLayoutInternal::resolve_monitor_in_direction)
        : CanvasLayoutInternal::DirectionalHandoffPlan{};

    switch (CanvasLayoutInternal::decide_cross_lane_move_window_action(currentWindow != nullptr, sourceMonitor != nullptr, handoffPlan.route)) {
    case CanvasLayoutInternal::CrossLaneMoveWindowAction::BuiltinFallback:
        CanvasLayoutInternal::dispatch_directional_builtin("movewindow", direction);
        return;
    case CanvasLayoutInternal::CrossLaneMoveWindowAction::AdjacentLaneTransfer:
        transferMoveWindowToAdjacentLane(lane, currentWindow, handoffPlan.targetLaneNode, direction, sourceMonitor);
        return;
    case CanvasLayoutInternal::CrossLaneMoveWindowAction::CrossMonitorTransfer:
        if (!handoffMoveWindowAcrossMonitor(workspace, direction, lane, currentWindow, sourceMonitor, handoffPlan.targetMonitor))
            CanvasLayoutInternal::dispatch_directional_builtin("movewindow", direction);
        return;
    case CanvasLayoutInternal::CrossLaneMoveWindowAction::CreateLaneTransfer:
        transferMoveWindowToNewLane(lane, currentWindow, sourceMonitor, mode, direction);
        return;
    case CanvasLayoutInternal::CrossLaneMoveWindowAction::NoOp:
        return;
    }
}

// Cycle the active stack width or active window height by one preset step.
//
// TODO(set_window_size): Replace this with an explicit visible-stack count
// setter (n stacks on screen => primary span = workarea / n). Remove the
// step-based cycle once `set_window_size` is wired through dispatch/lane.
void CanvasLayout::cycle_window_size(int workspace, int step)
{
    (void)workspace;
    withActiveLane(ActiveLaneSyncPolicy::WorkspaceFocus, [step](Lane *lane) {
        lane->resize_active_stack(step);
    });
    persistSnapshot();
}

// Move the focused window or stack according to lane/mode routing rules.
// Reading guide:
// `dispatch_movewindow` resolves the active `CanvasLayout` and lands here.
// This function only decides which scope should handle the move:
// same lane, adjacent lane, or another monitor. The actual payload transfer
// work then happens in the lane/canvas helpers chosen by that route.
void CanvasLayout::move_window(int workspace, Direction direction) {
    withActiveLane(ActiveLaneSyncPolicy::WorkspaceFocus, [&](Lane *lane) {
        const auto mode = lane->get_mode();
        const auto currentWindow = lane->get_active_window();
        const auto sourceMonitor = currentWindow ? ScrollerCore::HyprlandRuntime::monitorById(currentWindow->monitorID()) : getVisibleCanvasMonitor();

        if (CanvasLayoutInternal::direction_moves_between_lanes(mode, direction)) {
            handleMoveWindowAcrossLanes(workspace, direction, lane, currentWindow, sourceMonitor, mode);
            return;
        }

        handleMoveWindowWithinLane(workspace, direction, lane, currentWindow, sourceMonitor);
    });
    persistSnapshot();
}

// Align the active stack/window inside the current lane viewport.
//
// TODO(remove-align-window): Delete this entrypoint once the Lua/dispatch
// align_window command is removed.
void CanvasLayout::align_window(int workspace, Direction direction) {
    (void)workspace;
    withActiveLane(ActiveLaneSyncPolicy::WorkspaceFocus, [direction](Lane *lane) {
        lane->align_stack(direction);
    });
    persistSnapshot();
}

// Change the active lane traversal mode.
void CanvasLayout::set_mode(int workspace, Mode mode) {
    (void)workspace;
    withActiveLane(ActiveLaneSyncPolicy::WorkspaceFocus, [mode](Lane *lane) {
        lane->set_mode(mode);
    });
    relayoutVisibleCanvas();
    persistSnapshot();
}

// Resize the requested visible range so it fills the current lane viewport.
//
// TODO(remove-fit-size-command): Delete this public command path. Unlike
// overview (read-only navigation over a snapshot), this mutates real geometry.
void CanvasLayout::fit_size(int workspace, FitSize fitsize) {
    (void)workspace;
    withActiveLane(ActiveLaneSyncPolicy::WorkspaceFocus, [fitsize](Lane *lane) {
        lane->fit_size(fitsize);
    });
    persistSnapshot();
}

// Toggle scroller-managed fullscreen/expanded behavior.
void CanvasLayout::toggle_fullscreen(int workspace) {
    (void)workspace;
    const auto syncPolicy = consumeActiveLaneSyncPolicy();
    withActiveLane(syncPolicy, [](Lane *lane) {
        lane->toggle_fullscreen_active_window();
    });
    persistSnapshot();
}

// Move the active stack into a new persistent neighboring lane.
//
// TODO(remove-create-lane-command): Delete with the public create_lane command.
// Not covered by automated tests; redundant with move_focus/move_window flows.
void CanvasLayout::create_lane(int workspace, Direction direction) {
    (void)workspace;
    withActiveLane(ActiveLaneSyncPolicy::WorkspaceFocus, [&](Lane *lane) {
        if (!activeLane)
            return;

        auto stack = lane->extract_active_stack();
        if (!stack)
            return;

        auto currentLaneNode = activeLane;
        auto newLane = new Lane(stack);
        auto newLaneNode = insertLaneNode(newLane, direction, currentLaneNode);

        rememberLaneWindows(newLane);
        activeLane = newLaneNode;
        finishLaneTransfer(currentLaneNode, nullptr, false, true);
        debugVerifyLaneCache();
    });
    persistSnapshot();
}
