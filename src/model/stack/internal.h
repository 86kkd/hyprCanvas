#pragma once

#include "model/stack/stack.h"

namespace KartaModel::StackInternal {

struct StackWidthPreset {
    StackWidth width = StackWidth::visible(StackWidth::kDefaultVisibleStacks);
    double     maxw = 0.0;
};

double stack_local_origin(const KartaCore::Box &geom, Mode mode);
double stack_local_span(const KartaCore::Box &geom, Mode mode);
double stack_cross_span(const KartaCore::Box &geom, Mode mode);
double stack_primary_span(const KartaCore::Box &geom, Mode mode);
double local_viewport_end(const KartaCore::Box &geom, Mode mode);

Vector2D compose_window_position(const KartaCore::Box &geom, Mode mode, double border,
                                 const Vector2D &cross_gap, double local_pos, double local_gap);
Vector2D compose_window_size(const KartaCore::Box &geom, Mode mode, double border,
                             const Vector2D &cross_gap, double local_size,
                             double local_gap0, double local_gap1);

StackWidthPreset parse_stack_width_preset(PHLWINDOW window, double fallback_maxw);

bool is_window_fully_visible(Window *window, const KartaCore::Box &geom, Mode mode);
bool is_window_intersect_viewport(Window *window, const KartaCore::Box &geom, Mode mode);
void sync_window_target_geometry(PHLWINDOW window);

} // namespace KartaModel::StackInternal
