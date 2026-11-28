#include "route.h"

#include <hyprland/src/Compositor.hpp>

#include "core/hyprland_runtime.h"
#include "core/layout_profile.h"
#include "internal.h"

namespace KartaLayoutInternal {

bool should_sync_workspace_focus_before_move(const Lane *activeLane) {
    if (!activeLane)
        return true;

    return !(activeLane->is_ephemeral() && activeLane->empty());
}

PHLMONITOR resolve_monitor_in_direction(PHLMONITOR sourceMonitor, Direction direction) {
    const auto monitorDirection = direction_to_math(direction);
    if (!g_pCompositor || !sourceMonitor || !monitorDirection)
        return nullptr;

    return KartaCore::HyprlandRuntime::monitorInDirection(sourceMonitor, *monitorDirection);
}

} // namespace KartaLayoutInternal
