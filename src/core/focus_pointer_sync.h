#pragma once

#include <hyprland/src/desktop/DesktopTypes.hpp>

namespace KartaCore::FocusPointerSync {
enum class Policy { Preserve, Navigation, NewWindow };
void initialize();
void shutdown();
void request(PHLWINDOW window, Policy policy);
}
