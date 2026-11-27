#pragma once

#include <optional>
#include <string>
#include <vector>

#include <hyprland/src/SharedDefs.hpp>
#include <hyprland/src/helpers/signal/Signal.hpp>

#include "core/canvas_workspace_snapshot.h"
#include "core/direction.h"
#include "core/service_slot.h"
#include "core/types.h"

namespace CanvasLayoutState {

struct CanvasWorkspaceMember {
    int         monitorId = -1;
    WORKSPACEID workspaceId = static_cast<WORKSPACEID>(-1);
    bool        special = false;
};

struct CanvasWorkspaceRecord {
    int                                canvasId = -1;
    int                                tileX = 0;
    int                                tileY = 0;
    std::vector<CanvasWorkspaceMember> members;
    bool                               ephemeral = false;
};

struct SyntheticCanvasWorkspace {
    CanvasWorkspaceRecord canvas;
    int                   anchorCanvasId = -1;
    Direction             direction = Direction::Right;
};

class CanvasWorkspaceRepository {
  public:
    void initialize();
    void flush() const;
    void shutdown();
    void refreshTemporaryCanvases(bool dropInactive = false);
    void promoteWorkspace(WORKSPACEID workspaceId);

    int activeCanvasId() const;
    std::vector<CanvasWorkspaceRecord> canvases() const;
    std::optional<CanvasWorkspaceRecord> find(int canvasId) const;

    std::vector<CanvasWorkspaceMember> currentVisibleMembers() const;
    int ensureCurrentVisibleCanvas();

    SyntheticCanvasWorkspace buildSyntheticCanvas(Direction direction,
                                                 int anchorCanvasId,
                                                 const std::vector<SyntheticCanvasWorkspace>& existingSynthetics = {}) const;
    std::vector<CanvasWorkspaceRecord> previewCanvases(const std::vector<SyntheticCanvasWorkspace>& synthetics = {}) const;
    void commitSyntheticCanvases(const std::vector<SyntheticCanvasWorkspace>& synthetics, int activeCanvasId);

    void markActive(int canvasId);
    void ensureCanvasHasVisibleMembers(int canvasId);

  private:
    std::string repositoryPath() const;
    void loadFromDisk();
    void flushToDisk() const;

    CHyprSignalListener windowOpen_;
    CHyprSignalListener windowMove_;
    bool initialized_ = false;
    CanvasRepoSnapshot::RepositorySnapshot snapshot_;
};

CanvasCore::ServiceSlot<CanvasWorkspaceRepository>& canvasRepositorySlot();
CanvasWorkspaceRepository& canvasRepository();

} // namespace CanvasLayoutState
