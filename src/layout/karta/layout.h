/**
 * @file layout.h
 * @brief Layout controller for Karta's tiled workspace orchestration.
 *
 * Declares `KartaLayout`, the Hyprland tiled algorithm implementation that
 * owns the lanes of a karta, translates Hyprland layout callbacks, and exposes
 * command-facing operations through dispatchers.
 */
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_set>
#include <string>
#include <string_view>
#include <utility>

#include <hyprland/src/layout/algorithm/TiledAlgorithm.hpp>
#include <hyprland/src/layout/target/Target.hpp>
#include <hyprland/src/helpers/signal/Signal.hpp>

#include "core/owner_index.h"
#include "core/layout_snapshot.h"
#include "core/overview_snapshot.h"
#include "core/types.h"
#include "core/linked_list.h"
#include "model/stack/width.h"
#include "handoff_state.h"
#include "topology_route.h"

class Lane;

/**
 * @brief Tiled layout controller for one karta/workspace instance.
 *
 * `KartaLayout` is the integration layer between Hyprland's tiled algorithm
 * API and the plugin's internal model. It owns the ordered set of lanes for a
 * karta, keeps active-lane state in sync with Hyprland focus, and exposes the
 * dispatcher-facing operations used by the plugin.
 *
 * Mental model for new readers:
 * - one workspace owns one `KartaLayout`
 * - one `KartaLayout` owns an ordered list of `Lane` objects
 * - one `Lane` owns an ordered list of `Stack` objects
 * - one `Stack` owns an ordered list of windows
 *
 * Most of the project is just different ways of moving through or mutating
 * that hierarchy:
 * - Hyprland target callbacks enter through `newTarget` / `removeTarget`
 * - keybindings enter through dispatcher-facing methods like `moveFocus`
 * - `Lane` and `Stack` do the local membership and geometry work
 *
 * Recommended reading order for new contributors:
 * - start here for the high-level ownership model
 * - then read `src/layout/karta/core.cpp` for lifecycle and relayout flow
 * - then `src/layout/karta/focus.cpp` for directional navigation and handoff
 * - then `src/layout/lane/lane.h` and `src/layout/lane/core.cpp`
 * - finally `src/model/stack/stack.h` plus the stack implementation files
 */
class KartaLayout : public Layout::ITiledAlgorithm {
public:
    KartaLayout();
    ~KartaLayout() override;

    // Public hooks required by Hyprland's tiled algorithm interface.
    void                             newTarget(SP<Layout::ITarget> target) override;
    void                             movedTarget(SP<Layout::ITarget> target, std::optional<Vector2D> focalPoint = std::nullopt) override;
    void                             removeTarget(SP<Layout::ITarget> target) override;
    void                             resizeTarget(const Vector2D &delta, SP<Layout::ITarget> target, Layout::eRectCorner corner = Layout::CORNER_NONE) override;
    void                             recalculate(Layout::eRecalculateReason reason = Layout::RECALCULATE_REASON_UNKNOWN) override;
    Config::ErrorResult              layoutMsg(const std::string_view& sv) override;
    std::optional<Vector2D>          predictSizeForNewTarget() override;
    SP<Layout::ITarget>              getNextCandidate(SP<Layout::ITarget> old) override;
    void                             swapTargets(SP<Layout::ITarget> a, SP<Layout::ITarget> b) override;
    void                             moveTargetInDirection(SP<Layout::ITarget> t, Math::eDirection direction, bool silent = false) override;

    // Integration hooks used by dispatchers, overview snapshots, and karta controller.
    bool isWindowTiled(PHLWINDOW);
    void recalculateMonitor(const int &monitor_id);
    void onWindowFocusChange(PHLWINDOW);
    void prepareForActionContext();
    void prepareForOverviewSnapshot();
    KartaOverviewSnapshot buildOverviewSnapshot() const;
    void persistCurrentSnapshot();

    // Command-facing control surface from Hyprland config / Lua.
    // The usual call chain is:
    // keybind -> `hl.plugin.karta.*` -> `src/plugin/dispatch/` -> one of
    // these methods -> lane/stack helpers -> relayout/focus synchronization.
    void setWindowSize(int workspace, int visibleStacks);
    void applyStackWidth(KartaModel::StackWidth width);
    void moveFocus(int workspace, Direction);
    void moveWindow(int workspace, Direction);
    void moveWindowToMonitor(int workspace, Direction);
    void swapWindow(int workspace, Direction);
    void setMode(int workspace, Mode);
    void toggleFullscreen(int workspace);

private:
    using LaneNode = ListNode<std::unique_ptr<Lane>>;

