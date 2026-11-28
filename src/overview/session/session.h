/**
 * @file session.h
 * @brief Global logical overview session spanning all kartas and monitors.
 *
 * The overview session is a navigation-only layer. Real windows remain owned by
 * their per-workspace `KartaLayout`; overview builds a temporary logical map
 * of windows and empty-workspace targets so directional navigation can happen
 * without mutating real Hyprland focus on every keypress.
 */
#pragma once

#include "core/direction.h"
#include "core/service_slot.h"
#include "overview/model/model.h"
#include "overview/navigation/selection.h"

namespace KartaLayoutState {
class KartaWorkspaceController;
}

namespace Overview {

class Session {
  public:
    explicit Session(KartaLayoutState::KartaWorkspaceRepository* repository = nullptr,
                     KartaLayoutState::KartaWorkspaceController* controller = nullptr);

    bool active() const;
    void open();
    void close(bool acceptSelectionFlag);
    void dismiss();
    bool moveSelection(Direction direction);
    bool moveKartaSelection(Direction direction);
    bool selectKartaNumber(int number);
    bool previewKarta(int kartaId);
    void markInputHandled();
    bool consumeInputHandled(bool released);
    const Model& model() const;
    void damageMonitors() const;

  private:
    KartaLayoutState::KartaWorkspaceRepository& kartaRepo() const;
    KartaLayoutState::KartaWorkspaceController& workspaceController() const;

    bool selectInitialTarget();
    bool selectKarta(int kartaId, std::optional<int> preferredMonitorId = std::nullopt);
    bool switchPreviewKarta(int kartaId, int preferredMonitorId, bool keepSelectedPending = true);
    bool acceptSelection();
    bool restoreOrigin();
    std::optional<TargetRef> findBestTarget(Direction direction) const;
    void clear();

    KartaLayoutState::KartaWorkspaceRepository* repository_ = nullptr;
    KartaLayoutState::KartaWorkspaceController* controller_ = nullptr;
    bool                  active_ = false;
    InputHandlingState    inputHandling_;
    Model                 model_;
    int                   originKartaId_ = INVALID_KARTA_ID;
    int                   viewKartaId_ = INVALID_KARTA_ID;
    std::vector<KartaLayoutState::SyntheticKartaWorkspace> pendingKartas_;
};

KartaCore::ServiceSlot<Session>& sessionSlot();
Session& session();

} // namespace Overview
