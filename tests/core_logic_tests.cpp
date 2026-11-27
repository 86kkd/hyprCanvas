#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "core/direction.h"
#include "core/canvas_transition.h"
#include "core/span_scale.h"
#include "core/interval.h"
#include "core/layout_math.h"
#include "core/layout_profile.h"
#include "core/monitor_geometry.h"
#include "core/owner_index.h"
#include "core/service_slot.h"
#include "core/linked_list.h"
#include "core/workarea_pager.h"
#include "model/stack/logic.h"
#include "model/stack/width.h"

#include "test_suite.h"
#include "test_support.h"

namespace {

struct FakeOwner {
    int              id = 0;
    std::vector<int> keys;

    bool owns(int key) const {
        return std::find(keys.begin(), keys.end(), key) != keys.end();
    }
};

struct FakeSpanItem {
    double span = 0.0;
};

void expect_reserved_workarea(std::string_view label,
                              const Hyprutils::Math::Vector2D &position,
                              const Hyprutils::Math::Vector2D &rawSize,
                              const Hyprutils::Math::Vector2D &transformedSize,
                              wl_output_transform transform,
                              const CanvasCore::ReservedEdges &rawReserved,
                              const CanvasCore::ReservedEdges &expectedReserved,
                              const CanvasCore::Box &expectedWorkarea) {
    const auto logicalReserved = CanvasCore::logical_reserved_edges(rawSize, transformedSize, transform, rawReserved);
    expect_near(logicalReserved.top, expectedReserved.top, 1e-9, std::string(label) + " reserved top");
    expect_near(logicalReserved.right, expectedReserved.right, 1e-9, std::string(label) + " reserved right");
    expect_near(logicalReserved.bottom, expectedReserved.bottom, 1e-9, std::string(label) + " reserved bottom");
    expect_near(logicalReserved.left, expectedReserved.left, 1e-9, std::string(label) + " reserved left");

    const auto workarea = CanvasCore::logical_workarea_box(position, rawSize, transformedSize, transform, rawReserved, 0.0);
    expect_near(workarea.x, expectedWorkarea.x, 1e-9, std::string(label) + " workarea x");
    expect_near(workarea.y, expectedWorkarea.y, 1e-9, std::string(label) + " workarea y");
    expect_near(workarea.w, expectedWorkarea.w, 1e-9, std::string(label) + " workarea width");
    expect_near(workarea.h, expectedWorkarea.h, 1e-9, std::string(label) + " workarea height");
}

void test_canvas_transition() {
    using CanvasCore::canvas_transition_offset;
    const Hyprutils::Math::Vector2D size{1200, 800};
    const auto check = [&](int x, int y, double expectedX, double expectedY) {
        const auto offset = canvas_transition_offset(4, -2, x, y, size);
        expect_near(offset.x, expectedX, 1e-9, "canvas transition x follows tile coordinates");
        expect_near(offset.y, expectedY, 1e-9, "canvas transition y follows tile coordinates");
        const auto reverse = canvas_transition_offset(x, y, 4, -2, size);
        expect_near(reverse.x, -expectedX, 1e-9, "return transition reverses x");
        expect_near(reverse.y, -expectedY, 1e-9, "return transition reverses y");
    };
    check(5, -2, 1200, 0);
    check(3, -2, -1200, 0);
    check(4, -3, 0, -800);
    check(4, -1, 0, 800);
    check(4, -2, 0, 0);
    check(104, -2, 1200, 0);
    check(6, -1, 1200, 400);
    check(3, -4, -600, -800);
    const auto portrait = canvas_transition_offset(0, 0, 0, 1, {600, 1000});
    expect_near(portrait.y, 1000, 1e-9, "each monitor uses its own logical extent");
}

void test_interval() {
    expect_true(CanvasCore::Interval::intersects(0.0, 10.0, 5.0, 15.0), "interval partial overlap intersects");
    expect_true(CanvasCore::Interval::intersects(0.0, 20.0, 5.0, 15.0), "interval containing viewport intersects");
    expect_true(!CanvasCore::Interval::intersects(0.0, 5.0, 5.0, 15.0), "touching edge does not intersect");
    expect_true(CanvasCore::Interval::fully_visible(6.0, 9.0, 5.0, 15.0), "fully visible interval reports true");
    expect_true(!CanvasCore::Interval::fully_visible(4.0, 9.0, 5.0, 15.0), "partially clipped interval reports false");
}

void test_direction_helpers() {
    expect_eq(std::string_view(CanvasCore::direction_name(Direction::Begin)), std::string_view("begin"),
              "direction_name returns begin");
    expect_eq(std::string_view(CanvasCore::direction_dispatch_arg(Direction::Right)), std::string_view("r"),
              "direction_dispatch_arg returns short right");
    expect_true(CanvasCore::direction_dispatch_arg(Direction::Center) == nullptr,
                "direction_dispatch_arg rejects center");
    expect_eq(CanvasCore::opposite_direction(Direction::Up), Direction::Down,
              "opposite_direction flips up to down");
}

void test_parse_helpers() {
    const auto down = CanvasCore::parse_direction_arg("dn");
    expect_true(down.has_value() && *down == Direction::Down, "parse_direction_arg handles dn alias");

    const auto center = CanvasCore::parse_direction_arg("centre");
    expect_true(center.has_value() && *center == Direction::Center, "parse_direction_arg handles centre alias");

    expect_true(!CanvasCore::parse_direction_arg("sideways").has_value(),
                "parse_direction_arg rejects invalid input");

    const auto rowMode = CanvasCore::parse_mode_arg("row");
    expect_true(rowMode.has_value() && *rowMode == Mode::Row,
                "parse_mode_arg handles row");

    const auto columnMode = CanvasCore::parse_mode_arg("col");
    expect_true(columnMode.has_value() && *columnMode == Mode::Column,
                "parse_mode_arg handles col alias");

    expect_true(!CanvasCore::parse_mode_arg("grid").has_value(),
                "parse_mode_arg rejects invalid input");
}

void test_anchor_selection() {
    const CanvasCore::Box visible(100.0, 50.0, 400.0, 300.0);

    expect_near(CanvasCore::choose_anchor_x(true, false, 150.0, 120.0, 0.0, 260.0, visible),
                230.0, 1e-9, "choose_anchor_x keeps active and next visible");
    expect_near(CanvasCore::choose_anchor_x(true, true, 200.0, 250.0, 150.0, 275.0, visible),
                250.0, 1e-9, "choose_anchor_x falls back to prev width when next does not fit");
    expect_near(CanvasCore::choose_anchor_x(false, true, 350.0, 0.0, 100.0, 275.0, visible),
                150.0, 1e-9, "choose_anchor_x aligns active to right edge when only prev exists");

    expect_near(CanvasCore::choose_anchor_y(true, false, 120.0, 100.0, 0.0, visible),
                130.0, 1e-9, "choose_anchor_y keeps next visible when possible");
    expect_near(CanvasCore::choose_anchor_y(false, true, 180.0, 0.0, 100.0, visible),
                150.0, 1e-9, "choose_anchor_y positions after prev when it fits");
    expect_near(CanvasCore::choose_anchor_y(false, true, 250.0, 0.0, 100.0, visible),
                100.0, 1e-9, "choose_anchor_y aligns to bottom when only prev exists but cannot fit");

    expect_near(CanvasCore::center_span(100.0, 400.0, 150.0),
                225.0, 1e-9, "center_span preserves the outer origin when centering");
    expect_near(CanvasCore::center_span(-320.0, 640.0, 320.0),
                -160.0, 1e-9, "center_span handles non-zero negative origins");

    const auto rendered = CanvasCore::rendered_local_interval(-1855.0, 1908.0, 4.0, 0.0, 16.0);
    expect_near(rendered.start, -1851.0, 1e-9, "rendered_local_interval applies border and leading gap to start");
    expect_near(rendered.end, 33.0, 1e-9, "rendered_local_interval matches final client bottom after border and trailing gap");
}

void test_layout_profile() {
    expect_eq(CanvasCore::layout_orientation_for_extent(1920.0, 1080.0),
              CanvasCore::LayoutOrientation::Landscape,
              "wide extents map to landscape");
    expect_eq(CanvasCore::layout_orientation_for_extent(1080.0, 1920.0),
              CanvasCore::LayoutOrientation::Portrait,
              "tall extents map to portrait");
    expect_eq(CanvasCore::default_mode_for_extent(1920.0, 1080.0),
              Mode::Row,
              "landscape extents default to row mode");
    expect_eq(CanvasCore::default_mode_for_extent(1080.0, 1920.0),
              Mode::Column,
              "portrait extents default to column mode");

    expect_true(CanvasCore::mode_pages_lanes_vertically(Mode::Row),
                "row mode pages lanes vertically");

    expect_eq(CanvasCore::local_item_backward_direction(Mode::Row),
              Direction::Left,
              "row mode local backward direction is left");
    expect_eq(CanvasCore::local_item_forward_direction(Mode::Column),
              Direction::Down,
              "column mode local forward direction is down");
    expect_eq(CanvasCore::stack_item_backward_direction(Mode::Row),
              Direction::Up,
              "row mode stack backward direction is up");
    expect_eq(CanvasCore::stack_item_forward_direction(Mode::Column),
              Direction::Right,
              "column mode stack forward direction is right");
    expect_eq(CanvasCore::lane_backward_direction(Mode::Row),
              Direction::Up,
              "row mode lane backward direction is up");
    expect_eq(CanvasCore::lane_forward_direction(Mode::Column),
              Direction::Right,
              "column mode lane forward direction is right");

    expect_near(CanvasCore::stack_local_origin_for_window(Mode::Row, {10.0, 20.0}),
                20.0, 1e-9, "row mode stack-local window origin uses y");
    expect_near(CanvasCore::stack_local_origin_for_window(Mode::Column, {10.0, 20.0}),
                10.0, 1e-9, "column mode stack-local window origin uses x");
    expect_near(CanvasCore::stack_primary_span_limit(Mode::Row, {100.0, 50.0, 640.0, 360.0}),
                640.0, 1e-9, "row mode stack primary span limit uses workarea width");
    expect_near(CanvasCore::stack_primary_span_limit(Mode::Column, {100.0, 50.0, 640.0, 360.0}),
                360.0, 1e-9, "column mode stack primary span limit uses workarea height");

    expect_true(CanvasCore::direction_targets_local_item(Mode::Row, Direction::Left),
                "row mode treats left as local item movement");
    expect_true(CanvasCore::direction_moves_between_lanes(Mode::Row, Direction::Down),
                "row mode treats down as cross-lane movement");
    expect_true(CanvasCore::direction_moves_between_lanes(Mode::Column, Direction::Left),
                "column mode treats left as cross-lane movement");
    expect_true(CanvasCore::direction_inserts_before_current(Mode::Column, Direction::Left),
                "column mode inserts before current on left");

    const auto portraitPrediction = CanvasCore::predict_window_size(Mode::Column, {0.0, 0.0, 800.0, 600.0});
    expect_near(portraitPrediction.x, 800.0, 1e-9, "column mode prediction keeps full width");
    expect_near(portraitPrediction.y, 300.0, 1e-9, "column mode prediction halves height");
}

void test_monitor_geometry() {
    const Hyprutils::Math::Vector2D landscapePosition{3840.0, 0.0};
    const Hyprutils::Math::Vector2D landscapeRawSize{3840.0, 2160.0};
    const Hyprutils::Math::Vector2D landscapeTransformedSize{3840.0, 2160.0};
    const Hyprutils::Math::Vector2D portraitPosition{1680.0, 0.0};
    const Hyprutils::Math::Vector2D portraitRawSize{3840.0, 2160.0};
    const Hyprutils::Math::Vector2D portraitTransformedSize{2160.0, 3840.0};

    expect_reserved_workarea("landscape top",
                             landscapePosition,
                             landscapeRawSize,
                             landscapeTransformedSize,
                             WL_OUTPUT_TRANSFORM_NORMAL,
                             {.top = 37.0, .right = 0.0, .bottom = 0.0, .left = 0.0},
                             {.top = 37.0, .right = 0.0, .bottom = 0.0, .left = 0.0},
                             {3840.0, 37.0, 3840.0, 2123.0});
    expect_reserved_workarea("landscape right",
                             landscapePosition,
                             landscapeRawSize,
                             landscapeTransformedSize,
                             WL_OUTPUT_TRANSFORM_NORMAL,
                             {.top = 0.0, .right = 37.0, .bottom = 0.0, .left = 0.0},
                             {.top = 0.0, .right = 37.0, .bottom = 0.0, .left = 0.0},
                             {3840.0, 0.0, 3803.0, 2160.0});
    expect_reserved_workarea("landscape bottom",
                             landscapePosition,
                             landscapeRawSize,
                             landscapeTransformedSize,
                             WL_OUTPUT_TRANSFORM_NORMAL,
                             {.top = 0.0, .right = 0.0, .bottom = 37.0, .left = 0.0},
                             {.top = 0.0, .right = 0.0, .bottom = 37.0, .left = 0.0},
                             {3840.0, 0.0, 3840.0, 2123.0});
    expect_reserved_workarea("landscape left",
                             landscapePosition,
                             landscapeRawSize,
                             landscapeTransformedSize,
                             WL_OUTPUT_TRANSFORM_NORMAL,
                             {.top = 0.0, .right = 0.0, .bottom = 0.0, .left = 37.0},
                             {.top = 0.0, .right = 0.0, .bottom = 0.0, .left = 37.0},
                             {3877.0, 0.0, 3803.0, 2160.0});

    const auto portraitSize = CanvasCore::logical_monitor_size({3840.0, 2160.0},
                                                                 {2160.0, 3840.0},
                                                                 WL_OUTPUT_TRANSFORM_270);
    expect_near(portraitSize.x, 2160.0, 1e-9, "portrait logical width uses transformed width");
    expect_near(portraitSize.y, 3840.0, 1e-9, "portrait logical height uses transformed height");

    expect_reserved_workarea("portrait top",
                             portraitPosition,
                             portraitRawSize,
                             portraitTransformedSize,
                             WL_OUTPUT_TRANSFORM_270,
                             {.top = 37.0, .right = 0.0, .bottom = 0.0, .left = 0.0},
                             {.top = 37.0, .right = 0.0, .bottom = 0.0, .left = 0.0},
                             {1680.0, 37.0, 2160.0, 3803.0});
    expect_reserved_workarea("portrait right",
                             portraitPosition,
                             portraitRawSize,
                             portraitTransformedSize,
                             WL_OUTPUT_TRANSFORM_270,
                             {.top = 0.0, .right = 37.0, .bottom = 0.0, .left = 0.0},
                             {.top = 0.0, .right = 37.0, .bottom = 0.0, .left = 0.0},
                             {1680.0, 0.0, 2123.0, 3840.0});
    expect_reserved_workarea("portrait bottom",
                             portraitPosition,
                             portraitRawSize,
                             portraitTransformedSize,
                             WL_OUTPUT_TRANSFORM_270,
                             {.top = 0.0, .right = 0.0, .bottom = 37.0, .left = 0.0},
                             {.top = 0.0, .right = 0.0, .bottom = 37.0, .left = 0.0},
                             {1680.0, 0.0, 2160.0, 3803.0});
    expect_reserved_workarea("portrait left",
                             portraitPosition,
                             portraitRawSize,
                             portraitTransformedSize,
                             WL_OUTPUT_TRANSFORM_270,
                             {.top = 0.0, .right = 0.0, .bottom = 0.0, .left = 37.0},
                             {.top = 0.0, .right = 0.0, .bottom = 0.0, .left = 37.0},
                             {1717.0, 0.0, 2123.0, 3840.0});

    const auto fallbackSize = CanvasCore::logical_monitor_size({3840.0, 2160.0},
                                                                 {0.0, 0.0},
                                                                 WL_OUTPUT_TRANSFORM_270);
    expect_near(fallbackSize.x, 2160.0, 1e-9, "missing transformed size still swaps portrait axes");
    expect_near(fallbackSize.y, 3840.0, 1e-9, "missing transformed size still preserves portrait height");
}

void test_workarea_pager() {
    const CanvasCore::Box full{0.0, 0.0, 1080.0, 1920.0};
    const CanvasCore::Box workarea{0.0, 40.0, 1080.0, 1880.0};
    const auto above = CanvasCore::project_box_to_workarea_page({0.0, -900.0, 1080.0, 940.0}, full, workarea);
    expect_near(above.committed.y, -940.0, 1e-9, "workarea pager keeps top reserved edge clear");
    expect_true(!above.visible, "workarea pager marks fully hidden top box invisible");

    const CanvasCore::Box fullWithLeftBar{0.0, 0.0, 1920.0, 1080.0};
    const CanvasCore::Box workareaWithLeftBar{48.0, 0.0, 1872.0, 1080.0};
    const auto left = CanvasCore::project_box_to_workarea_page({-888.0, 0.0, 936.0, 1080.0}, fullWithLeftBar, workareaWithLeftBar);
    expect_near(left.committed.x, -936.0, 1e-9, "workarea pager keeps left reserved edge clear");
    expect_true(!left.visible, "workarea pager marks fully hidden left box invisible");
}

void test_owner_index() {
    CanvasCore::OwnerIndex<int, FakeOwner> index;
    FakeOwner stackA{.id = 1, .keys = {1, 2}};
    FakeOwner stackB{.id = 2, .keys = {3}};

    index.remember_owner(&stackA, [&](auto &&remember) {
        for (const auto key : stackA.keys)
            remember(key);
    });
    index.remember_owner(&stackB, [&](auto &&remember) {
        for (const auto key : stackB.keys)
            remember(key);
    });

    expect_true(index.find_valid(2, [&](FakeOwner *owner) { return owner && owner->owns(2); }) == &stackA,
                "owner index returns the cached owner when it still owns the key");
    expect_true(index.matches_expected([&](auto &&addExpected) {
        addExpected(1, &stackA);
        addExpected(2, &stackA);
        addExpected(3, &stackB);
    }), "owner index matches the expected owner mapping after initial population");

    stackA.keys = {1};
    expect_true(index.find_valid(2, [&](FakeOwner *owner) { return owner && owner->owns(2); }) == nullptr,
                "owner index evicts stale cached keys when the owner no longer matches");
    expect_true(index.matches_expected([&](auto &&addExpected) {
        addExpected(1, &stackA);
        addExpected(3, &stackB);
    }), "owner index keeps unrelated keys after evicting one stale entry");

    index.forget_owner(&stackA);
    expect_true(index.matches_expected([&](auto &&addExpected) {
        addExpected(3, &stackB);
    }), "owner index can forget every key that still points at one owner");

    index.clear();
    expect_true(index.matches_expected([&](auto &&) {}),
                "owner index clear removes every cached key");
}

void test_span_scale_helper() {
    FakeSpanItem itemA{.span = 10.0};
    FakeSpanItem itemB{.span = 20.0};
    FakeSpanItem itemC{.span = 30.0};
    FakeSpanItem itemD{.span = 40.0};
    List<FakeSpanItem *> items;
    items.push_back(&itemA);
    items.push_back(&itemB);
    items.push_back(&itemC);
    items.push_back(&itemD);

    const auto normalized = CanvasCore::scale_range_to_span(
        items.first()->next(),
        items.last()->prev(),
        100.0,
        [](ListNode<FakeSpanItem *> *node) {
            return node->data()->span;
        },
        [](ListNode<FakeSpanItem *> *node, double scaledSpan) {
            node->data()->span = scaledSpan;
        });
    expect_true(normalized, "span scale helper normalizes non-empty ranges");
    expect_near(itemB.span, 40.0, 1e-9,
                "span scale helper preserves proportional span for the first node");
    expect_near(itemC.span, 60.0, 1e-9,
                "span scale helper preserves proportional span for the second node");

    FakeSpanItem zeroA{.span = 0.0};
    FakeSpanItem zeroB{.span = 0.0};
    List<FakeSpanItem *> zeros;
    zeros.push_back(&zeroA);
    zeros.push_back(&zeroB);
    expect_true(!CanvasCore::scale_range_to_span(
                    zeros.first(),
                    zeros.last(),
                    80.0,
                    [](ListNode<FakeSpanItem *> *node) {
                        return node->data()->span;
                    },
                    [](ListNode<FakeSpanItem *> *node, double scaledSpan) {
                        node->data()->span = scaledSpan;
                    }),
                "span scale helper rejects zero-total ranges");
}

void test_stack_width_visible_count() {
    using CanvasModel::StackWidth;
    expect_near(StackWidth::visible(3).extent(1800.0), 600.0, 1e-9,
                "visible stack width divides the workarea by its count");
    expect_near(StackWidth::free().extent(1800.0), 1800.0, 1e-9,
                "free stack width reports the full workarea");
    expect_eq(StackWidth::visible(0).visible_stacks(), StackWidth::kMinVisibleStacks,
              "visible stack width clamps counts below the minimum");
    expect_eq(StackWidth::visible(99).visible_stacks(), StackWidth::kMaxVisibleStacks,
              "visible stack width clamps counts above the maximum");

    expect_true(StackWidth::from_snapshot(0).is_free(), "snapshot width 0 restores a free stack");
    expect_eq(StackWidth::from_snapshot(4).visible_stacks(), 4, "snapshot width keeps a valid visible count");
    expect_eq(StackWidth::from_snapshot(-3).visible_stacks(), StackWidth::kDefaultVisibleStacks,
              "invalid snapshot width falls back to the default count");
    expect_eq(StackWidth::visible(3).to_snapshot(), 3, "visible stack width serializes its count");

    const auto three = CanvasModel::parse_visible_stack_count("3");
    expect_true(three.has_value() && *three == 3, "visible stack count parses integers");
    expect_true(!CanvasModel::parse_visible_stack_count("0").has_value(),
                "visible stack count rejects zero");
    expect_true(!CanvasModel::parse_visible_stack_count("9").has_value(),
                "visible stack count rejects counts above the maximum");
    expect_true(!CanvasModel::parse_visible_stack_count("next").has_value(),
                "visible stack count rejects the removed cycle arguments");
    expect_true(!CanvasModel::parse_visible_stack_count("2.5").has_value(),
                "visible stack count rejects fractional input");
}

void test_list_move_only() {
    static_assert(!std::is_copy_constructible_v<List<int>>);
    static_assert(!std::is_copy_assignable_v<List<int>>);
    static_assert(std::is_move_constructible_v<List<int>>);
    static_assert(std::is_move_assignable_v<List<int>>);

    List<int> original;
    original.push_back(1);
    original.push_back(2);

    List<int> moved(std::move(original));
    expect_true(original.empty(), "moved-from list becomes empty after move construction");
    expect_eq(moved.size(), std::size_t{2}, "move construction preserves list size");
    expect_eq(moved.first()->data(), 1, "move construction preserves first node data");
    expect_eq(moved.last()->data(), 2, "move construction preserves last node data");

    List<int> assigned;
    assigned.push_back(99);
    assigned = std::move(moved);
    expect_true(moved.empty(), "moved-from list becomes empty after move assignment");
    expect_eq(assigned.size(), std::size_t{2}, "move assignment replaces the destination contents");
    expect_eq(assigned.first()->data(), 1, "move assignment preserves first node data");
    expect_eq(assigned.last()->data(), 2, "move assignment preserves last node data");

    // Edge cases: null iterator handling and unique_ptr ownership.
    assigned.erase(nullptr);
    expect_eq(assigned.size(), std::size_t{2}, "erasing null node is a safe no-op");
    expect_eq(assigned.extract(nullptr), 0, "extracting null node returns default value");

    assigned.emplace_after(nullptr, 3);
    expect_eq(assigned.size(), std::size_t{3}, "emplace_after(nullptr) on non-empty list appends safely");
    expect_eq(assigned.last()->data(), 3, "emplace_after(nullptr) places element at tail");

    assigned.emplace_before(nullptr, 0);
    expect_eq(assigned.size(), std::size_t{4}, "emplace_before(nullptr) on non-empty list prepends safely");
    expect_eq(assigned.first()->data(), 0, "emplace_before(nullptr) places element at head");

    List<std::unique_ptr<int>> owned;
    owned.push_back(std::make_unique<int>(10));
    owned.push_back(std::make_unique<int>(20));
    auto *firstNode = owned.first();
    auto *lastNode = owned.last();
    owned.swap(firstNode, lastNode);
    expect_eq(*owned.first()->data(), 20, "swapping unique_ptr nodes exchanges ownership");
    expect_eq(*owned.last()->data(), 10, "swapping unique_ptr nodes updates tail ownership");

    auto extracted = owned.extract(owned.first());
    expect_true(extracted != nullptr && *extracted == 20, "extract transfers unique_ptr ownership out of list");
    expect_eq(owned.size(), std::size_t{1}, "extract shrinks list size");
    expect_eq(*owned.first()->data(), 10, "extract preserves remaining elements");
}

void test_stack_logic() {
    List<int> values;
    values.push_back(1);
    values.push_back(2);
    values.push_back(3);

    auto *first = values.first();
    auto *second = first->next();
    auto *last = values.last();
    expect_true(CanvasModel::StackLogic::next_active_after_removal(first, last) == second,
                "stack logic prefers the next node when removing a non-tail active item");
    expect_true(CanvasModel::StackLogic::next_active_after_removal(last, last) == second,
                "stack logic falls back to the previous node when removing the tail active item");

    List<int> single;
    single.push_back(42);
    expect_true(CanvasModel::StackLogic::next_active_after_removal(single.first(), single.last()) == nullptr,
                "stack logic clears active selection when removing the only node");
}

void test_stack_deck() {
    using CanvasModel::StackLogic::deck_cards;
    expect_true(deck_cards(2, 0, 900.0, 800.0).empty(), "two windows keep ordinary layout");
    expect_true(deck_cards(3, 3, 900.0, 800.0).empty(), "invalid active index produces no cards");
    expect_true(deck_cards(3, 0, 0.0, 800.0).empty(), "empty viewport produces no cards");
    // Exercise both navigation directions and missing/stale history. Verify
    // actual occlusion along the local axis, not only placement formulas.
    for (const size_t count : {3u, 4u, 5u, 100u}) {
        for (size_t active = 0; active < count; ++active) {
            for (const size_t previous : {active > 0 ? active - 1 : count,
                                          active + 1, active, count}) {
                const auto cards = deck_cards(count, active, 900.0, 800.0, previous);
                const auto half = previous < count && previous != active
                    ? previous : (active > 0 ? active - 1 : active + 1);
                expect_true(cards.size() == count, "deck includes all windows");
                expect_true(cards.back().index == active, "active card is painted last");
                std::vector<bool> seen(count, false);
                std::vector<double> offsets(count);
                for (size_t i = 0; i < count; ++i) {
                    const auto &card = cards[i];
                    expect_true(card.narrow == (card.index != active && card.index != half),
                                "only narrow background cards receive edge labels");
                    expect_true(!seen[card.index], "each window appears once");
                    seen[card.index] = true;
                    offsets[card.index] = card.offset;
                    expect_true(card.offset >= 0.0 && card.offset + card.span <= 900.0 + 1e-9,
                                "all cards stay within viewport");
                    expect_true(800.0 - 2.0 * card.inset > 0.0, "card widths remain positive");
                    // Every later card occludes an interval of this card.
                    std::vector<std::pair<double, double>> visible{{card.offset, card.offset + card.span}};
                    for (size_t j = i + 1; j < count; ++j) {
                        std::vector<std::pair<double, double>> remaining;
                        const auto start = cards[j].offset;
                        const auto end = start + cards[j].span;
                        for (const auto &[lo, hi] : visible) {
                            if (hi <= start || lo >= end)
                                remaining.emplace_back(lo, hi);
                            else {
                                if (lo < start)
                                    remaining.emplace_back(lo, start);
                                if (hi > end)
                                    remaining.emplace_back(end, hi);
                            }
                        }
                        visible = std::move(remaining);
                    }
                    double exposure = 0.0;
                    for (const auto &[lo, hi] : visible)
                        exposure += hi - lo;
                    if (card.index == active)
                        expect_near(exposure, 556.2, 1e-9, "active window occupies 61.8 percent");
                    else if (card.index == half)
                        expect_near(exposure, 171.9, 1e-9, "last focused window occupies 19.1 percent on either side");
                    else
                        expect_near(exposure, 171.9 / (count - 2), 1e-9, "other windows equally share 19.1 percent");
                }
                expect_near(offsets.front(), 0.0, 1e-9, "first window remains at start");
                expect_near(offsets.back() + cards.back().span, 900.0, 1e-9, "last window remains at end");
                for (size_t i = 1; i < count; ++i)
                    expect_true(offsets[i - 1] < offsets[i], "focus changes preserve original spatial order");
            }
        }
    }
    // Tuner-approved scales are independent of deck mode and viewport aspect.
    for (const bool fullsize : {false, true}) {
        for (const double crossSpan : {400.0, 800.0, 1920.0}) {
            const auto fraction = [crossSpan](const auto& cards, size_t index) {
                for (const auto& card : cards)
                    if (card.index == index)
                        return (crossSpan - 2 * card.inset) / crossSpan;
                return -1.0;
            };
            const auto end = deck_cards(6, 5, 900, crossSpan, 4, fullsize);
            const double expected[] = {0.623295, 0.69255, 0.7695, 0.855, 0.95, 1.0};
            for (size_t i = 0; i < 6; ++i)
                expect_near(fraction(end, i), expected[i], 1e-9,
                            "approved six-card progressive cross-axis sizes");
            const auto middle = deck_cards(6, 2, 900, crossSpan, 1, fullsize);
            expect_near(fraction(middle, 0), 0.855, 1e-9, "left side first other layer");
            expect_near(fraction(middle, 3), 0.855, 1e-9, "right side restarts decay");
            expect_near(fraction(middle, 5), 0.69255, 1e-9, "right side third other layer");
            const auto distant = deck_cards(6, 5, 900, crossSpan, 0, fullsize);
            expect_near(fraction(distant, 0), 0.95, 1e-9, "distant previous focus stays secondary");
            expect_near(fraction(distant, 4), 0.855, 1e-9, "nearest other starts first decay layer");
            const auto fallback = deck_cards(6, 5, 900, crossSpan, 99, fullsize);
            expect_near(fraction(fallback, 4), 0.95, 1e-9, "stale history uses adjacent secondary");
        }
    }
    const auto tiny = deck_cards(20, 7, 30.0, 20.0);
    expect_true(tiny.back().span > 0.0, "small viewports keep positive card spans");
}

void test_fullsize_deck() {
    using namespace CanvasModel::StackLogic;
    using CanvasCore::Box;
    expect_true(deck_cards(1, 0, 1000, 800, std::nullopt, true).empty(),
                "single fullsize window keeps ordinary fullscreen geometry");
    for (size_t count : {2u, 3u, 5u, 20u}) {
        for (size_t active = 0; active < count; ++active) {
            for (size_t previous = 0; previous < count; ++previous) {
                if (active == previous)
                    continue;
                const auto cards = deck_cards(count, active, 1000, 800, previous, true);
                for (const auto mode : {Mode::Row, Mode::Column}) {
                    std::vector<Box> boxes;
                    for (const auto& c : cards)
                        boxes.push_back(mode == Mode::Row
                            ? Box{c.inset, c.offset, 800 - 2*c.inset, c.span}
                            : Box{c.offset, c.inset, c.span, 800 - 2*c.inset});
                    const Box viewport = mode == Mode::Row ? Box{0,0,800,1000} : Box{0,0,1000,800};
                    double background = 0;
                    for (size_t i = 0; i < cards.size(); ++i) {
                        const auto& card = cards[i];
                        const auto hit = deck_hit_box(boxes, i, card.index < active, mode, viewport);
                        const auto exposed = mode == Mode::Row ? hit.h : hit.w;
                        if (card.index == active) {
                            expect_near(exposed, 900, 1e-8, "fullsize main occupies 90 percent");
                            expect_true(!card.narrow, "fullsize main has no strip label");
                        } else {
                            background += exposed;
                            const auto expected = count == 2 ? 100.0 : card.index == previous ? 50.0 : 50.0/(count-2);
                            expect_near(exposed, expected, 1e-8, "fullsize background keeps assigned visible share");
                            expect_true(card.narrow, "all fullsize background windows have labels");
                        }
                        expect_near(card.exposedSpan, exposed, 1e-8, "label extent matches actual visible strip");
                        expect_true(card.offset >= 0 && card.offset + card.span <= 1000 + 1e-8,
                                    "fullsize cards remain inside viewport");
                    }
                    expect_near(background, 100, 1e-8, "fullsize backgrounds together occupy 10 percent");
                }
            }
        }
    }
}

void test_stack_visible_hit_boxes() {
    using CanvasCore::Box;
    using CanvasModel::StackLogic::deck_hit_box;
    using CanvasModel::StackLogic::deck_cards;
    using CanvasModel::StackLogic::deck_peek;
    expect_near(deck_peek(4, 900), 85.95, 1e-9, "two narrow strips share 19.1 percent");
    expect_near(deck_peek(100, 900) * 98, 171.9, 1e-9, "large decks preserve total strip share");
    for (const auto mode : {Mode::Row, Mode::Column}) {
        for (size_t active = 0; active < 4; ++active) {
            const auto cards = deck_cards(4, active, 900, 600);
            const Box viewport = mode == Mode::Row ? Box{0, 0, 600, 900} : Box{0, 0, 900, 600};
            std::vector<Box> boxes;
            for (const auto& card : cards)
                boxes.push_back(mode == Mode::Row
                    ? Box{card.inset, card.offset, 600 - 2 * card.inset, card.span}
                    : Box{card.offset, card.inset, card.span, 600 - 2 * card.inset});
            for (size_t i = 0; i < cards.size(); ++i) {
                const auto hit = deck_hit_box(boxes, i, cards[i].index < active, mode, viewport);
                expect_true(hit.w > 0 && hit.h > 0, "every exposed card remains targetable");
                for (size_t j = i + 1; j < boxes.size(); ++j) {
                    const auto& front = boxes[j];
                    const auto overlapW = std::min(hit.x + hit.w, front.x + front.w) - std::max(hit.x, front.x);
                    const auto overlapH = std::min(hit.y + hit.h, front.y + front.h) - std::max(hit.y, front.y);
                    expect_true(overlapW <= 1e-9 || overlapH <= 1e-9, "covered area never belongs to background hitbox");
                }
                if (cards[i].narrow)
                    expect_near(mode == Mode::Row ? hit.h : hit.w, 85.95, 1e-9, "whole narrow strip is targetable");
            }
        }
    }
    std::vector<Box> moving{{0, 0, 600, 600}, {0, 60, 600, 600}};
    expect_near(deck_hit_box(moving, 0, true, Mode::Row, {0,0,600,900}).h, 60, 1e-9,
                "hit area follows current rendered occluder position");
    moving.back().y = 30;
    expect_near(deck_hit_box(moving, 0, true, Mode::Row, {0,0,600,900}).h, 30, 1e-9,
                "animation updates the exposed hit strip");
    moving.back().y = 0;
    expect_near(deck_hit_box(moving, 0, true, Mode::Row, {0,0,600,900}).h, 0, 1e-9,
                "fully covered window has no hit area");
    expect_near(deck_hit_box(moving, 1, false, Mode::Row, {0,100,600,300}).h, 300, 1e-9,
                "hit regions stay inside the visible work area");
}

void test_service_slot() {
    struct CounterService {
        int value = 7;
    };

    CanvasCore::ServiceSlot<CounterService> slot;
    expect_true(!slot.hasOwnedInstance(), "service slot starts lazily uninitialized");
    slot.get().value = 42;
    expect_true(slot.hasOwnedInstance(), "service slot constructs owned instance on first access");
    expect_eq(slot.get().value, 42, "service slot retains state across reads");

    {
        CounterService injected{.value = 100};
        auto guard = slot.overrideWith(injected);
        expect_eq(slot.get().value, 100, "scoped override redirects access to injected instance");
        slot.get().value = 101;
        expect_eq(injected.value, 101, "mutations through slot hit the injected instance");

        CounterService nested{.value = 200};
        {
            auto nestedGuard = slot.overrideWith(nested);
            expect_eq(slot.get().value, 200, "nested override shadows outer override");
        }
        expect_eq(slot.get().value, 101, "destroying nested override restores outer override");
    }

    expect_eq(slot.get().value, 42, "destroying scoped override restores default owned instance");
    slot.reset();
    expect_true(!slot.hasOwnedInstance(), "reset destroys the owned instance cleanly");
    expect_eq(slot.get().value, 7, "subsequent access after reset starts from fresh default state");
}

} // namespace

void run_core_logic_tests() {
    test_canvas_transition();
    test_interval();
    test_direction_helpers();
    test_parse_helpers();
    test_anchor_selection();
    test_layout_profile();
    test_monitor_geometry();
    test_workarea_pager();
    test_owner_index();
    test_span_scale_helper();
    test_stack_width_visible_count();
    test_list_move_only();
    test_stack_logic();
    test_stack_deck();
    test_fullsize_deck();
    test_stack_visible_hit_boxes();
    test_service_slot();
}
