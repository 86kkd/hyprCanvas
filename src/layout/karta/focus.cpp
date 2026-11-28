/**
 * @file focus.cpp
 * @brief Focus synchronization, directional focus movement, and monitor handoff.
 *
 * This file owns the "where should focus go next?" part of the karta layer.
 * It keeps plugin state aligned with Hyprland focus, routes directional focus
 * requests across stacks, lanes, and monitors, and handles temporary empty-lane
 * navigation used to emulate moving into blank space.
 *
 * New-reader mental model:
 * - local focus movement happens inside `Lane`
 * - this file decides when local movement is not enough
 * - if a move exits the current lane/page, this file chooses adjacent-lane,
 *   cross-monitor, or temporary-empty-lane behavior
 */
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <spdlog/spdlog.h>

#include "core/direction.h"
#include "plugin/config.h"
#include "internal.h"
#include "route.h"

// Synchronize the karta active lane/window with a concrete focused window.
void KartaLayout::onWindowFocusChange(PHLWINDOW window)
{
    const auto beforeLane = activeLane ? activeLane->data().get() : nullptr;
    const auto beforeWindow = beforeLane ? beforeLane->get_active_window() : nullptr;
    const auto beforeLaneIndex = laneIndexOf(beforeLane);
    const auto totalLanes = static_cast<int>(laneCount());

    if (window == nullptr) {
        spdlog::debug("onWindowFocusChange: ignored null window karta_ws={} lanes={} before_lane={} before_lane_index={} before_window={}",
                      KartaLayoutInternal::get_workspace_id(),
                      totalLanes,
                      static_cast<const void*>(beforeLane),
                      beforeLaneIndex,
                      static_cast<const void*>(beforeWindow ? beforeWindow.get() : nullptr));
        return;
    }

    auto lane = getLaneForWindow(window);
    const auto targetWindow = lane ? lane->get_active_window() : nullptr;
    const auto targetLaneIndex = laneIndexOf(lane);
    spdlog::info(
        "onWindowFocusChange: window={} workspace={} monitor={} karta_ws={} lane_found={} lanes={} before_lane={} before_lane_index={} before_window={} target_lane={} target_lane_index={} target_window={} same_lane={}",
        static_cast<const void*>(window.get()),
        KartaCore::HyprlandRuntime::windowWorkspaceId(window),
        window->monitorID(),
        KartaLayoutInternal::get_workspace_id(),
        lane != nullptr,
        totalLanes,
        static_cast<const void*>(beforeLane),
        beforeLaneIndex,
        static_cast<const void*>(beforeWindow ? beforeWindow.get() : nullptr),
        static_cast<const void*>(lane),
        targetLaneIndex,
        static_cast<const void*>(targetWindow ? targetWindow.get() : nullptr),
        beforeLane == lane);

    if (lane == nullptr) {
        spdlog::warn("onWindowFocusChange: window={} not managed by current karta karta_ws={} lanes={}",
                     static_cast<const void*>(window.get()),
                     KartaLayoutInternal::get_workspace_id(),
                     totalLanes);
        return;
    }

    setActiveLane(lane);
    lane->focus_window(window);

    const auto afterLane = activeLane ? activeLane->data().get() : nullptr;
    const auto afterWindow = afterLane ? afterLane->get_active_window() : nullptr;
    spdlog::info("onWindowFocusChange: synced window={} karta_ws={} after_lane={} after_lane_index={} after_window={}",
                 static_cast<const void*>(window.get()),
                 KartaLayoutInternal::get_workspace_id(),
                 static_cast<const void*>(afterLane),
                 laneIndexOf(afterLane),
                 static_cast<const void*>(afterWindow ? afterWindow.get() : nullptr));
    persistSnapshot();
}

