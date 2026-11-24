/** @file logic.h
 * @brief Read-only canvas map geometry; independent of compositor state.
 */
#pragma once

#include <optional>
#include <vector>
#include "core/types.h"

namespace Overview::Minimap {
struct Canvas {
    int id = -1;
    int x = 0;
    int y = 0;
    bool temporary = false;
    bool synthetic = false;
};
struct Cell {
    Canvas canvas;
    int number = 0; // Creation order, matching numbered canvas navigation.
    ScrollerCore::Box box;
    bool current = false;
    bool selected = false;
    bool create = false;
};
struct Map {
    ScrollerCore::Box panel;
    std::vector<Cell> cells;
    bool left = false, right = false, up = false, down = false;
};
Map layout(std::vector<Canvas> canvases, int current, int selected, const ScrollerCore::Box& workarea);
std::optional<Cell> hit(const Map& map, double x, double y);
// Overview is persistent; the desktop HUD holds for 1.3s then fades for 0.2s.
double opacity(bool overview, double elapsedMs);
} // namespace Overview::Minimap