    bool onWindowCreatedTiling(PHLWINDOW, Math::eDirection = Math::DIRECTION_DEFAULT);
    void onWindowRemovedTiling(PHLWINDOW);
    void recalculateWindow(PHLWINDOW);

    std::optional<KartaLayoutSnapshot::KartaSnapshot> captureSnapshot() const;
    void persistSnapshot();
    void clearPersistedSnapshot();
    bool maybeRestoreWorkspaceSnapshot();
    bool restoreSnapshot(const KartaLayoutSnapshot::KartaSnapshot &snapshot);
    // Disconnect listeners and drop all per-workspace model state.
    void resetRuntimeState();
    // Ensure workspace-bound listeners are attached for this layout's workspace.
    void ensureWorkspaceRuntime();
    // Resolve the single Hyprland workspace this layout instance tiles. This is
    // one member of a Karta Workspace, not the Karta Workspace itself.
    PHLWORKSPACE getLayoutWorkspace() const;
    // Return the currently active lane, defaulting to the first lane when needed.
    Lane *getActiveLane();
    // Point the karta at a new active lane.
    void setActiveLane(Lane *lane);
    // Find the lane that currently owns a window.
    Lane *getLaneForWindow(PHLWINDOW window);
    // Remember or update the cached lane owner for one window.
    void rememberWindowLane(PHLWINDOW window, Lane *lane);
    // Remove one window from the cached lane-owner index.
    void forgetWindowLane(PHLWINDOW window);
    // Refresh cached ownership for every window currently in the given lane.
    void rememberLaneWindows(Lane *lane);
    // Drop every cached entry that still points at the given lane pointer.
    void forgetLaneWindows(Lane *lane);
    // Validate the debug window -> lane cache against the actual lane contents.
    void debugVerifyLaneCache() const;
    // Return the list node for a lane inside this karta.
    LaneNode *getLaneNode(Lane *lane) const;
    // Return the zero-based index of a lane for logs and paging math.
    int laneIndexOf(Lane *lane) const;
    // Count lanes in the current karta.
    size_t laneCount() const;
    // Return the lane node at a zero-based index, or null when out of range.
    LaneNode *laneNodeAt(size_t index) const;
    // Derive the routing coordinates of a lane's focused item.
    KartaLayoutInternal::TopologyIndex topologyIndex(Lane *lane) const;
    // Resolve the monitor currently showing this karta.
    PHLMONITOR getVisibleKartaMonitor(PHLMONITOR fallbackMonitor = nullptr) const;
    // Relayout this karta on its visible monitor.
    void relayoutVisibleKarta(PHLMONITOR fallbackMonitor = nullptr);
    // Recalculate all lanes inside the karta against one monitor.
    void relayoutKarta(PHLMONITOR monitor, bool honor_fullscreen, bool preserve_restored_geometry = false);
    // Sweep all hidden special kartas so stale empty-lane state gets marked even when the hidden karta is not ticking.
    void syncHiddenSpecialWorkspaceKartas();
    // Track hidden/visible transitions for special-workspace empty lanes and restore focus when they reappear.
    bool syncSpecialWorkspaceVisibilityState(PHLMONITOR visibleMonitor);
    // Sync active lane/window state from Hyprland's remembered workspace focus.
    void syncActiveStateFromWorkspaceFocus();
    // Adopt the lane containing a newly focused window.
    bool adoptFocusedLane(PHLWINDOW focusedWindow, PHLMONITOR fallbackMonitor = nullptr);
    // Resolve the target window/lane selected for a cross-monitor focus handoff.
    PHLWINDOW resolveCrossMonitorFocusTarget(KartaLayout *targetLayout, PHLMONITOR monitor, WORKSPACEID workspaceId, Direction direction, PHLWINDOW sourceWindow, Lane **targetLane, const char **selection);
    // Apply active-lane state and relayout on the destination karta after cross-monitor focus selection.
    void activateCrossMonitorFocusTarget(KartaLayout *targetLayout, Lane *targetLane, PHLWINDOW targetWindow, PHLMONITOR fallbackMonitor);
    // Execute the full cross-monitor focus handoff for directional navigation.
    void handoffFocusAcrossMonitor(int workspace, Direction direction, PHLWINDOW sourceWindow, PHLMONITOR sourceMonitor, WORKSPACEID sourceActiveWorkspaceId, WORKSPACEID sourceSpecialWorkspaceId, LaneNode *sourceLaneNode, PHLMONITOR targetMonitor);
    // Execute the full cross-monitor movewindow handoff and relayout/focus updates.
    bool handoffMoveWindowAcrossMonitor(int workspace, Direction direction, Lane *sourceLane, PHLWINDOW currentWindow, PHLMONITOR sourceMonitor, PHLMONITOR targetMonitor);
    // Move the active window payload into an adjacent lane and finish source cleanup.
    void transferMoveWindowToAdjacentLane(Lane *sourceLane, PHLWINDOW currentWindow, LaneNode *targetLaneNode, Direction direction, PHLMONITOR sourceMonitor);
    // Move the active window payload into a newly created lane and finish source cleanup.
    void transferMoveWindowToNewLane(Lane *sourceLane, PHLWINDOW currentWindow, PHLMONITOR sourceMonitor, Mode mode, Direction direction, LaneNode *anchor);
    // Apply a move_window route that stays inside the lane.
    void applyLocalWindowMove(Lane *lane, KartaLayoutInternal::LocalMove move, Direction direction);
    // Focus the adjacent lane chosen by directional routing and clean up the source lane if needed.
    void focusAdjacentLane(int workspace, Direction direction, LaneNode *sourceLaneNode, PHLMONITOR sourceMonitor, LaneNode *targetLaneNode);
    // Create a temporary empty lane used when focus moves into blank space.
    void createEphemeralLaneForFocus(int workspace, Direction direction, PHLMONITOR sourceMonitor, Mode mode, LaneNode *anchor);
    // Finish a successful local focus movement by logging and switching focus.
    void finalizeLocalFocusMove(int workspace, Direction direction, Lane *lane);
    // Switch Hyprland focus to a window and resync karta state if the switch fails.
    bool focusManagedWindow(PHLWINDOW window, bool warpCursor = false, const char *context = nullptr, bool suppressWorkspaceSync = false);
    // Drop an empty lane and resolve a valid replacement active lane.
    bool dropEmptyLane(LaneNode *laneNode, Lane *preferredLane = nullptr, PHLMONITOR fallbackMonitor = nullptr, bool ephemeralOnly = false);
    // Choose the active lane that should survive after lane removal.
    Lane *resolveActiveLaneAfterRemoval(LaneNode *laneNode, PHLWINDOW removedWindow);
    // Insert a lane into karta ordering according to directional semantics.
    LaneNode *insertLaneNode(std::unique_ptr<Lane> lane, Direction direction, LaneNode *anchor = nullptr);
    // Return the active lane, creating one if this karta is still empty.
    Lane *ensureActiveLane(PHLMONITOR monitor, Mode mode);
    // Finish a lane transfer by pruning the source lane, relayouting, and focusing the active lane.
    void finishLaneTransfer(LaneNode *sourceLaneNode, PHLMONITOR sourceMonitor = nullptr, bool ephemeralOnly = false, bool warpCursor = true);