bool KartaLayout::syncSpecialWorkspaceVisibilityState(PHLMONITOR visibleMonitor)
{
    const auto workspace = getLayoutWorkspace();
    const auto currentLane = activeLane ? activeLane->data().get() : nullptr;
    if (!workspace) {
        specialEphemeralLaneRestorePending = false;
        return false;
    }
    if (!KartaCore::HyprlandRuntime::isSpecialWorkspace(workspace)) {
        specialEphemeralLaneRestorePending = false;
        return false;
    }
    if (!currentLane)
        return false;

    const auto workspaceVisible = visibleMonitor != nullptr;
    // Hidden special workspaces can temporarily leave the karta pointing at an
    // empty ephemeral lane. Remember that state while hidden so we can re-adopt
    // the real focused lane when the special workspace becomes visible again.
    if (KartaLayoutInternal::should_mark_special_ephemeral_lane_for_restore(
            KartaCore::HyprlandRuntime::isSpecialWorkspace(workspace),
            workspaceVisible,
            currentLane->is_ephemeral(),
            currentLane->empty())) {
        specialEphemeralLaneRestorePending = true;
        return false;
    }

    if (workspaceVisible && specialEphemeralLaneRestorePending &&
        (!currentLane->is_ephemeral() || !currentLane->empty())) {
        specialEphemeralLaneRestorePending = false;
        return false;
    }

    if (!KartaLayoutInternal::should_restore_marked_special_ephemeral_lane(
            KartaCore::HyprlandRuntime::isSpecialWorkspace(workspace),
            workspaceVisible,
            specialEphemeralLaneRestorePending,
            currentLane->is_ephemeral(),
            currentLane->empty()))
        return false;

    // On reopen, pull the workspace's remembered focused window back into the
    // karta model before the next relayout reads stale ephemeral-lane state.
    syncActiveStateFromWorkspaceFocus();

    const auto afterLane = activeLane ? activeLane->data().get() : nullptr;
    const auto restored = !(afterLane && afterLane->is_ephemeral() && afterLane->empty());
    if (restored)
        specialEphemeralLaneRestorePending = false;
    return restored;
}

// Adopt the lane that owns Hyprland's current focused window, dropping a stale
// empty temporary lane when necessary.
bool KartaLayout::adoptFocusedLane(PHLWINDOW focusedWindow, PHLMONITOR fallbackMonitor)
{
    if (!focusedWindow)
        return false;

    const auto currentLane = activeLane ? activeLane->data().get() : nullptr;
    const auto currentWindow = currentLane ? currentLane->get_active_window() : nullptr;
    auto targetLane = getLaneForWindow(focusedWindow);
    if (!targetLane)
        return false;

    // If Hyprland focus jumped back onto a real window while the karta still
    // points at a blank ephemeral lane, discard that temporary lane first so
    // the active-lane pointer reattaches to real model state.
    if (activeLane && currentLane && currentLane->is_ephemeral() && currentLane->empty() && targetLane != currentLane)
        dropEmptyLane(activeLane, targetLane, fallbackMonitor, true);

    if (currentWindow == focusedWindow)
        return true;

    onWindowFocusChange(focusedWindow);
    return true;
}

// Pull remembered focus state from this layout's workspace and mirror it into the
// plugin's active lane/window state.
void KartaLayout::syncActiveStateFromWorkspaceFocus()
{
    const auto workspace = getLayoutWorkspace();
    if (!workspace)
        return;

    const auto focusedWindow = workspace->getLastFocusedWindow();
    const auto currentLane = activeLane ? activeLane->data().get() : nullptr;
    const auto currentWindow = currentLane ? currentLane->get_active_window() : nullptr;
    const auto managed = focusedWindow && getLaneForWindow(focusedWindow) != nullptr;

    spdlog::debug("syncActiveStateFromWorkspaceFocus: karta_ws={} focused_window={} focused_workspace={} focused_monitor={} managed={} current_window={}",
                  KartaCore::HyprlandRuntime::workspaceId(workspace),
                  static_cast<const void*>(focusedWindow ? focusedWindow.get() : nullptr),
                  focusedWindow ? KartaCore::HyprlandRuntime::windowWorkspaceId(focusedWindow) : WORKSPACE_INVALID,
                  focusedWindow ? focusedWindow->monitorID() : MONITOR_INVALID,
                  managed,
                  static_cast<const void*>(currentWindow ? currentWindow.get() : nullptr));

    if (!managed || currentWindow == focusedWindow)
        return;

    if (adoptFocusedLane(focusedWindow, getVisibleKartaMonitor()))
        spdlog::info("syncActiveStateFromWorkspaceFocus: adopted focused lane karta_ws={} focused_window={}",
                     KartaCore::HyprlandRuntime::workspaceId(workspace),
                     static_cast<const void*>(focusedWindow.get()));
}

