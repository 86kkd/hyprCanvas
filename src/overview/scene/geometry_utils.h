/**
 * @file geometry_utils.h
 * @brief Shared box math helpers used across overview model, scene, and render code.
 */
#pragma once

#include <algorithm>
#include <cmath>
#include <optional>

#include "core/types.h"

namespace Overview {

inline bool approximatelyEqual(double a, double b, double epsilon = 0.5) {
    return std::abs(a - b) <= epsilon;
}

inline bool boxesMatch(const CanvasCore::Box& a, const CanvasCore::Box& b, double epsilon = 0.5) {
    return approximatelyEqual(a.x, b.x, epsilon) && approximatelyEqual(a.y, b.y, epsilon)
        && approximatelyEqual(a.w, b.w, epsilon) && approximatelyEqual(a.h, b.h, epsilon);
}

inline bool finiteBox(const CanvasCore::Box& box) {
    return std::isfinite(box.x) && std::isfinite(box.y) && std::isfinite(box.w) && std::isfinite(box.h);
}

inline CanvasCore::Box translateBox(const CanvasCore::Box& box, double dx, double dy) {
    return {
        box.x + dx,
        box.y + dy,
        box.w,
        box.h,
    };
}

inline CanvasCore::Box insetBox(const CanvasCore::Box& box, double insetX, double insetY, double minimumSize = 1.0) {
    return {
        box.x + insetX,
        box.y + insetY,
        std::max(minimumSize, box.w - insetX * 2.0),
        std::max(minimumSize, box.h - insetY * 2.0),
    };
}

inline CanvasCore::Box centerScaleBox(const CanvasCore::Box& box, double scale) {
    const auto scaledWidth = box.w * scale;
    const auto scaledHeight = box.h * scale;
    return {
        box.x + (box.w - scaledWidth) * 0.5,
        box.y + (box.h - scaledHeight) * 0.5,
        scaledWidth,
        scaledHeight,
    };
}

inline CanvasCore::Box unionBox(const CanvasCore::Box& a, const CanvasCore::Box& b) {
    const auto x = std::min(a.x, b.x);
    const auto y = std::min(a.y, b.y);
    return {x, y, std::max(a.x + a.w, b.x + b.w) - x,
            std::max(a.y + a.h, b.y + b.h) - y};
}

inline std::optional<CanvasCore::Box> intersectBox(const CanvasCore::Box& box, const CanvasCore::Box& bounds) {
    if (!finiteBox(box) || !finiteBox(bounds))
        return std::nullopt;

    const auto x0 = std::max(box.x, bounds.x);
    const auto y0 = std::max(box.y, bounds.y);
    const auto x1 = std::min(box.x + box.w, bounds.x + bounds.w);
    const auto y1 = std::min(box.y + box.h, bounds.y + bounds.h);
    if (x1 <= x0 || y1 <= y0)
        return std::nullopt;

    return CanvasCore::Box{x0, y0, x1 - x0, y1 - y0};
}

} // namespace Overview
