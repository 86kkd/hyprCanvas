#include <algorithm>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "core/direction.h"
#include "core/fit_size.h"
#include "core/interval.h"
#include "core/layout_math.h"
#include "core/layout_profile.h"
#include "core/monitor_geometry.h"
#include "core/owner_index.h"
#include "core/intrusive_list.h"
#include "core/workarea_pager.h"
#include "model/stack/logic.h"

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

struct FakeFitItem {
    double span = 0.0;
    bool   visible = false;
};

void expect_reserved_workarea(std::string_view label,
                              const Hyprutils::Math::Vector2D &position,
                              const Hyprutils::Math::Vector2D &rawSize,
                              const Hyprutils::Math::Vector2D &transformedSize,
                              wl_output_transform transform,
                              const ScrollerCore::ReservedEdges &rawReserved,
                              const ScrollerCore::ReservedEdges &expectedReserved,
                              const ScrollerCore::Box &expectedWorkarea) {
    const auto logicalReserved = ScrollerCore::logical_reserved_edges(rawSize, transformedSize, transform, rawReserved);
    expect_near(logicalReserved.top, expectedReserved.top, 1e-9, std::string(label) + " reserved top");
    expect_near(logicalReserved.right, expectedReserved.right, 1e-9, std::string(label) + " reserved right");
    expect_near(logicalReserved.bottom, expectedReserved.bottom, 1e-9, std::string(label) + " reserved bottom");
    expect_near(logicalReserved.left, expectedReserved.left, 1e-9, std::string(label) + " reserved left");

    const auto workarea = ScrollerCore::logical_workarea_box(position, rawSize, transformedSize, transform, rawReserved, 0.0);
    expect_near(workarea.x, expectedWorkarea.x, 1e-9, std::string(label) + " workarea x");
    expect_near(workarea.y, expectedWorkarea.y, 1e-9, std::string(label) + " workarea y");
    expect_near(workarea.w, expectedWorkarea.w, 1e-9, std::string(label) + " workarea width");
    expect_near(workarea.h, expectedWorkarea.h, 1e-9, std::string(label) + " workarea height");
}

void test_interval() {
    expect_true(ScrollerCore::Interval::intersects(0.0, 10.0, 5.0, 15.0), "interval partial overlap intersects");
    expect_true(ScrollerCore::Interval::intersects(0.0, 20.0, 5.0, 15.0), "interval containing viewport intersects");
    expect_true(!ScrollerCore::Interval::intersects(0.0, 5.0, 5.0, 15.0), "touching edge does not intersect");
    expect_true(ScrollerCore::Interval::fully_visible(6.0, 9.0, 5.0, 15.0), "fully visible interval reports true");
    expect_true(!ScrollerCore::Interval::fully_visible(4.0, 9.0, 5.0, 15.0), "partially clipped interval reports false");
}

void test_direction_helpers() {
    expect_eq(std::string_view(ScrollerCore::direction_name(Direction::Begin)), std::string_view("begin"),
              "direction_name returns begin");
    expect_eq(std::string_view(ScrollerCore::direction_dispatch_arg(Direction::Right)), std::string_view("r"),
              "direction_dispatch_arg returns short right");
    expect_true(ScrollerCore::direction_dispatch_arg(Direction::Center) == nullptr,
                "direction_dispatch_arg rejects center");
    expect_eq(ScrollerCore::opposite_direction(Direction::Up), Direction::Down,
              "opposite_direction flips up to down");
}

