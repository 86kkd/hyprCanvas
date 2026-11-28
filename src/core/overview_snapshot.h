#pragma once

#include <vector>

#include "types.h"

struct KartaOverviewSnapshotWindow {
    PHLWINDOW         window = nullptr;
    KartaCore::Box box;
    PHLWINDOW         stackActiveWindow = nullptr;
};

struct KartaOverviewSnapshot {
    WORKSPACEID                               workspaceId = WORKSPACE_INVALID;
    int                                       monitorId = MONITOR_INVALID;
    std::vector<KartaOverviewSnapshotWindow> windows;
};
