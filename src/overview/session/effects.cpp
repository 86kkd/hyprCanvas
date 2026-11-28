/**
 * @file overview/session/effects.cpp
 * @brief Runtime-facing helpers that apply overview session decisions to Hyprland state.
 *
 * `logic.cpp` decides what the overview session wants to do. This file is the
 * imperative layer that turns those decisions into workspace switches, monitor
 * focus changes, and window selection in the live compositor state.
 */
#include "overview/session/effects.h"

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/workspace/HLWorkspace.hpp>

#include "core/hyprland_runtime.h"
#include "layout/karta/internal.h"
#include "overview/session/effects_runtime.h"

namespace Overview::SessionEffects {
namespace {

constexpr MONITORID INVALID_MONITOR_ID = static_cast<MONITORID>(-1);

class HyprlandSessionEffectsRuntime final : public Runtime {
  public:
    WORKSPACEID currentWorkspaceId() const override {
        return KartaLayoutInternal::get_workspace_id();
    }

    PHLWORKSPACE getWorkspaceByID(WORKSPACEID workspaceId) const override {
        return g_pCompositor ? KartaCore::HyprlandRuntime::workspaceById(workspaceId) : nullptr;
    }

    std::vector<PHLWORKSPACE> getWorkspaces() const override {
        std::vector<PHLWORKSPACE> workspaces;
        if (!g_pCompositor)
            return workspaces;

        for (const auto& workspaceRef : KartaCore::HyprlandRuntime::workspaces()) {
            const auto workspace = workspaceRef.lock();
            if (workspace)
                workspaces.push_back(workspace);
        }

        return workspaces;
    }

    PHLMONITOR getMonitorFromID(MONITORID monitorId) const override {
        return g_pCompositor ? KartaCore::HyprlandRuntime::monitorById(monitorId) : nullptr;
    }

    PHLMONITOR getFocusedMonitor() const override {
        return g_pCompositor ? KartaCore::HyprlandRuntime::monitorForKeyboardAction() : nullptr;
    }

    MONITORID monitorId(PHLMONITOR monitor) const override {
        return monitor ? monitor->m_id : INVALID_MONITOR_ID;
    }

    WORKSPACEID workspaceId(PHLWORKSPACE workspace) const override {
        return workspace ? KartaCore::HyprlandRuntime::workspaceId(workspace) : WORKSPACE_INVALID;
    }

    MONITORID workspaceMonitorId(PHLWORKSPACE workspace) const override {
        return workspace ? workspace->monitorID() : INVALID_MONITOR_ID;
    }

    bool isWorkspaceSpecial(PHLWORKSPACE workspace) const override {
        return workspace && KartaCore::HyprlandRuntime::isSpecialWorkspace(workspace);
    }

    PHLWINDOW workspaceLastFocusedWindow(PHLWORKSPACE workspace) const override {
        return workspace ? workspace->getLastFocusedWindow() : nullptr;
    }

    MONITORID windowMonitorId(PHLWINDOW window) const override {
        return window ? window->monitorID() : INVALID_MONITOR_ID;
    }

    bool isWindowMapped(PHLWINDOW window) const override {
        return window && window->mapped();
    }

    PHLMONITOR visibleMonitorForWorkspace(PHLWORKSPACE workspace) const override {
        return KartaLayoutInternal::visible_monitor_for_workspace(workspace);
    }

    void syncKartaTargetWindow(PHLWORKSPACE workspace, PHLWINDOW window, MONITORID monitorId) const override {
        if (!workspace || !window)
            return;

        auto* layout = KartaLayoutInternal::get_karta_for_workspace(KartaCore::HyprlandRuntime::workspaceId(workspace));
        if (!layout)
            return;

        layout->onWindowFocusChange(window);
        layout->recalculateMonitor(monitorId);
    }

    void prepareWorkspaceSnapshot(PHLWORKSPACE workspace) const override {
        if (!workspace)
            return;

        auto* layout = KartaLayoutInternal::get_karta_for_workspace(KartaCore::HyprlandRuntime::workspaceId(workspace));
        if (!layout)
            return;

        layout->prepareForOverviewSnapshot();
    }

    bool focusMonitorWorkspace(PHLMONITOR monitor, PHLWORKSPACE workspace, WORKSPACEID fallbackWorkspaceId,
                               bool requireMonitorFocus, const char* context) const override {
        return KartaLayoutInternal::focus_monitor_workspace(monitor, workspace, fallbackWorkspaceId, requireMonitorFocus, context);
    }

    bool switchToWindow(PHLWINDOW window, bool warpCursor) const override {
        return KartaLayoutInternal::switch_to_window(window, warpCursor);
    }
};

Runtime& runtime() {
    static HyprlandSessionEffectsRuntime instance;
    return instance;
}

} // namespace

OriginState captureOrigin() {
    return captureOrigin(runtime());
}

void prepareSnapshots() {
    prepareSnapshots(runtime());
}

WORKSPACEID nextWorkspaceId() {
    return nextWorkspaceId(runtime());
}

bool acceptTarget(const Target& selection) {
    return acceptTarget(runtime(), selection);
}

bool restoreOrigin(const OriginState& origin) {
    return restoreOrigin(runtime(), origin);
}

} // namespace Overview::SessionEffects