    template <typename Fn>
    // Execute a command against the current active lane with optional focus sync.
    void withActiveLane(ActiveLaneSyncPolicy syncPolicy, Fn&& fn) {
        if (syncPolicy == ActiveLaneSyncPolicy::WorkspaceFocus)
            syncActiveStateFromWorkspaceFocus();

        if (auto *lane = getActiveLane())
            std::forward<Fn>(fn)(lane);
    }

    // Optional Hyprland focus listener used to keep karta state synchronized.
    CHyprSignalListener m_focusCallback;
    // Workspace-active listener used to observe special workspace hide/show transitions.
    CHyprSignalListener m_workspaceActiveCallback;
    // Workspace id currently bound to the workspace-active listener.
    WORKSPACEID workspaceRuntimeId = WORKSPACE_INVALID;
    // Currently active lane inside this karta.
    LaneNode *activeLane = nullptr;
    // Ordered lanes that make up the current karta.
    List<std::unique_ptr<Lane>> lanes;
    // Cached window -> lane index used to avoid repeated whole-karta scans.
    KartaCore::OwnerIndex<uintptr_t, Lane> laneByWindow;
    std::unordered_set<uintptr_t> restoredTargetsAwaitingCallback;
    bool restoredGeometryActive = false;
    // Concentrated one-shot focus and cross-monitor handoff state.
    HandoffState handoffState;
    // Remember whether a hidden special workspace needs to restore from a stale empty lane when shown again.
    bool specialEphemeralLaneRestorePending = false;
    // Prevent save hooks from writing partially rebuilt state during restore.
    bool restoringSnapshot = false;
    // Fresh karta instances should only attempt repository restore once.
    bool snapshotRestoreAttempted = false;
};
