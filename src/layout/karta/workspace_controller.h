/**
 * @file layout/karta/workspace_controller.h
 * @brief Multi-monitor karta workspace switching and window transfer service.
 *
 * Owns real Hyprland workspace activation, directional slide transitions, and
 * cross-karta window transfer. Both direct desktop keybindings and overview
 * acceptance delegate here so `Overview::Session` remains a pure read-only
 * preview layer.
 */
#pragma once

#include <vector>

#include <hyprland/src/SharedDefs.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>

#include "core/direction.h"
#include "core/service_slot.h"
#include "layout/karta/karta_workspace_repository.h"

namespace KartaLayoutState {

class KartaWorkspaceController {
  public:
    explicit KartaWorkspaceController(KartaWorkspaceRepository* repository = nullptr);

    // Activate every visible monitor's member workspace for `kartaId` with
    // relative-position slide animation.
    bool activateKarta(int kartaId,
                        int selectedMonitorId,
                        bool requireSelectedMonitorFocus,
                        const std::vector<SyntheticKartaWorkspace>& pending,
                        const char* context) const;

    // Sync karta layout focus and switch Hyprland focus to `window`.
    bool finalizeKartaTarget(WORKSPACEID workspaceId, const PHLWINDOW& window, bool warpCursor) const;

    // Direct desktop karta switching (never opens or mutates Overview::Session).
    bool switchByDirection(Direction direction);
    bool switchByNumber(int number);

    // Move the focused tiled window to the same physical monitor in an adjacent
    // karta without invoking Overview::Session.
    bool moveFocusedWindowToKarta(Direction direction);

  private:
    KartaWorkspaceRepository& repo() const;
    bool applyDesktopSwitch(int previousKartaId,
                            int preferredMonitorId,
                            int targetKartaId,
                            const std::vector<SyntheticKartaWorkspace>& pending,
                            const char* context);

    KartaWorkspaceRepository* repository_ = nullptr;
};

KartaCore::ServiceSlot<KartaWorkspaceController>& workspaceControllerSlot();
KartaWorkspaceController& workspaceController();

} // namespace KartaLayoutState
