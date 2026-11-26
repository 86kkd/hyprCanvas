/**
 * @file layout/canvas/workspace_controller.h
 * @brief Multi-monitor canvas workspace switching and window transfer service.
 *
 * Owns real Hyprland workspace activation, directional slide transitions, and
 * cross-canvas window transfer. Both direct desktop keybindings and overview
 * acceptance delegate here so `Overview::Session` remains a pure read-only
 * preview layer.
 */
#pragma once

#include <vector>

#include <hyprland/src/SharedDefs.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>

#include "core/direction.h"
#include "core/service_slot.h"
#include "layout/canvas/canvas_workspace_repository.h"

namespace CanvasLayoutState {

class CanvasWorkspaceController {
  public:
    explicit CanvasWorkspaceController(CanvasWorkspaceRepository* repository = nullptr);

    // Activate every visible monitor's member workspace for `canvasId` with
    // relative-position slide animation.
    bool activateCanvas(int canvasId,
                        int selectedMonitorId,
                        bool requireSelectedMonitorFocus,
                        const std::vector<SyntheticCanvasWorkspace>& pending,
                        const char* context) const;

    // Sync canvas layout focus and switch Hyprland focus to `window`.
    bool finalizeCanvasTarget(WORKSPACEID workspaceId, const PHLWINDOW& window, bool warpCursor) const;

    // Direct desktop canvas switching (never opens or mutates Overview::Session).
    bool switchByDirection(Direction direction);
    bool switchByNumber(int number);

    // Move the focused tiled window to the same physical monitor in an adjacent
    // canvas without invoking Overview::Session.
    bool moveFocusedWindowToCanvas(Direction direction);

  private:
    CanvasWorkspaceRepository& repo() const;
    bool applyDesktopSwitch(int previousCanvasId,
                            int preferredMonitorId,
                            int targetCanvasId,
                            const std::vector<SyntheticCanvasWorkspace>& pending,
                            const char* context);

    CanvasWorkspaceRepository* repository_ = nullptr;
};

ScrollerCore::ServiceSlot<CanvasWorkspaceController>& workspaceControllerSlot();
CanvasWorkspaceController& workspaceController();

} // namespace CanvasLayoutState
