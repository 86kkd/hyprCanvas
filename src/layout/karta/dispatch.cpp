/**
 * @file dispatch.cpp
 * @brief Shared Hyprland dispatcher invocation helpers for karta code.
 *
 * This file centralizes lookup, validation, logging and invocation of Hyprland
 * dispatchers so higher-level focus/window routing code no longer touches the
 * raw dispatcher map directly.
 */
#include <cstdio>
#include <string>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/config/shared/actions/ConfigActions.hpp>
#include <hyprland/src/desktop/state/ViewState.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include "core/focus_pointer_sync.h"
#include <spdlog/spdlog.h>

#include "core/direction.h"
#include "core/hyprland_runtime.h"
#include "core/workspace_selector.h"
#include "internal.h"

namespace {

// Default production runtime backed by Hyprland globals. Tests can replace it
// with a fake implementation through set_dispatcher_runtime_for_tests().
class HyprlandDispatcherRuntime final : public KartaLayoutInternal::DispatcherRuntime {
public:
    bool hasDispatcherRegistry() const override {
        return true;
    }

    bool hasDispatcher(const char *dispatcher) const override {
        if (!dispatcher)
            return false;
        const std::string_view name(dispatcher);
        return name == "focusmonitor" || name == "workspace" || name == "focuswindow" ||
               name == "movetoworkspacesilent" || name == "movefocus" || name == "movewindow";
    }

    bool invokeDispatcher(const char *dispatcher, std::string_view arg) const override {
        if (!dispatcher)
            return false;

        const std::string name(dispatcher);
        const std::string argument(arg);
        if (name == "focusmonitor") {
            for (const auto& monitor : KartaCore::HyprlandRuntime::monitors()) {
                if (monitor && monitor->m_name == argument)
                    return Config::Actions::focusMonitor(monitor).has_value();
            }
            return false;
        }
        if (name == "workspace")
            return Config::Actions::changeWorkspace(argument).has_value();
        if (name == "movetoworkspacesilent")
            return Config::Actions::moveToWorkspace(argument, true).has_value();
        if (name == "focuswindow") {
            const auto window = Desktop::viewState()->query().selector(argument).mappedOnly().runWindow();
            return window && Config::Actions::focus(window).has_value();
        }

        if (arg.empty())
            return false;
        const auto direction = Math::fromChar(arg.front());
        if (name == "movefocus")
            return Config::Actions::moveFocus(direction).has_value();
        if (name == "movewindow")
            return Config::Actions::moveInDirection(direction).has_value();

        return false;
    }

    PHLMONITOR getMonitorFromID(int monitorId) const override {
        return g_pCompositor ? KartaCore::HyprlandRuntime::monitorById(monitorId) : nullptr;
    }

    PHLMONITOR getFocusedMonitor() const override {
        return g_pCompositor ? KartaCore::HyprlandRuntime::monitorForKeyboardAction() : nullptr;
    }

    bool isWindowActive(PHLWINDOW window) const override {
        return g_pCompositor && window && KartaCore::HyprlandRuntime::isWindowActive(window);
    }

    std::string monitorName(PHLMONITOR monitor) const override {
        return monitor ? monitor->m_name : std::string();
    }

    WORKSPACEID workspaceID(PHLWORKSPACE workspace) const override {
        return workspace ? KartaCore::HyprlandRuntime::workspaceId(workspace) : WORKSPACE_INVALID;
    }

    bool isWorkspaceSpecial(PHLWORKSPACE workspace) const override {
        return workspace && KartaCore::HyprlandRuntime::isSpecialWorkspace(workspace);
    }

    std::string workspaceSelector(PHLWORKSPACE workspace) const override {
        return KartaCore::workspace_selector(workspace);
    }

    bool activateMonitorWorkspaceDirectly(PHLMONITOR monitor, PHLWORKSPACE workspace, WORKSPACEID fallbackWorkspaceId) const override {
        if (!monitor)
            return false;

        auto target = workspace;
        if (!target && fallbackWorkspaceId != WORKSPACE_INVALID) {
            target = KartaCore::HyprlandRuntime::workspaceById(fallbackWorkspaceId);
            if (!target && fallbackWorkspaceId >= 0)
                target = State::workspaceState()->createNumbered(
                    Workspace::SWorkspaceNumberedID(static_cast<Workspace::WorkspaceIDContainer>(fallbackWorkspaceId)), monitor);
        }
        if (!target || target->monitorID() != monitor->m_id)
            return false;

        // Focusing the monitor first refocuses its outgoing workspace and can
        // overwrite the window it remembers. Switch in place, without synthetic
        // pointer movement; background karta members must not acquire focus.
        const bool noFocus = getFocusedMonitor() != monitor;
        if (KartaCore::HyprlandRuntime::isSpecialWorkspace(target))
            monitor->setSpecialWorkspace(target, noFocus);
        else
            monitor->changeWorkspace(target, false, true, noFocus);

        return isWorkspaceActiveOnMonitor(monitor, workspace, fallbackWorkspaceId);
    }

