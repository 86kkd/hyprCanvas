/**
 * @file layout/canvas/workspace_controller_logic.h
 * @brief Pure canvas-grid navigation planning shared by desktop and overview.
 */
#pragma once

#include <vector>

#include "core/direction.h"

namespace CanvasLayoutState {

struct CanvasTileNode {
    int  canvasId = -1;
    int  tileX = 0;
    int  tileY = 0;
    bool ephemeral = false;
    bool synthetic = false;
};

enum class CanvasNavigationAction {
    None,
    SelectExisting,
    CreateSynthetic,
};

struct CanvasNavigationPlan {
    CanvasNavigationAction action = CanvasNavigationAction::None;
    int                    targetCanvasId = -1;
    int                    anchorCanvasId = -1;
    Direction              createDirection = Direction::Right;
};

// Plan moving from `currentCanvasId` in a cardinal direction across the canvas
// tile grid, respecting the one-empty-page boundary when creating at an edge.
CanvasNavigationPlan planDirectionalCanvasMove(const std::vector<CanvasTileNode>& canvases,
                                               int currentCanvasId,
                                               Direction direction);

// Plan selecting the 1-based canvas number (1..10) in stable creation order.
// Allows extending by one synthetic page immediately past the last canvas when
// neither the current canvas nor the last canvas is ephemeral.
CanvasNavigationPlan planNumberedCanvasMove(const std::vector<CanvasTileNode>& canvases,
                                            int currentCanvasId,
                                            int number);

} // namespace CanvasLayoutState
