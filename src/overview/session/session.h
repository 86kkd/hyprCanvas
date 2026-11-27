/**
 * @file session.h
 * @brief Global logical overview session spanning all canvases and monitors.
 *
 * The overview session is a navigation-only layer. Real windows remain owned by
 * their per-workspace `CanvasLayout`; overview builds a temporary logical map
 * of windows and empty-workspace targets so directional navigation can happen
 * without mutating real Hyprland focus on every keypress.
 */
#pragma once

#include "core/direction.h"
#include "core/service_slot.h"
#include "overview/model/model.h"
#include "overview/navigation/selection.h"

namespace CanvasLayoutState {
class CanvasWorkspaceController;
}

namespace Overview {

class Session {
  public:
    explicit Session(CanvasLayoutState::CanvasWorkspaceRepository* repository = nullptr,
                     CanvasLayoutState::CanvasWorkspaceController* controller = nullptr);

    bool active() const;
    void open();
    void close(bool acceptSelectionFlag);
    void dismiss();
    bool moveSelection(Direction direction);
    bool moveCanvasSelection(Direction direction);
    bool selectCanvasNumber(int number);
    bool previewCanvas(int canvasId);
    void markInputHandled();
    bool consumeInputHandled(bool released);
    const Model& model() const;
    void damageMonitors() const;

  private:
    CanvasLayoutState::CanvasWorkspaceRepository& canvasRepo() const;
    CanvasLayoutState::CanvasWorkspaceController& workspaceController() const;

    bool selectInitialTarget();
    bool selectCanvas(int canvasId, std::optional<int> preferredMonitorId = std::nullopt);
    bool switchPreviewCanvas(int canvasId, int preferredMonitorId, bool keepSelectedPending = true);
    bool acceptSelection();
    bool restoreOrigin();
    std::optional<TargetRef> findBestTarget(Direction direction) const;
    void clear();

    CanvasLayoutState::CanvasWorkspaceRepository* repository_ = nullptr;
    CanvasLayoutState::CanvasWorkspaceController* controller_ = nullptr;
    bool                  active_ = false;
    InputHandlingState    inputHandling_;
    Model                 model_;
    int                   originCanvasId_ = INVALID_CANVAS_ID;
    int                   viewCanvasId_ = INVALID_CANVAS_ID;
    std::vector<CanvasLayoutState::SyntheticCanvasWorkspace> pendingCanvases_;
};

CanvasCore::ServiceSlot<Session>& sessionSlot();
Session& session();

} // namespace Overview
