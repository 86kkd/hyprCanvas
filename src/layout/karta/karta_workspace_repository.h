#pragma once

#include <optional>
#include <string>
#include <vector>

#include <hyprland/src/SharedDefs.hpp>
#include <hyprland/src/helpers/signal/Signal.hpp>

#include "core/karta_workspace_snapshot.h"
#include "core/direction.h"
#include "core/service_slot.h"
#include "core/types.h"

namespace KartaLayoutState {

struct KartaWorkspaceMember {
    int         monitorId = -1;
    WORKSPACEID workspaceId = static_cast<WORKSPACEID>(-1);
    bool        special = false;
};

struct KartaWorkspaceRecord {
    int                                kartaId = -1;
    int                                tileX = 0;
    int                                tileY = 0;
    std::vector<KartaWorkspaceMember> members;
    bool                               ephemeral = false;
};

struct SyntheticKartaWorkspace {
    KartaWorkspaceRecord karta;
    int                   anchorKartaId = -1;
    Direction             direction = Direction::Right;
};

class KartaWorkspaceRepository {
  public:
    void initialize();
    void flush() const;
    void shutdown();
    void refreshTemporaryKartas(bool dropInactive = false);
    void promoteWorkspace(WORKSPACEID workspaceId);

    int activeKartaId() const;
    std::vector<KartaWorkspaceRecord> kartas() const;
    std::optional<KartaWorkspaceRecord> find(int kartaId) const;

    std::vector<KartaWorkspaceMember> currentVisibleMembers() const;
    int ensureCurrentVisibleKarta();

    SyntheticKartaWorkspace buildSyntheticKarta(Direction direction,
                                                 int anchorKartaId,
                                                 const std::vector<SyntheticKartaWorkspace>& existingSynthetics = {}) const;
    std::vector<KartaWorkspaceRecord> previewKartas(const std::vector<SyntheticKartaWorkspace>& synthetics = {}) const;
    void commitSyntheticKartas(const std::vector<SyntheticKartaWorkspace>& synthetics, int activeKartaId);

    void markActive(int kartaId);
    void ensureKartaHasVisibleMembers(int kartaId);

  private:
    std::string repositoryPath() const;
    void loadFromDisk();
    void flushToDisk() const;

    CHyprSignalListener windowOpen_;
    CHyprSignalListener windowMove_;
    bool initialized_ = false;
    KartaRepoSnapshot::RepositorySnapshot snapshot_;
};

KartaCore::ServiceSlot<KartaWorkspaceRepository>& kartaRepositorySlot();
KartaWorkspaceRepository& kartaRepository();

} // namespace KartaLayoutState
