/** @file logic.cpp
 * @brief Project stable canvas coordinates into a bounded local map.
 */
#include "logic.h"
#include "overview/navigation/logic.h"
#include <algorithm>
#include <array>
#include <cstdint>

namespace Overview::Minimap {
Map layout(std::vector<Canvas> canvases, int current, int selected, const CanvasCore::Box& area) {
    Map map;
    if (canvases.empty() || area.w < 180 || area.h < 180)
        return map;
    std::sort(canvases.begin(), canvases.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    auto anchor = std::find_if(canvases.begin(), canvases.end(), [&](const auto& c) { return c.id == selected; });
    if (anchor == canvases.end())
        anchor = std::find_if(canvases.begin(), canvases.end(), [&](const auto& c) { return c.id == current; });
    if (anchor == canvases.end())
        anchor = canvases.begin();
    const Canvas center = *anchor;
    std::vector<Cell> cells;
    cells.reserve(canvases.size() + 4);
    for (size_t i = 0; i < canvases.size(); ++i) {
        const auto& c = canvases[i];
        cells.push_back({.canvas = c, .number = static_cast<int>(i + 1), .box = {},
                         .current = c.id == current, .selected = c.id == selected});
    }
    // Only advertise immediate creation slots. Empty temporary/synthetic pages
    // cannot grow another page, matching the repository's one-empty-page rule.
    std::vector<OverviewLogic::TargetCandidate> candidates;
    candidates.reserve(canvases.size());
    for (const auto& c : canvases)
        candidates.push_back({.monitorId = 0, .box = {double(c.x) * 128, double(c.y) * 128, 96, 96}});
    const size_t centerIndex = static_cast<size_t>(anchor - canvases.begin());
    if (!center.temporary && !center.synthetic) {
        constexpr std::array<std::pair<int, int>, 4> deltas{{{-1, 0}, {1, 0}, {0, -1}, {0, 1}}};
        for (const auto& delta : deltas) {
            const auto direction = delta.first < 0 ? Direction::Left : delta.first > 0 ? Direction::Right :
                                   delta.second < 0 ? Direction::Up : Direction::Down;
            // Reuse overview routing: a direction with an existing destination
            // navigates there, so it must not be advertised as a creation slot.
            if (OverviewLogic::pickTargetIndex(candidates, centerIndex, direction))
                continue;
            const int64_t x = static_cast<int64_t>(center.x) + delta.first;
            const int64_t y = static_cast<int64_t>(center.y) + delta.second;
            if (x < INT32_MIN || x > INT32_MAX || y < INT32_MIN || y > INT32_MAX)
                continue;
            if (std::none_of(canvases.begin(), canvases.end(), [&](const auto& c) { return c.x == x && c.y == y; }))
                cells.push_back({.canvas = {.x = static_cast<int>(x), .y = static_cast<int>(y)}, .box = {}, .create = true});
        }
    }
    int64_t minX = center.x, maxX = center.x, minY = center.y, maxY = center.y;
    for (const auto& cell : cells) {
        minX = std::min(minX, int64_t(cell.canvas.x)); maxX = std::max(maxX, int64_t(cell.canvas.x));
        minY = std::min(minY, int64_t(cell.canvas.y)); maxY = std::max(maxY, int64_t(cell.canvas.y));
    }
    // Keep readable cells instead of shrinking a sparse world into tiny dots.
    if (maxX - minX > 4) { minX = int64_t(center.x) - 2; maxX = int64_t(center.x) + 2; }
    if (maxY - minY > 4) { minY = int64_t(center.y) - 2; maxY = int64_t(center.y) + 2; }
    const double columns = maxX - minX + 1, rows = maxY - minY + 1;
    const double step = std::min({44.0, (area.w - 48.0) / columns, (area.h - 94.0) / rows});
    const double panelWidth = std::max(156.0, columns * step + 24);
    map.panel = {area.x + area.w - panelWidth - 16, area.y + 16,
                 panelWidth, rows * step + 70};
    const double gridInset = (panelWidth - columns * step) * 0.5;
    for (auto cell : cells) {
        if (cell.canvas.x < minX || cell.canvas.x > maxX || cell.canvas.y < minY || cell.canvas.y > maxY) {
            if (!cell.create) {
                map.left |= cell.canvas.x < minX; map.right |= cell.canvas.x > maxX;
                map.up |= cell.canvas.y < minY; map.down |= cell.canvas.y > maxY;
            }
            continue;
        }
        cell.box = {map.panel.x + gridInset + (int64_t(cell.canvas.x) - minX) * step,
                    map.panel.y + 34 + (int64_t(cell.canvas.y) - minY) * step, step - 5, step - 5};
        map.cells.push_back(cell);
    }
    return map;
}
std::optional<Cell> hit(const Map& map, double x, double y) {
    for (const auto& cell : map.cells)
        if (x >= cell.box.x && x < cell.box.x + cell.box.w && y >= cell.box.y && y < cell.box.y + cell.box.h)
            return cell;
    return std::nullopt;
}
double opacity(bool overview, double elapsedMs) {
    if (overview)
        return 1.0;
    if (elapsedMs < 0)
        return 0.0;
    return std::clamp((1500.0 - elapsedMs) / 200.0, 0.0, 1.0);
}
} // namespace Overview::Minimap
