/**
 * @file scene.h
 * @brief Render-facing overview scene derived from the logical overview model.
 */
#pragma once

#include <optional>
#include <string>
#include <vector>

#include <hyprland/src/output/Monitor.hpp>

#include "core/types.h"
#include "overview/model/model.h"

namespace Overview {

struct SceneTarget {
    int               kartaId = INVALID_KARTA_ID;
    TargetType        type = TargetType::Window;
    KartaCore::Box box;
    PHLWINDOW         window = nullptr;
    bool              synthetic = false;
    bool              selected = false;
    std::string       label;
};

struct SceneWorkspace {
    KartaCore::Box      box;
    KartaCore::Box      contentBox;
    bool                   selected = false;
    bool                   special = false;
    std::string            label;
    std::vector<SceneTarget> targets;
};

struct SceneMonitor {
    int                      monitorId = INVALID_MONITOR_ID;
    std::string              monitorName;
    KartaCore::Box        box;
    std::vector<SceneWorkspace> workspaces;
    std::optional<SceneTarget> syntheticTarget;
    std::optional<KartaCore::Box> selectionBox;
};

std::optional<SceneMonitor> buildSceneForMonitor(PHLMONITOR monitor, const Model& model);

} // namespace Overview
