#pragma once

#include <optional>
#include <string>

#include "core/layout_snapshot.h"
#include "core/service_slot.h"

namespace CanvasLayoutState {

class WorkspaceLayoutRepository {
public:
    void initialize();
    void flush() const;

    std::optional<CanvasLayoutSnapshot::CanvasSnapshot> find(int workspaceId) const;
    void upsert(CanvasLayoutSnapshot::CanvasSnapshot snapshot);
    void erase(int workspaceId);

private:
    std::string repositoryPath() const;
    void loadFromDisk();
    void flushToDisk() const;

    bool initialized_ = false;
    CanvasLayoutSnapshot::RepositorySnapshot snapshots_;
};

CanvasCore::ServiceSlot<WorkspaceLayoutRepository> &repositorySlot();
WorkspaceLayoutRepository &repository();

} // namespace CanvasLayoutState