// Resolve the window and lane that should receive focus after a cross-monitor
// handoff, preferring the destination karta's active lane before geometry fallback.
PHLWINDOW KartaLayout::resolveCrossMonitorFocusTarget(KartaLayout *targetLayout, PHLMONITOR monitor, WORKSPACEID workspaceId, Direction direction, PHLWINDOW sourceWindow, Lane **targetLane, const char **selection)
{
    auto *resolvedLane = targetLayout ? targetLayout->getActiveLane() : nullptr;
    const char *resolvedSelection = "geometry";

    // First try to let the destination karta re-adopt its remembered focused
    // lane/window. That preserves local intent better than pure geometry-based
    // fallback when the target workspace already has a meaningful active item.
    if (targetLayout)
        targetLayout->syncActiveStateFromWorkspaceFocus();

    resolvedLane = targetLayout ? targetLayout->getActiveLane() : nullptr;
    auto targetWindow = resolvedLane ? resolvedLane->get_active_window() : nullptr;
    if (targetWindow) {
        resolvedSelection = "active";
    } else {
        targetWindow = KartaLayoutInternal::pick_cross_monitor_target_window(monitor, workspaceId, direction, sourceWindow);
    }

    if (targetLayout && targetWindow && (!resolvedLane || !resolvedLane->has_window(targetWindow)))
        resolvedLane = targetLayout->getLaneForWindow(targetWindow);

    if (targetLane)
        *targetLane = resolvedLane;
    if (selection)
        *selection = resolvedSelection;

    return targetWindow;
}

// Apply destination-karta state once a cross-monitor target has been chosen.
void KartaLayout::activateCrossMonitorFocusTarget(KartaLayout *targetLayout, Lane *targetLane, PHLWINDOW targetWindow, PHLMONITOR fallbackMonitor)
{
    if (!targetLayout || !targetLane || !targetWindow)
        return;

    // Make the destination karta internally coherent before Hyprland focus is
    // switched. Otherwise subsequent relayout/focus-sync code would still think
    // the old lane is active.
    targetLayout->setActiveLane(targetLane);
    targetLane->focus_window(targetWindow);

    const auto targetWorkspace = targetLayout->getLayoutWorkspace();
    auto targetMonitor = targetWorkspace ? KartaLayoutInternal::visible_monitor_for_workspace(targetWorkspace) : fallbackMonitor;
    if (targetMonitor)
        targetLayout->relayoutKarta(targetMonitor, targetWorkspace && !KartaCore::HyprlandRuntime::isSpecialWorkspace(targetWorkspace));
}

