/** @file runtime.h
 * @brief Session-owned HUD lifetime and read-only repository/model adaptation.
 */
#pragma once
#include <hyprland/src/output/Monitor.hpp>
#include "logic.h"
#include "overview/render/animation.h"
namespace Overview::Minimap {
void notifySwitch(int fromKarta, int toKarta);
void reset();
bool visible(PHLMONITOR monitor, steady_tp now);
double alpha(steady_tp now);
Map mapForMonitor(PHLMONITOR monitor);
void click(double globalX, double globalY);
} // namespace Overview::Minimap
