#pragma once

#include "../lane/lane.h"
#include "route_logic.h"

namespace CanvasLayoutInternal {

bool should_sync_workspace_focus_before_move(ListNode<Lane *> *activeLaneNode);
PHLMONITOR resolve_monitor_in_direction(PHLMONITOR sourceMonitor, Direction direction);

} // namespace CanvasLayoutInternal
