#pragma once

#include "core/types.h"

namespace CanvasLayoutInternal {

bool direction_moves_between_lanes(Mode mode, Direction direction);
bool direction_inserts_before_current(Mode mode, Direction direction);
bool should_mark_special_ephemeral_lane_for_restore(bool workspaceIsSpecial, bool workspaceVisible, bool activeLaneIsEphemeral, bool activeLaneEmpty);
bool should_restore_marked_special_ephemeral_lane(bool workspaceIsSpecial, bool workspaceVisible, bool restorePending, bool activeLaneIsEphemeral, bool activeLaneEmpty);

} // namespace CanvasLayoutInternal
