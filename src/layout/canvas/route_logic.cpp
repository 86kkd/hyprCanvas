/**
 * @file route_logic.cpp
 * @brief Pure lane-axis helpers and special-workspace restore predicates.
 *
 * Directional neighbor routing itself lives in `topology_route.cpp`.
 */
#include "route_logic.h"

#include "../../core/layout_profile.h"

namespace CanvasLayoutInternal {

bool direction_moves_between_lanes(Mode mode, Direction direction) {
    return ScrollerCore::direction_moves_between_lanes(mode, direction);
}

bool direction_inserts_before_current(Mode mode, Direction direction) {
    return ScrollerCore::direction_inserts_before_current(mode, direction);
}

// Hidden special workspaces can temporarily leave the canvas focused on a blank
// ephemeral lane. These predicates keep that hide/show restoration policy pure
// so focus code only needs to react to a yes/no answer.
bool should_mark_special_ephemeral_lane_for_restore(bool workspaceIsSpecial, bool workspaceVisible, bool activeLaneIsEphemeral, bool activeLaneEmpty) {
    return workspaceIsSpecial && !workspaceVisible && activeLaneIsEphemeral && activeLaneEmpty;
}

bool should_restore_marked_special_ephemeral_lane(bool workspaceIsSpecial, bool workspaceVisible, bool restorePending, bool activeLaneIsEphemeral, bool activeLaneEmpty) {
    return workspaceIsSpecial && workspaceVisible && restorePending && activeLaneIsEphemeral && activeLaneEmpty;
}

} // namespace CanvasLayoutInternal
