/**
 * @file animation.h
 * @brief Shared animation timing and easing helpers for overview rendering.
 */
#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <tuple>

namespace Overview {

// One window can occur in multiple legacy canvas combinations. Its previews
// must never share animation coordinates across canvas tiles or monitors.
using PreviewKey = std::tuple<int, int, std::uintptr_t>;
inline PreviewKey previewKey(int canvasId, int monitorId, std::uintptr_t window) {
    return {canvasId, monitorId, window};
}

using steady_tp = std::chrono::steady_clock::time_point;

inline constexpr auto kOpenDuration = std::chrono::milliseconds(180);
inline constexpr auto kSelectionDuration = std::chrono::milliseconds(120);

inline double clamp01(double value) {
    return std::clamp(value, 0.0, 1.0);
}

inline double easeOutCubic(double t) {
    const auto clamped = clamp01(t);
    const auto inverse = 1.0 - clamped;
    return 1.0 - inverse * inverse * inverse;
}

} // namespace Overview
