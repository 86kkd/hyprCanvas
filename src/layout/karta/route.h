#pragma once

#include "layout/lane/lane.h"
#include "route_logic.h"

namespace KartaLayoutInternal {

bool should_sync_workspace_focus_before_move(const Lane *activeLane);
PHLMONITOR resolve_monitor_in_direction(PHLMONITOR sourceMonitor, Direction direction);

} // namespace KartaLayoutInternal
