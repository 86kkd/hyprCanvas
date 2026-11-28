/**
 * @file overview/scene/layout.h
 * @brief Pure projection helpers for overview scene construction.
 */
#pragma once

#include <span>
#include <vector>

#include "core/types.h"

namespace Overview {

KartaCore::Box              localizeGlobalBox(const KartaCore::Box& box, double originX, double originY);
KartaCore::Box              buildWorkspaceContentBox(const KartaCore::Box& workspaceBox);
std::vector<KartaCore::Box> projectBoxesToContent(std::span<const KartaCore::Box> sourceBoxes,
                                                     const KartaCore::Box& contentBox);
std::vector<KartaCore::Box> projectGlobalBoxesToContent(std::span<const KartaCore::Box> sourceBoxes,
                                                           const KartaCore::Box& contentBox,
                                                           double originX,
                                                           double originY);
KartaCore::Box              buildEmptyWorkspacePreviewBox(const KartaCore::Box& contentBox);

} // namespace Overview