// Execute the whole cross-monitor focus handoff and keep the destination karta
// state coherent before Hyprland focus is switched.
void KartaLayout::handoffFocusAcrossMonitor(int workspace, Direction direction, PHLWINDOW sourceWindow, PHLMONITOR sourceMonitor, WORKSPACEID sourceActiveWorkspaceId, WORKSPACEID sourceSpecialWorkspaceId, LaneNode *sourceLaneNode, PHLMONITOR targetMonitor)
{
    const auto activeWorkspaceId = targetMonitor ? KartaCore::HyprlandRuntime::activeWorkspaceId(targetMonitor) : WORKSPACE_INVALID;
    const auto workspaceId = KartaLayoutInternal::preferred_workspace_id(targetMonitor, workspace);
    const auto specialWorkspaceId = targetMonitor ? KartaCore::HyprlandRuntime::activeSpecialWorkspaceId(targetMonitor) : WORKSPACE_INVALID;
    spdlog::info(
        "move_focus_cross_monitor: source_workspace={} direction={} source_window={} "
        "source_active_ws={} source_special_ws={} dest_active_ws={} dest_special_ws={} selected_ws={}",
        workspace,
        KartaCore::direction_name(direction),
        static_cast<const void*>(sourceWindow ? sourceWindow.get() : nullptr),
        sourceActiveWorkspaceId,
        sourceSpecialWorkspaceId,
        activeWorkspaceId,
        specialWorkspaceId,
        workspaceId);
    if (!targetMonitor) {
        spdlog::warn("move_focus: no monitor in direction={} from workspace={}",
                     KartaCore::direction_name(direction), workspace);
        return;
    }

    // Phase 1: choose the destination workspace/lane/window.
    const char *targetSelection = "geometry";
    auto *targetLayout = KartaLayoutInternal::get_karta_for_workspace(workspaceId);
    auto *targetLane = static_cast<Lane *>(nullptr);
    const auto crossMonitorTarget = resolveCrossMonitorFocusTarget(targetLayout, targetMonitor, workspaceId, direction, sourceWindow, &targetLane, &targetSelection);
    // Phase 2: if the user left a blank ephemeral lane behind, clean it up
    // before we hand control to another monitor/workspace.
    if (dropEmptyLane(sourceLaneNode, nullptr, sourceMonitor, true)) {
        spdlog::info("move_focus: dropped empty lane after leaving workspace={} direction={}",
                     workspace, KartaCore::direction_name(direction));
    }
    persistSnapshot();

    // Phase 3: either focus an empty target workspace or activate the chosen
    // destination window and let Hyprland follow that focus.
    if (!crossMonitorTarget) {
        const auto targetWorkspace = KartaCore::HyprlandRuntime::workspaceById(workspaceId);
        spdlog::info("move_focus: no target window for crossed monitor workspace={} target_monitor={} target_workspace_found={}",
                     workspaceId,
                     targetMonitor->m_id,
                     targetWorkspace != nullptr);
        KartaLayoutInternal::focus_monitor_workspace(targetMonitor,
                                                      targetWorkspace,
                                                      workspaceId,
                                                      true,
                                                      "move_focus_cross_monitor_empty_target");
        return;
    }

    spdlog::info(
        "move_focus_cross_monitor_target: selected_ws={} target_window={} target_workspace={} "
        "target_layout_found={} target_lane_found={} selection={} target_pos=({}, {}) target_size=({}, {})",
        workspaceId,
        static_cast<const void*>(crossMonitorTarget ? crossMonitorTarget.get() : nullptr),
        crossMonitorTarget ? KartaCore::HyprlandRuntime::windowWorkspaceId(crossMonitorTarget) : WORKSPACE_INVALID,
        targetLayout != nullptr,
        targetLane != nullptr,
        targetSelection,
        KartaCore::HyprlandRuntime::windowPosition(crossMonitorTarget).x,
        KartaCore::HyprlandRuntime::windowPosition(crossMonitorTarget).y,
        KartaCore::HyprlandRuntime::windowSize(crossMonitorTarget).x,
        KartaCore::HyprlandRuntime::windowSize(crossMonitorTarget).y);

    if (targetLane == nullptr) {
        spdlog::warn("move_focus: no lane for crossed monitor target window={} workspace={}",
                     static_cast<const void*>(crossMonitorTarget.get()), workspaceId);
    } else {
        activateCrossMonitorFocusTarget(targetLayout, targetLane, crossMonitorTarget, targetMonitor);
    }

    spdlog::info("move_focus: workspace={} direction={} after={} result={}",
                 workspace,
                 KartaCore::direction_name(direction),
                 static_cast<const void*>(crossMonitorTarget.get()),
                 "cross_monitor");
    focusManagedWindow(crossMonitorTarget, true, "move_focus_cross_monitor", true);
}

void KartaLayout::focusAdjacentLane(int workspace, Direction direction, LaneNode *sourceLaneNode,
                                     PHLMONITOR sourceMonitor, LaneNode *targetLaneNode) {
    if (!targetLaneNode)
        return;

    // Adjacent-lane focus is the local-page analogue of cross-monitor handoff:
    // update active-lane state first, then prune any empty source lane, then
    // switch Hyprland focus to the destination lane's active window.
    activeLane = targetLaneNode;
    if (dropEmptyLane(sourceLaneNode, activeLane ? activeLane->data().get() : nullptr, sourceMonitor, true)) {
        spdlog::info("move_focus: dropped empty lane after leaving workspace={} direction={}",
                     workspace, KartaCore::direction_name(direction));
    } else {
        relayoutVisibleKarta(sourceMonitor);
    }

    const auto targetWindow = activeLane->data()->get_active_window();
    spdlog::info("move_focus: workspace={} direction={} lane_switch={} target_window={}",
                 workspace,
                 KartaCore::direction_name(direction),
                 true,
                 static_cast<const void*>(targetWindow ? targetWindow.get() : nullptr));
    if (targetWindow) {
        focusManagedWindow(targetWindow, true, "move_focus_adjacent_lane", true);
    } else {
        persistSnapshot();
    }
}

void KartaLayout::createEphemeralLaneForFocus(int workspace, Direction direction, PHLMONITOR sourceMonitor,
                                               Mode mode, LaneNode *anchor) {
    // Empty lanes are navigation-only placeholders. They let the user "move
    // into blank space" and then create/move content there later without
    // immediately mutating existing real lanes.
    auto newLane = std::make_unique<Lane>(sourceMonitor, mode);
    auto *newLanePtr = newLane.get();
    newLanePtr->set_ephemeral(true);
    auto newLaneNode = insertLaneNode(std::move(newLane), direction, anchor);

    activeLane = newLaneNode;
    relayoutVisibleKarta(sourceMonitor);
    spdlog::info("move_focus: created empty lane workspace={} direction={} lane={}",
                 workspace,
                 KartaCore::direction_name(direction),
                 static_cast<const void*>(newLanePtr));
    persistSnapshot();
}

