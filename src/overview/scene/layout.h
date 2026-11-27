/**
 * @file overview/scene/layout.h
 * @brief Pure projection helpers for overview scene construction.
 */
#pragma once

#include <span>
#include <vector>

#include "core/types.h"

namespace Overview {

CanvasCore::Box              localizeGlobalBox(const CanvasCore::Box& box, double originX, double originY);
CanvasCore::Box              buildWorkspaceContentBox(const CanvasCore::Box& workspaceBox);
std::vector<CanvasCore::Box> projectBoxesToContent(std::span<const CanvasCore::Box> sourceBoxes,
                                                     const CanvasCore::Box& contentBox);
std::vector<CanvasCore::Box> projectGlobalBoxesToContent(std::span<const CanvasCore::Box> sourceBoxes,
                                                           const CanvasCore::Box& contentBox,
                                                           double originX,
                                                           double originY);
CanvasCore::Box              buildEmptyWorkspacePreviewBox(const CanvasCore::Box& contentBox);

} // namespace Overview
