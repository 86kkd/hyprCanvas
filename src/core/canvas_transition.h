#pragma once

#include <algorithm>
#include <cmath>

#include "types.h"

namespace CanvasCore {

// Canvas coordinates, unlike native workspace IDs, encode spatial direction.
// Normalize in tile space: a distant jump still travels one screen extent,
// while diagonal jumps retain the ratio between their horizontal/vertical legs.
inline Hyprutils::Math::Vector2D canvas_transition_offset(int sourceX, int sourceY, int targetX, int targetY,
                                                         const Hyprutils::Math::Vector2D& monitorSize) {
    const double dx = static_cast<double>(targetX) - sourceX;
    const double dy = static_cast<double>(targetY) - sourceY;
    const double distance = std::max(std::abs(dx), std::abs(dy));
    if (distance == 0.0)
        return {};
    return {dx / distance * monitorSize.x, dy / distance * monitorSize.y};
}

} // namespace CanvasCore
