#include "topology_route.h"

#include "core/layout_profile.h"

namespace CanvasLayoutInternal {
namespace {

Neighbor no_op() {
    return {};
}

Neighbor cross_monitor() {
    return {.kind = NeighborKind::CrossMonitor};
}

Neighbor local(LocalMove move, size_t stackIndex, std::optional<size_t> windowIndex = std::nullopt) {
    return {
        .kind = NeighborKind::LocalTarget,
        .localMove = move,
        .stackIndex = stackIndex,
        .windowIndex = windowIndex,
    };
}

// Lane-axis exit: an existing neighbor lane wins, then another monitor, then
// a new lane at the canvas edge.
Neighbor lane_neighbor(const TopologyIndex &index, const NeighborQuery &query, bool allowCreate) {
    const bool backward = query.direction == ScrollerCore::lane_backward_direction(index.mode);
    if (backward ? index.laneIndex > 0 : index.laneIndex + 1 < index.laneCount)
        return {
            .kind = NeighborKind::AdjacentLane,
            .laneIndex = backward ? index.laneIndex - 1 : index.laneIndex + 1,
        };
    if (query.monitorInDirection)
        return cross_monitor();
    if (allowCreate)
        return {
            .kind = NeighborKind::CreateEphemeral,
            .laneIndex = backward ? 0 : index.laneCount - 1,
        };
    return no_op();
}

// Lane-axis commands first walk the windows of the active stack, because the
// stack-item axis and the lane axis are the same screen axis.
std::optional<Neighbor> stack_window_neighbor(const TopologyIndex &index, const NeighborQuery &query) {
    const bool backward = query.direction == ScrollerCore::stack_item_backward_direction(index.mode);
    const bool focus = query.intent == NeighborIntent::Focus;
    if (backward ? index.windowIndex > 0 : index.windowIndex + 1 < index.windowCount)
        return local(focus ? LocalMove::Focus : LocalMove::ReorderWindow, index.stackIndex,
                     backward ? index.windowIndex - 1 : index.windowIndex + 1);
    // Wrapping inside the stack is a last resort when no monitor is there.
    if (focus && !query.monitorInDirection && query.focusWrap && index.windowCount > 1)
        return local(LocalMove::Focus, index.stackIndex, backward ? index.windowCount - 1 : 0);
    return std::nullopt;
}

Neighbor local_focus_neighbor(const TopologyIndex &index, const NeighborQuery &query) {
    const auto last = index.stackCount - 1;
    switch (query.direction) {
    case Direction::Begin:
        return index.stackIndex > 0 ? local(LocalMove::Focus, 0) : no_op();
    case Direction::End:
        return index.stackIndex < last ? local(LocalMove::Focus, last) : no_op();
    default:
        break;
    }

    const bool backward = query.direction == ScrollerCore::local_item_backward_direction(index.mode);
    if (!backward && query.direction != ScrollerCore::local_item_forward_direction(index.mode))
        return query.monitorInDirection ? cross_monitor() : no_op();

    if (backward ? index.stackIndex > 0 : index.stackIndex < last)
        return local(LocalMove::Focus, backward ? index.stackIndex - 1 : index.stackIndex + 1);
    if (query.monitorInDirection)
        return cross_monitor();
    if (query.focusWrap && index.stackCount > 1)
        return local(LocalMove::Focus, backward ? last : 0);
    return no_op();
}

Neighbor local_move_neighbor(const TopologyIndex &index, const NeighborQuery &query) {
    const auto last = index.stackCount - 1;
    switch (query.direction) {
    case Direction::Begin:
        return index.stackIndex > 0 ? local(LocalMove::ReorderStack, 0) : no_op();
    case Direction::End:
        return index.stackIndex < last ? local(LocalMove::ReorderStack, last) : no_op();
    default:
        break;
    }

    const bool backward = query.direction == ScrollerCore::local_item_backward_direction(index.mode);
    if (!backward && query.direction != ScrollerCore::local_item_forward_direction(index.mode))
        return no_op();

    // A multi-window stack always splits locally, even at the lane edge;
    // only a single-window stack can join a neighbor or leave the monitor.
    if (index.windowCount > 1)
        return local(LocalMove::SplitStack, backward ? index.stackIndex : index.stackIndex + 1);
    if (backward ? index.stackIndex > 0 : index.stackIndex < last)
        return local(LocalMove::JoinStack, backward ? index.stackIndex - 1 : index.stackIndex + 1);
    return query.monitorInDirection ? cross_monitor() : no_op();
}

} // namespace

Neighbor neighbor(const TopologyIndex &index, const NeighborQuery &query) {
    if (index.laneCount == 0 || index.laneIndex >= index.laneCount)
        return no_op();

    const bool focus = query.intent == NeighborIntent::Focus;
    if (ScrollerCore::direction_moves_between_lanes(index.mode, query.direction)) {
        if (!index.laneEmpty()) {
            if (const auto inStack = stack_window_neighbor(index, query))
                return *inStack;
        }
        // Focus never opens a second blank lane from an empty one.
        return lane_neighbor(index, query, !focus || !index.laneEmpty());
    }

    if (index.laneEmpty())
        return focus && query.monitorInDirection ? cross_monitor() : no_op();

    return focus ? local_focus_neighbor(index, query) : local_move_neighbor(index, query);
}

Neighbor swap_window_neighbor(const TopologyIndex &index, Direction direction) {
    if (direction != ScrollerCore::local_item_backward_direction(index.mode) &&
        direction != ScrollerCore::local_item_forward_direction(index.mode))
        return no_op();
    const auto target = neighbor(index, {.intent = NeighborIntent::Focus, .direction = direction});
    if (target.kind != NeighborKind::LocalTarget || target.stackIndex == index.stackIndex)
        return no_op();
    return target;
}

} // namespace CanvasLayoutInternal
