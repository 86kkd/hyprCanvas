#include <optional>

#include "layout/canvas/topology_route.h"

#include "test_suite.h"
#include "test_support.h"

namespace {

using CanvasLayoutInternal::LocalMove;
using CanvasLayoutInternal::Neighbor;
using CanvasLayoutInternal::NeighborIntent;
using CanvasLayoutInternal::NeighborKind;
using CanvasLayoutInternal::NeighborQuery;
using CanvasLayoutInternal::TopologyIndex;

// Row mode: stacks run left/right, windows and lanes run up/down.
TopologyIndex row_index(size_t laneIndex, size_t laneCount, size_t stackIndex, size_t stackCount,
                        size_t windowIndex = 0, size_t windowCount = 1) {
    return {
        .mode = Mode::Row,
        .laneCount = laneCount,
        .laneIndex = laneIndex,
        .stackCount = stackCount,
        .stackIndex = stackIndex,
        .windowCount = windowCount,
        .windowIndex = windowIndex,
    };
}

TopologyIndex empty_lane(size_t laneIndex, size_t laneCount) {
    return {.mode = Mode::Row, .laneCount = laneCount, .laneIndex = laneIndex};
}

Neighbor focus(const TopologyIndex &index, Direction direction, bool monitor = false, bool wrap = false) {
    return CanvasLayoutInternal::neighbor(index, {
        .intent = NeighborIntent::Focus,
        .direction = direction,
        .monitorInDirection = monitor,
        .focusWrap = wrap,
    });
}

Neighbor move(const TopologyIndex &index, Direction direction, bool monitor = false) {
    return CanvasLayoutInternal::neighbor(index, {
        .intent = NeighborIntent::MoveWindow,
        .direction = direction,
        .monitorInDirection = monitor,
    });
}

bool is_local(const Neighbor &n, LocalMove move, size_t stackIndex, std::optional<size_t> windowIndex = std::nullopt) {
    return n.kind == NeighborKind::LocalTarget && n.localMove == move && n.stackIndex == stackIndex &&
           n.windowIndex == windowIndex;
}

void test_focus_inside_lane() {
    const auto middle = row_index(0, 1, 1, 3, 1, 3);
    expect_true(is_local(focus(middle, Direction::Left), LocalMove::Focus, 0),
                "focus left selects the previous stack and keeps its active window");
    expect_true(is_local(focus(middle, Direction::Right), LocalMove::Focus, 2),
                "focus right selects the next stack");
    expect_true(is_local(focus(middle, Direction::Up), LocalMove::Focus, 1, 0),
                "row focus up walks to the previous window in the stack");
    expect_true(is_local(focus(middle, Direction::Down), LocalMove::Focus, 1, 2),
                "row focus down walks to the next window in the stack");
    expect_true(is_local(focus(middle, Direction::Begin), LocalMove::Focus, 0),
                "focus begin selects the first stack");
    expect_true(is_local(focus(middle, Direction::End), LocalMove::Focus, 2),
                "focus end selects the last stack");

    const TopologyIndex column{
        .mode = Mode::Column, .laneCount = 1, .stackCount = 2, .windowCount = 2,
    };
    expect_true(is_local(focus(column, Direction::Down), LocalMove::Focus, 1),
                "column focus down selects the next stack");
    expect_true(is_local(focus(column, Direction::Right), LocalMove::Focus, 0, 1),
                "column focus right walks to the next window in the stack");
}

void test_focus_at_lane_edges() {
    const auto first = row_index(0, 1, 0, 3);
    expect_eq(focus(first, Direction::Left).kind, NeighborKind::NoOp,
              "focus stops at the first stack without monitor or wrap");
    expect_eq(focus(first, Direction::Left, true).kind, NeighborKind::CrossMonitor,
              "focus leaves the monitor at the first stack");
    expect_true(is_local(focus(first, Direction::Left, false, true), LocalMove::Focus, 2),
                "focus wraps to the last stack when enabled");
    expect_eq(focus(first, Direction::Left, true, true).kind, NeighborKind::CrossMonitor,
              "a monitor in that direction wins over wrapping");
    expect_eq(focus(first, Direction::Begin).kind, NeighborKind::NoOp,
              "focus begin at the first stack is a no-op");
    expect_eq(focus(row_index(0, 1, 0, 1), Direction::Right, false, true).kind, NeighborKind::NoOp,
              "a single stack has nothing to wrap to");
}

void test_focus_between_lanes() {
    const auto topOfStack = row_index(1, 3, 0, 1, 0, 2);
    expect_true(focus(topOfStack, Direction::Up).kind == NeighborKind::AdjacentLane &&
                    focus(topOfStack, Direction::Up).laneIndex == 0,
                "focus up from the top window enters the previous lane");
    expect_true(is_local(focus(topOfStack, Direction::Up, false, true), LocalMove::Focus, 0, 1),
                "focus wrap inside the stack still wins without a monitor");
    expect_eq(focus(topOfStack, Direction::Up, true, true).kind, NeighborKind::AdjacentLane,
              "an adjacent lane wins over a monitor on the lane axis");

    const auto lastLane = row_index(2, 3, 0, 1);
    expect_eq(focus(lastLane, Direction::Down, true).kind, NeighborKind::CrossMonitor,
              "focus leaves the monitor past the last lane");
    const auto created = focus(lastLane, Direction::Down);
    expect_true(created.kind == NeighborKind::CreateEphemeral && created.laneIndex == 2,
                "focus past the last lane opens a blank lane after it");
    const auto createdBefore = focus(row_index(0, 3, 0, 1), Direction::Up);
    expect_true(createdBefore.kind == NeighborKind::CreateEphemeral && createdBefore.laneIndex == 0,
                "focus before the first lane opens a blank lane before it");
}

void test_focus_from_empty_lane() {
    expect_eq(focus(empty_lane(1, 2), Direction::Up).kind, NeighborKind::AdjacentLane,
              "an empty lane can move focus to an adjacent lane");
    expect_eq(focus(empty_lane(1, 2), Direction::Down).kind, NeighborKind::NoOp,
              "an empty lane never opens a second blank lane");
    expect_eq(focus(empty_lane(1, 2), Direction::Down, true).kind, NeighborKind::CrossMonitor,
              "an empty lane can still leave the monitor on the lane axis");
    expect_eq(focus(empty_lane(0, 1), Direction::Left, true).kind, NeighborKind::CrossMonitor,
              "an empty lane leaves the monitor on the stack axis");
    expect_eq(focus(empty_lane(0, 1), Direction::Left).kind, NeighborKind::NoOp,
              "an empty lane without a monitor stays put on the stack axis");
}

void test_move_window_routes() {
    const auto single = row_index(1, 3, 1, 3);
    expect_true(is_local(move(single, Direction::Left), LocalMove::JoinStack, 0),
                "a single-window stack joins the previous stack");
    expect_true(is_local(move(single, Direction::Right), LocalMove::JoinStack, 2),
                "a single-window stack joins the next stack");
    expect_true(is_local(move(single, Direction::Begin), LocalMove::ReorderStack, 0),
                "move begin reorders the stack to the lane start");

    const auto multi = row_index(0, 1, 0, 1, 0, 2);
    expect_true(is_local(move(multi, Direction::Left, true), LocalMove::SplitStack, 0),
                "a multi-window stack splits locally even at the edge with a monitor");
    expect_true(is_local(move(multi, Direction::Right), LocalMove::SplitStack, 1),
                "splitting forward places the new stack after the source");

    const auto edge = row_index(0, 1, 0, 1);
    expect_eq(move(edge, Direction::Left, true).kind, NeighborKind::CrossMonitor,
              "a single-window edge stack moves to the next monitor");
    expect_eq(move(edge, Direction::Left).kind, NeighborKind::NoOp,
              "move_window never wraps at the lane edge");

    expect_eq(move(row_index(1, 3, 0, 1, 0, 2), Direction::Up).kind, NeighborKind::AdjacentLane,
              "move_window on the lane axis skips windows inside the stack");
    expect_eq(move(row_index(2, 3, 0, 1), Direction::Down, true).kind, NeighborKind::CrossMonitor,
              "move_window prefers a monitor over a new lane");
    expect_eq(move(row_index(2, 3, 0, 1), Direction::Down).kind, NeighborKind::CreateEphemeral,
              "move_window past the last lane carries the window into a new lane");
}

void test_focus_and_move_share_lane_exits() {
    // Both commands use the same lane-axis exit order once the window walk is
    // done: adjacent lane, then monitor, then a new lane.
    for (const bool monitor : {false, true}) {
        for (const size_t laneIndex : {size_t{0}, size_t{1}, size_t{2}}) {
            const auto index = row_index(laneIndex, 3, 0, 1);
            const auto focused = focus(index, Direction::Down, monitor);
            const auto moved = move(index, Direction::Down, monitor);
            expect_true(focused.kind == moved.kind && focused.laneIndex == moved.laneIndex,
                        "focus and move_window resolve the same lane exit");
        }
    }
}

void test_invalid_index() {
    expect_eq(focus(TopologyIndex{}, Direction::Left, true).kind, NeighborKind::NoOp,
              "an index without lanes resolves to no-op");
}

} // namespace

void run_topology_route_tests() {
    test_focus_inside_lane();
    test_focus_at_lane_edges();
    test_focus_between_lanes();
    test_focus_from_empty_lane();
    test_move_window_routes();
    test_focus_and_move_share_lane_exits();
    test_invalid_index();
}