void KartaLayout::finalizeLocalFocusMove(int workspace, Direction direction, Lane *lane) {
    if (!lane)
        return;

    const auto after = lane->get_active_window();
    spdlog::info("move_focus: workspace={} direction={} after={} result={}",
                 workspace,
                 KartaCore::direction_name(direction),
                 static_cast<const void*>(after ? after.get() : nullptr),
                 "moved");

    // Keep the plugin's active-lane pointer in sync before handing focus back
    // to Hyprland. Command code assumes the model already points at the same
    // lane/window Hyprland is about to focus.
    setActiveLane(lane);
    focusManagedWindow(after, true, "move_focus_local", true);
}

// Execute directional focus movement, including lane handoff, monitor handoff,
// and temporary empty-lane creation when the user moves into blank space.
// Reading guide:
// `dispatch_movefocus` resolves the active `KartaLayout` and lands here.
// `KartaLayoutInternal::neighbor` (shared with `moveWindow`) turns the
// request into one route: local focus move, adjacent-lane focus,
// cross-monitor handoff, or creation of a temporary empty lane.
void KartaLayout::moveFocus(int workspace, Direction direction)
{
    // Phase 1: snapshot the current lane/window/monitor state before any move.
    // Both local focus changes and cross-monitor handoff decisions depend on
    // the same source state, so keep that snapshot stable for the whole route.
    if (KartaLayoutInternal::should_sync_workspace_focus_before_move(activeLane ? activeLane->data().get() : nullptr))
        syncActiveStateFromWorkspaceFocus();
    auto lane = getActiveLane();
    const auto before = lane ? lane->get_active_window() : nullptr;
    const auto activeKartaMonitor = getVisibleKartaMonitor();
    const auto sourceMonitor = before ? KartaCore::HyprlandRuntime::monitorById(before->monitorID())
                                     : (activeKartaMonitor ? activeKartaMonitor : KartaCore::monitorForKeyboardAction());
    const auto beforeActiveWorkspaceId = sourceMonitor ? KartaCore::HyprlandRuntime::activeWorkspaceId(sourceMonitor) : WORKSPACE_INVALID;
    const auto beforeSpecialWorkspaceId = sourceMonitor ? KartaCore::HyprlandRuntime::activeSpecialWorkspaceId(sourceMonitor) : WORKSPACE_INVALID;
    auto sourceLaneNode = activeLane;
    spdlog::info("move_focus: workspace={} direction={} lane_found={} before={}",
                 workspace, KartaCore::direction_name(direction), lane != nullptr,
                 static_cast<const void*>(before ? before.get() : nullptr));
    if (lane == nullptr) {
        KartaLayoutInternal::dispatch_builtin_movefocus(direction);
        return;
    }

    // Phase 2: ask the topology index where this direction leads, then run
    // exactly one action for that answer.
    const auto targetMonitor = KartaLayoutInternal::resolve_monitor_in_direction(sourceMonitor, direction);
    const auto next = KartaLayoutInternal::neighbor(topologyIndex(lane), {
        .intent = KartaLayoutInternal::NeighborIntent::Focus,
        .direction = direction,
        .monitorInDirection = targetMonitor != nullptr,
        .focusWrap = karta::plugin_config::focusWrap(),
    });

    switch (next.kind) {
    case KartaLayoutInternal::NeighborKind::NoOp:
        return;
    case KartaLayoutInternal::NeighborKind::LocalTarget:
        lane->focus_position(next.stackIndex, next.windowIndex);
        finalizeLocalFocusMove(workspace, direction, lane);
        return;
    case KartaLayoutInternal::NeighborKind::AdjacentLane:
        focusAdjacentLane(workspace, direction, sourceLaneNode, sourceMonitor, laneNodeAt(next.laneIndex));
        return;
    case KartaLayoutInternal::NeighborKind::CreateEphemeral:
        createEphemeralLaneForFocus(workspace, direction, sourceMonitor, lane->get_mode(), laneNodeAt(next.laneIndex));
        return;
    case KartaLayoutInternal::NeighborKind::CrossMonitor:
        handoffFocusAcrossMonitor(workspace, direction, before, sourceMonitor, beforeActiveWorkspaceId,
                                  beforeSpecialWorkspaceId, sourceLaneNode, targetMonitor);
        return;
    }
}
