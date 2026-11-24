#include "overview/minimap/logic.h"
#include "test_support.h"
#include <algorithm>
#include <climits>

void run_minimap_tests() {
    using namespace Overview::Minimap;
    const ScrollerCore::Box area{10, 20, 1000, 700};
    const auto map = layout({{20, 2, 0}, {10, 0, 0}, {30, 2, -1, true}}, 10, 20, area);
    auto find = [&](int id) { return *std::find_if(map.cells.begin(), map.cells.end(), [&](const auto& c) { return c.canvas.id == id; }); };
    const auto first = find(10), second = find(20), third = find(30);
    expect_eq(first.number, 1, "map numbers follow canvas creation order, not input order or native workspace");
    expect_true(first.current && !first.selected && second.selected && !second.current, "current and preview remain distinct");
    expect_near(second.box.x - first.box.x, 88, 1e-9, "sparse map preserves empty coordinate between canvases");
    expect_true(third.box.y < second.box.y && third.canvas.temporary, "negative coordinates and temporary status survive projection");
    expect_true(map.panel.x >= area.x && map.panel.y >= area.y && map.panel.x + map.panel.w <= area.x + area.w && map.panel.y + map.panel.h <= area.y + area.h, "map fits offset workarea");
    expect_eq(hit(map, second.box.x + 2, second.box.y + 2)->canvas.id, 20, "map click uses the rendered cell");
    expect_true(!hit(map, -1, -1), "outside click has no destination");
    const auto temporary = layout({{1, 0, 0, true}}, 1, 1, area);
    expect_eq(temporary.cells.size(), size_t(1), "temporary canvas cannot advertise further creation");
    const auto synthetic = layout({{1, 0, 0}, {2, 1, 0, false, true}}, 1, 2, area);
    expect_eq(synthetic.cells.size(), size_t(2), "uncommitted preview cannot advertise further creation");
    const auto gap = layout({{1, 0, 0}, {2, 3, 0}}, 1, 1, area);
    expect_true(std::none_of(gap.cells.begin(), gap.cells.end(), [](const auto& cell) { return cell.create && cell.canvas.x == 1; }),
                "a gap toward an existing destination is not a new-page action");
    const auto distant = layout({{1, 0, 0}, {2, 100, -100}}, 1, 1, area);
    expect_true(distant.right && distant.up && !distant.left && !distant.down, "off-map canvases indicate their real directions");
    const auto extreme = layout({{1, INT_MIN, INT_MIN}, {2, INT_MAX, INT_MAX}}, 1, 1, area);
    expect_true(extreme.right && extreme.down, "extreme sparse coordinates do not overflow");
    for (const auto& bounds : std::vector<ScrollerCore::Box>{{0, 0, 180, 180}, {0, 0, 600, 1000}}) {
        const auto small = layout({{1, 0, 0}, {2, 2, 2}}, 1, 1, bounds);
        for (const auto& cell : small.cells)
            expect_true(cell.box.x >= 0 && cell.box.y >= 0 && cell.box.x + cell.box.w <= bounds.w && cell.box.y + cell.box.h <= bounds.h, "cells fit small and portrait outputs");
    }
    expect_true(layout({}, 1, 1, area).cells.empty(), "empty repository produces no map");
    expect_near(opacity(false, -1), 0, 1e-9, "HUD hidden before a switch");
    expect_near(opacity(false, 1200), 1, 1e-9, "HUD holds before fading");
    expect_near(opacity(false, 1400), .5, 1e-9, "HUD fades before deadline");
    expect_near(opacity(false, 1500), 0, 1e-9, "HUD expires");
    expect_near(opacity(true, 9000), 1, 1e-9, "overview map stays visible");
}