void test_parse_helpers() {
    const auto down = ScrollerCore::parse_direction_arg("dn");
    expect_true(down.has_value() && *down == Direction::Down, "parse_direction_arg handles dn alias");

    const auto center = ScrollerCore::parse_direction_arg("centre");
    expect_true(center.has_value() && *center == Direction::Center, "parse_direction_arg handles centre alias");

    expect_true(!ScrollerCore::parse_direction_arg("sideways").has_value(),
                "parse_direction_arg rejects invalid input");

    const auto toBeginning = ScrollerCore::parse_fit_size_arg("tobeginning");
    expect_true(toBeginning.has_value() && *toBeginning == FitSize::ToBeg,
                "parse_fit_size_arg handles tobeginning");

    expect_true(!ScrollerCore::parse_fit_size_arg("largest").has_value(),
                "parse_fit_size_arg rejects invalid input");

    const auto rowMode = ScrollerCore::parse_mode_arg("row");
    expect_true(rowMode.has_value() && *rowMode == Mode::Row,
                "parse_mode_arg handles row");

    const auto columnMode = ScrollerCore::parse_mode_arg("col");
    expect_true(columnMode.has_value() && *columnMode == Mode::Column,
                "parse_mode_arg handles col alias");

    expect_true(!ScrollerCore::parse_mode_arg("grid").has_value(),
                "parse_mode_arg rejects invalid input");
}

void test_anchor_selection() {
    const ScrollerCore::Box visible(100.0, 50.0, 400.0, 300.0);

    expect_near(ScrollerCore::choose_anchor_x(true, false, 150.0, 120.0, 0.0, 260.0, visible),
                230.0, 1e-9, "choose_anchor_x keeps active and next visible");
    expect_near(ScrollerCore::choose_anchor_x(true, true, 200.0, 250.0, 150.0, 275.0, visible),
                250.0, 1e-9, "choose_anchor_x falls back to prev width when next does not fit");
    expect_near(ScrollerCore::choose_anchor_x(false, true, 350.0, 0.0, 100.0, 275.0, visible),
                150.0, 1e-9, "choose_anchor_x aligns active to right edge when only prev exists");

    expect_near(ScrollerCore::choose_anchor_y(true, false, 120.0, 100.0, 0.0, visible),
                130.0, 1e-9, "choose_anchor_y keeps next visible when possible");
    expect_near(ScrollerCore::choose_anchor_y(false, true, 180.0, 0.0, 100.0, visible),
                150.0, 1e-9, "choose_anchor_y positions after prev when it fits");
    expect_near(ScrollerCore::choose_anchor_y(false, true, 250.0, 0.0, 100.0, visible),
                100.0, 1e-9, "choose_anchor_y aligns to bottom when only prev exists but cannot fit");

    expect_near(ScrollerCore::center_span(100.0, 400.0, 150.0),
                225.0, 1e-9, "center_span preserves the outer origin when centering");
    expect_near(ScrollerCore::center_span(-320.0, 640.0, 320.0),
                -160.0, 1e-9, "center_span handles non-zero negative origins");

    const auto rendered = ScrollerCore::rendered_local_interval(-1855.0, 1908.0, 4.0, 0.0, 16.0);
    expect_near(rendered.start, -1851.0, 1e-9, "rendered_local_interval applies border and leading gap to start");
    expect_near(rendered.end, 33.0, 1e-9, "rendered_local_interval matches final client bottom after border and trailing gap");
}

