#pragma once

#include <optional>
#include <string>

#include "core/layout_snapshot.h"
#include "core/service_slot.h"

namespace KartaLayoutState {

class WorkspaceLayoutRepository {
public:
    void initialize();
    void flush() const;

    std::optional<KartaLayoutSnapshot::KartaSnapshot> find(int workspaceId) const;
    void upsert(KartaLayoutSnapshot::KartaSnapshot snapshot);
    void erase(int workspaceId);

private:
    std::string repositoryPath() const;
    void loadFromDisk();
    void flushToDisk() const;

    bool initialized_ = false;
    KartaLayoutSnapshot::RepositorySnapshot snapshots_;
};

KartaCore::ServiceSlot<WorkspaceLayoutRepository> &repositorySlot();
WorkspaceLayoutRepository &repository();

} // namespace KartaLayoutState
