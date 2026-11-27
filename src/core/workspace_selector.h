/**
 * @file workspace_selector.h
 * @brief Shared conversion from workspace objects to Hyprland dispatcher args.
 */
#pragma once

#include <string>

#include "hyprland_runtime.h"

namespace CanvasCore {

inline std::string workspace_selector(PHLWORKSPACE workspace) {
    if (!workspace)
        return {};

    if (!workspace->displayName().empty())
        return workspace->displayName();

    return std::to_string(CanvasCore::HyprlandRuntime::workspaceId(workspace));
}

} // namespace CanvasCore
