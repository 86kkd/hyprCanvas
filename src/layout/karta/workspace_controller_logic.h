/**
 * @file layout/karta/workspace_controller_logic.h
 * @brief Pure karta-grid navigation planning shared by desktop and overview.
 */
#pragma once

#include <vector>

#include "core/direction.h"

namespace KartaLayoutState {

struct KartaTileNode {
    int  kartaId = -1;
    int  tileX = 0;
    int  tileY = 0;
    bool ephemeral = false;
    bool synthetic = false;
};

enum class KartaNavigationAction {
    None,
    SelectExisting,
    CreateSynthetic,
};

struct KartaNavigationPlan {
    KartaNavigationAction action = KartaNavigationAction::None;
    int                    targetKartaId = -1;
    int                    anchorKartaId = -1;
    Direction              createDirection = Direction::Right;
};

// Plan moving from `currentKartaId` in a cardinal direction across the karta
// tile grid, respecting the one-empty-page boundary when creating at an edge.
KartaNavigationPlan planDirectionalKartaMove(const std::vector<KartaTileNode>& kartas,
                                               int currentKartaId,
                                               Direction direction);

// Plan selecting the 1-based karta number (1..10) in stable creation order.
// Allows extending by one synthetic page immediately past the last karta when
// neither the current karta nor the last karta is ephemeral.
KartaNavigationPlan planNumberedKartaMove(const std::vector<KartaTileNode>& kartas,
                                            int currentKartaId,
                                            int number);

} // namespace KartaLayoutState
