#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "types.h"

namespace ScrollerSnapshot {

constexpr int kLegacyFormatVersion = 1;
constexpr int kGridFormatVersion = 2;
constexpr int kGridModeFormatVersion = 3;
constexpr int kFullscreenFormatVersion = 4;
constexpr int kStableWindowKeyFormatVersion = 5;
constexpr int kFormatVersion = kStableWindowKeyFormatVersion;

struct WindowSnapshot {
    uintptr_t key = 0;
    int       heightMode = 0;
    double    geomY = 0.0;
    double    geomH = 0.0;
    double    memY = 0.0;
    double    memH = 0.0;
};

struct StackSnapshot {
    int                          width = 0;
    bool                         fullscreened = false;
    bool                         maximized = false;
    ScrollerCore::Box            geom;
    ScrollerCore::Box            memGeom;
    uintptr_t                    activeWindowKey = 0;
    std::vector<WindowSnapshot>  windows;
};

struct LaneSnapshot {
    int                         mode = 0;
    bool                        ephemeral = false;
    size_t                      activeStackIndex = 0;
    std::vector<StackSnapshot>  stacks;
};

struct CanvasSnapshot {
    int                        version = kFormatVersion;
    int                        workspaceId = -1;
    size_t                     activeLaneIndex = 0;
    std::vector<LaneSnapshot>  lanes;
};

using RepositorySnapshot = std::unordered_map<int, CanvasSnapshot>;

std::string serialize_repository(const RepositorySnapshot &snapshots);
std::optional<RepositorySnapshot> deserialize_repository(std::string_view data);

} // namespace ScrollerSnapshot