void test_layout_profile() {
    expect_eq(ScrollerCore::layout_orientation_for_extent(1920.0, 1080.0),
              ScrollerCore::LayoutOrientation::Landscape,
              "wide extents map to landscape");
    expect_eq(ScrollerCore::layout_orientation_for_extent(1080.0, 1920.0),
              ScrollerCore::LayoutOrientation::Portrait,
              "tall extents map to portrait");
    expect_eq(ScrollerCore::default_mode_for_extent(1920.0, 1080.0),
              Mode::Row,
              "landscape extents default to row mode");
    expect_eq(ScrollerCore::default_mode_for_extent(1080.0, 1920.0),
              Mode::Column,
              "portrait extents default to column mode");

    expect_true(ScrollerCore::mode_pages_lanes_vertically(Mode::Row),
                "row mode pages lanes vertically");

    expect_eq(ScrollerCore::local_item_backward_direction(Mode::Row),
              Direction::Left,
              "row mode local backward direction is left");
    expect_eq(ScrollerCore::local_item_forward_direction(Mode::Column),
              Direction::Down,
              "column mode local forward direction is down");
    expect_eq(ScrollerCore::stack_item_backward_direction(Mode::Row),
              Direction::Up,
              "row mode stack backward direction is up");
    expect_eq(ScrollerCore::stack_item_forward_direction(Mode::Column),
              Direction::Right,
              "column mode stack forward direction is right");
    expect_eq(ScrollerCore::lane_backward_direction(Mode::Row),
              Direction::Up,
              "row mode lane backward direction is up");
    expect_eq(ScrollerCore::lane_forward_direction(Mode::Column),
              Direction::Right,
              "column mode lane forward direction is right");

    expect_near(ScrollerCore::stack_local_origin_for_window(Mode::Row, {10.0, 20.0}),
                20.0, 1e-9, "row mode stack-local window origin uses y");
    expect_near(ScrollerCore::stack_local_origin_for_window(Mode::Column, {10.0, 20.0}),
                10.0, 1e-9, "column mode stack-local window origin uses x");
    expect_near(ScrollerCore::stack_primary_span_limit(Mode::Row, {100.0, 50.0, 640.0, 360.0}),
                640.0, 1e-9, "row mode stack primary span limit uses workarea width");
    expect_near(ScrollerCore::stack_primary_span_limit(Mode::Column, {100.0, 50.0, 640.0, 360.0}),
                360.0, 1e-9, "column mode stack primary span limit uses workarea height");

    expect_true(ScrollerCore::direction_targets_local_item(Mode::Row, Direction::Left),
                "row mode treats left as local item movement");
    expect_true(ScrollerCore::direction_moves_between_lanes(Mode::Row, Direction::Down),
                "row mode treats down as cross-lane movement");
    expect_true(ScrollerCore::direction_moves_between_lanes(Mode::Column, Direction::Left),
                "column mode treats left as cross-lane movement");
    expect_true(ScrollerCore::direction_inserts_before_current(Mode::Column, Direction::Left),
                "column mode inserts before current on left");

    const auto portraitPrediction = ScrollerCore::predict_window_size(Mode::Column, {0.0, 0.0, 800.0, 600.0});
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

    const auto portraitSize = ScrollerCore::logical_monitor_size({3840.0, 2160.0},
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

    const auto fallbackSize = ScrollerCore::logical_monitor_size({3840.0, 2160.0},
                                                                 {0.0, 0.0},
                                                                 WL_OUTPUT_TRANSFORM_270);
    expect_near(fallbackSize.x, 2160.0, 1e-9, "missing transformed size still swaps portrait axes");
    expect_near(fallbackSize.y, 3840.0, 1e-9, "missing transformed size still preserves portrait height");
}

void test_workarea_pager() {
    const ScrollerCore::Box full{0.0, 0.0, 1080.0, 1920.0};
    const ScrollerCore::Box workarea{0.0, 40.0, 1080.0, 1880.0};
    const auto above = ScrollerCore::project_box_to_workarea_page({0.0, -900.0, 1080.0, 940.0}, full, workarea);
    expect_near(above.committed.y, -940.0, 1e-9, "workarea pager keeps top reserved edge clear");
    expect_true(!above.visible, "workarea pager marks fully hidden top box invisible");

    const ScrollerCore::Box fullWithLeftBar{0.0, 0.0, 1920.0, 1080.0};
    const ScrollerCore::Box workareaWithLeftBar{48.0, 0.0, 1872.0, 1080.0};
    const auto left = ScrollerCore::project_box_to_workarea_page({-888.0, 0.0, 936.0, 1080.0}, fullWithLeftBar, workareaWithLeftBar);
    expect_near(left.committed.x, -936.0, 1e-9, "workarea pager keeps left reserved edge clear");
    expect_true(!left.visible, "workarea pager marks fully hidden left box invisible");
}

void test_owner_index() {
    ScrollerCore::OwnerIndex<int, FakeOwner> index;
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

void test_fit_size_helpers() {
    FakeFitItem itemA{.span = 10.0, .visible = false};
    FakeFitItem itemB{.span = 20.0, .visible = true};
    FakeFitItem itemC{.span = 30.0, .visible = true};
    FakeFitItem itemD{.span = 40.0, .visible = false};
    List<FakeFitItem *> items;
    items.push_back(&itemA);
    items.push_back(&itemB);
    items.push_back(&itemC);
    items.push_back(&itemD);

    auto *active = items.first()->next()->next();
    const auto isVisible = [](ListNode<FakeFitItem *> *node) {
        return node && node->data() && node->data()->visible;
    };

    const auto [activeFrom, activeTo] = ScrollerCore::select_fit_size_range(
        FitSize::Active, items.first(), items.last(), active, isVisible);
    expect_true(activeFrom == active && activeTo == active,
                "fit-size helper selects only the active node for active mode");

    const auto [visibleFrom, visibleTo] = ScrollerCore::select_fit_size_range(
        FitSize::Visible, items.first(), items.last(), active, isVisible);
    expect_true(visibleFrom == items.first()->next(),
                "fit-size helper finds the first visible node");
    expect_true(visibleTo == items.last()->prev(),
                "fit-size helper finds the last visible node");

    const auto [toBegFrom, toBegTo] = ScrollerCore::select_fit_size_range(
        FitSize::ToBeg, items.first(), items.last(), active, isVisible);
    expect_true(toBegFrom == items.first() && toBegTo == active,
                "fit-size helper keeps the head-to-active range");

    const auto normalized = ScrollerCore::normalize_fit_size_range(
        visibleFrom,
        visibleTo,
        100.0,
        [](ListNode<FakeFitItem *> *node) {
            return node->data()->span;
        },
        [](ListNode<FakeFitItem *> *node, double scaledSpan) {
            node->data()->span = scaledSpan;
        });
    expect_true(normalized, "fit-size helper normalizes non-empty ranges");
    expect_near(itemB.span, 40.0, 1e-9,
                "fit-size helper preserves proportional span for the first visible node");
    expect_near(itemC.span, 60.0, 1e-9,
                "fit-size helper preserves proportional span for the second visible node");

    FakeFitItem zeroA{.span = 0.0, .visible = true};
    FakeFitItem zeroB{.span = 0.0, .visible = true};
    List<FakeFitItem *> zeros;
    zeros.push_back(&zeroA);
    zeros.push_back(&zeroB);
    expect_true(!ScrollerCore::normalize_fit_size_range(
                    zeros.first(),
                    zeros.last(),
                    80.0,
                    [](ListNode<FakeFitItem *> *node) {
                        return node->data()->span;
                    },
                    [](ListNode<FakeFitItem *> *node, double scaledSpan) {
                        node->data()->span = scaledSpan;
                    }),
                "fit-size helper rejects zero-total ranges");
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
}

void test_stack_logic() {
    List<int> values;
    values.push_back(1);
    values.push_back(2);
    values.push_back(3);

    auto *first = values.first();
    auto *second = first->next();
    auto *last = values.last();
    expect_true(ScrollerModel::StackLogic::next_active_after_removal(first, last) == second,
                "stack logic prefers the next node when removing a non-tail active item");
    expect_true(ScrollerModel::StackLogic::next_active_after_removal(last, last) == second,
                "stack logic falls back to the previous node when removing the tail active item");

    List<int> single;
    single.push_back(42);
    expect_true(ScrollerModel::StackLogic::next_active_after_removal(single.first(), single.last()) == nullptr,
                "stack logic clears active selection when removing the only node");
}

void test_stack_deck() {
    using ScrollerModel::StackLogic::deck_cards;
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
    const auto tiny = deck_cards(20, 7, 30.0, 20.0);
    expect_true(tiny.back().span > 0.0, "small viewports keep positive card spans");
}

void test_fullsize_deck() {
    using namespace ScrollerModel::StackLogic;
    using ScrollerCore::Box;
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
    using ScrollerCore::Box;
    using ScrollerModel::StackLogic::deck_hit_box;
    using ScrollerModel::StackLogic::deck_cards;
    using ScrollerModel::StackLogic::deck_peek;
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

} // namespace

void run_core_logic_tests() {
    test_interval();
    test_direction_helpers();
    test_parse_helpers();
    test_anchor_selection();
    test_layout_profile();
    test_monitor_geometry();
    test_workarea_pager();
    test_owner_index();
    test_fit_size_helpers();
    test_list_move_only();
    test_stack_logic();
    test_stack_deck();
    test_fullsize_deck();
    test_stack_visible_hit_boxes();
}
