/**
 * @file topology_route.h
 * @brief Read-only topology index and the shared directional routing decision.
 *
 * `move_focus` and `move_window` both ask one question first: "from the
 * focused item, what is the neighbor in this direction?" The answer only
 * depends on the focused item's lane/stack/window coordinates, the lane mode,
 * and whether another monitor lies in that direction. Keeping that decision
 * here, without compositor or lane pointers, lets both commands share it and
 * lets tests cover every route directly.
 */
#pragma once

#include <cstddef>
#include <optional>

#include "core/types.h"

namespace KartaLayoutInternal {

// Coordinates of the focused item, derived from the karta lane list for every
// directional command. Lanes and stacks stay the only source of truth.
struct TopologyIndex {
    Mode   mode = Mode::Row;
    size_t laneCount = 0;
    size_t laneIndex = 0;
    // Zero for an empty (ephemeral) lane.
    size_t stackCount = 0;
    size_t stackIndex = 0;
    // Windows in the active stack.
    size_t windowCount = 0;
    size_t windowIndex = 0;

    bool laneEmpty() const {
        return stackCount == 0;
    }
};

enum class NeighborIntent {
    // Move focus to the neighbor.
    Focus,
    // Carry the active window toward the neighbor.
    MoveWindow,
};

struct NeighborQuery {
    NeighborIntent intent = NeighborIntent::Focus;
    Direction      direction = Direction::Left;
    bool           monitorInDirection = false;
    bool           focusWrap = false;
};

enum class NeighborKind {
    NoOp,
    // The target stays in the current lane; see `LocalMove`.
    LocalTarget,
    AdjacentLane,
    // Open a new lane at the karta edge (empty for focus, holding the window
    // for move_window).
    CreateEphemeral,
    CrossMonitor,
};

enum class LocalMove {
    // Focus the stack at `stackIndex`, and `windowIndex` inside it when set.
    Focus,
    // Move the whole active stack to `stackIndex` (lane begin/end).
    ReorderStack,
    // Swap the active window with its neighbor at `windowIndex` in the stack.
    ReorderWindow,
    // Move the active window into the neighboring stack at `stackIndex`.
    JoinStack,
    // Split the active window into a new stack on the requested side.
    SplitStack,
};

struct Neighbor {
    NeighborKind          kind = NeighborKind::NoOp;
    LocalMove             localMove = LocalMove::Focus;
    // AdjacentLane: target lane. CreateEphemeral: edge lane the new lane is
    // inserted next to.
    size_t                laneIndex = 0;
    size_t                stackIndex = 0;
    std::optional<size_t> windowIndex = std::nullopt;
};

// Resolve the neighbor of the focused item for one directional command.
Neighbor neighbor(const TopologyIndex &index, const NeighborQuery &query);
// Focus routing restricted to the physically adjacent stack in this lane.
Neighbor swap_window_neighbor(const TopologyIndex &index, Direction direction);

} // namespace KartaLayoutInternal
