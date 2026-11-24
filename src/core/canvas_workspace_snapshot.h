#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ScrollerCanvasSnapshot {

constexpr int kFormatVersion = 1;

// A Canvas Workspace is one whole multi-monitor desktop: exactly one ordinary
// Hyprland workspace per monitor. Windows still belong to those workspaces;
// the canvas only groups them so they can be switched together.
struct MonitorMemberSnapshot {
    int  monitorId = -1;
    int  workspaceId = -1;
    // Only older state files carry special members; new members never do.
    bool special = false;
};

struct CanvasWorkspaceSnapshot {
    int                               canvasId = -1;
    int                               tileX = 0;
    int                               tileY = 0;
    std::vector<MonitorMemberSnapshot> members;
    bool                              ephemeral = false;
};

struct RepositorySnapshot {
    int                                version = kFormatVersion;
    int                                activeCanvasId = -1;
    std::vector<CanvasWorkspaceSnapshot> canvases;
};

// Ordinary (non-special) workspace a monitor currently shows.
struct MonitorWorkspace {
    int monitorId = -1;
    int workspaceId = -1;
};

// Members for the desktop on screen right now. A special workspace opened on
// top of a monitor is an overlay, so callers pass the ordinary workspace under
// it and the result never contains special members.
std::vector<MonitorMemberSnapshot> visible_members(std::vector<MonitorWorkspace> monitors);

// Give a canvas exactly one ordinary member per connected monitor: special and
// duplicate entries are dropped and missing monitors get fresh workspace IDs.
// Members of disconnected monitors are kept for when the monitor returns.
bool fill_monitor_members(CanvasWorkspaceSnapshot& canvas, const std::vector<int>& monitorIds, int nextWorkspaceId);

// Preserve the visible canvas; replace other references to the same ordinary
// workspace with fresh IDs. No windows or compositor workspaces are mutated.
bool make_members_exclusive(RepositorySnapshot& snapshot, int preferredCanvasId, int nextWorkspaceId);

// Promote occupied temporary canvases; discard empty inactive ones on departure.
bool reconcile_temporary_canvases(RepositorySnapshot& snapshot, const std::vector<int>& occupiedWorkspaceIds, bool dropInactive);

std::string                      serialize_repository(const RepositorySnapshot& snapshot);
std::optional<RepositorySnapshot> deserialize_repository(std::string_view data);

} // namespace ScrollerCanvasSnapshot
