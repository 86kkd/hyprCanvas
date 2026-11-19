#include "route.h"

#include <hyprland/src/Compositor.hpp>

#include "../../core/layout_profile.h"
#include "core/hyprland_runtime.h"
#include "internal.h"

namespace CanvasLayoutInternal {

bool should_sync_workspace_focus_before_move(ListNode<Lane *> *activeLaneNode) {
    if (!activeLaneNode || !activeLaneNode->data())
        return true;

    const auto lane = activeLaneNode->data();
    return !(lane->is_ephemeral() && lane->empty());
}

PHLMONITOR resolve_monitor_in_direction(PHLMONITOR sourceMonitor, Direction direction) {
    const auto monitorDirection = direction_to_math(direction);
    if (!g_pCompositor || !sourceMonitor || !monitorDirection)
        return nullptr;

    return ScrollerCore::HyprlandRuntime::monitorInDirection(sourceMonitor, *monitorDirection);
}

} // namespace CanvasLayoutInternal