    bool isWorkspaceActiveOnMonitor(PHLMONITOR monitor, PHLWORKSPACE workspace, WORKSPACEID fallbackWorkspaceId) const override {
        if (!monitor)
            return false;

        if (workspace) {
            return KartaCore::HyprlandRuntime::isSpecialWorkspace(workspace)
                ? KartaCore::HyprlandRuntime::activeSpecialWorkspaceId(monitor) == KartaCore::HyprlandRuntime::workspaceId(workspace)
                : KartaCore::HyprlandRuntime::activeWorkspaceId(monitor) == KartaCore::HyprlandRuntime::workspaceId(workspace);
        }

        if (fallbackWorkspaceId != WORKSPACE_INVALID)
            return KartaCore::HyprlandRuntime::activeWorkspaceId(monitor) == fallbackWorkspaceId;

        return true;
    }

    MONITORID windowMonitorID(PHLWINDOW window) const override {
        return window ? window->monitorID() : MONITOR_INVALID;
    }

    WORKSPACEID windowWorkspaceID(PHLWINDOW window) const override {
        return window ? KartaCore::HyprlandRuntime::windowWorkspaceId(window) : WORKSPACE_INVALID;
    }

    void warpCursorToWindow(PHLWINDOW window) const override {
        KartaCore::FocusPointerSync::request(window, KartaCore::FocusPointerSync::Policy::Navigation);
    }
};

KartaLayoutInternal::DispatcherRuntime *g_dispatcherRuntimeOverride = nullptr;

KartaLayoutInternal::DispatcherRuntime &dispatcher_runtime() {
    static HyprlandDispatcherRuntime runtime;
    return g_dispatcherRuntimeOverride ? *g_dispatcherRuntimeOverride : runtime;
}

} // namespace

namespace KartaLayoutInternal {

void sync_new_window_pointer(PHLWINDOW window) {
    KartaCore::FocusPointerSync::request(window, KartaCore::FocusPointerSync::Policy::NewWindow);
}

void set_dispatcher_runtime_for_tests(DispatcherRuntime *runtime) {
    g_dispatcherRuntimeOverride = runtime;
}

bool can_invoke_dispatcher(const char* dispatcher, std::string_view arg, const char* context) {
    return KartaLayoutInternal::can_invoke_dispatcher(dispatcher_runtime(), dispatcher, arg, context);
}

bool invoke_dispatcher(const char* dispatcher, std::string_view arg, const char* context) {
    return KartaLayoutInternal::invoke_dispatcher(dispatcher_runtime(), dispatcher, arg, context);
}

// Shared wrapper around Hyprland builtin directional dispatchers.
void dispatch_directional_builtin(const char* dispatcher, Direction direction) {
    const auto arg = KartaCore::direction_dispatch_arg(direction);
    if (!arg) {
        spdlog::warn("dispatch_directional_builtin: unsupported direction={} dispatcher={}",
                     KartaCore::direction_name(direction),
                     dispatcher ? dispatcher : "(null)");
        return;
    }

    (void)invoke_dispatcher(dispatcher, arg, "dispatch_directional_builtin");
}

// Thin specialized wrapper for builtin movefocus.
void dispatch_builtin_movefocus(Direction direction) {
    dispatch_directional_builtin("movefocus", direction);
}

// Focus a monitor even when no concrete target window exists yet.
bool focus_monitor_workspace(PHLMONITOR monitor, PHLWORKSPACE workspace, WORKSPACEID fallback_workspace_id,
                             bool require_monitor_focus, const char* context) {
    return focus_monitor_workspace(dispatcher_runtime(), monitor, workspace, fallback_workspace_id, require_monitor_focus, context);
}

// Focus the monitor hosting a target window before focusing the window itself.
void focus_window_monitor(PHLWINDOW window) {
    focus_window_monitor(dispatcher_runtime(), window);
}

// Focus a target window and optionally warp the cursor to it.
bool switch_to_window(PHLWINDOW window, bool warp_cursor)
{
    KartaCore::FocusPointerSync::request(nullptr, KartaCore::FocusPointerSync::Policy::Preserve);
    return switch_to_window(dispatcher_runtime(), window, warp_cursor);
}

} // namespace KartaLayoutInternal
