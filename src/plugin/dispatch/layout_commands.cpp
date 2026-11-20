/**
 * @file plugin/dispatch/layout_commands.cpp
 * @brief Layout-oriented dispatcher entrypoints and registration.
 */
#include "plugin/dispatch/shared.h"

#include <lua.hpp>

#include "core/core.h"
#include "model/stack/width.h"

namespace dispatchers::detail {
namespace {

// setwindowsize <n>: size the active stack so `n` stacks share the monitor
// workarea along the lane axis.
void dispatch_setwindowsize(std::string arg) {
    const auto count = ScrollerModel::parse_visible_stack_count(firstDispatchArg(arg));
    if (!count)
        return;

    if (const auto action = workspaceLayoutForAction())
        action.layout->set_window_size(action.workspace, *count);
}

// movefocus <dir>: move focus inside scroller layout, with optional monitor
// fallback when the active lane cannot move in requested direction.
// This is the top of the focus path:
// keybind -> dispatcher parse -> active canvas -> `CanvasLayout::move_focus`.
void dispatch_movefocus(std::string arg) {
    const auto direction = parsedDirectionArg(arg);
    if (!direction) {
        spdlog::warn("dispatch_movefocus: unsupported arg='{}'", arg);
        return;
    }

    if (Overview::session().active()) {
        spdlog::info("dispatch_movefocus: overview arg='{}'", arg);
        Overview::session().markInputHandled();
        (void)Overview::session().moveSelection(*direction);
        return;
    }

    const auto action = workspaceLayoutForAction();
    if (action) {
        spdlog::info("dispatch_movefocus: arg='{}' workspace={}", arg, action.workspace);
        action.layout->move_focus(action.workspace, *direction);
        return;
    }

    spdlog::warn("dispatch_movefocus: no layout for arg='{}', fallback builtin", arg);
    CanvasLayoutInternal::dispatch_builtin_movefocus(*direction);
}

// focusmonitor <dir>: move monitor focus in normal mode, or move between
// canvas-workspace tiles in overview mode.
void dispatch_focusmonitor(std::string arg) {
    const auto direction = parsedDirectionArg(arg);
    if (!direction) {
        spdlog::warn("dispatch_focusmonitor: unsupported arg='{}'", arg);
        return;
    }

    if (Overview::session().active()) {
        spdlog::info("dispatch_focusmonitor: overview arg='{}'", arg);
        overviewSessionForDispatch().moveCanvasSelection(*direction);
        return;
    }

    const auto sourceMonitor = ScrollerCore::monitorForKeyboardAction();
    const auto monitorDirection = CanvasLayoutInternal::direction_to_math(*direction);
    if (!g_pCompositor || !sourceMonitor || !monitorDirection) {
        spdlog::warn("dispatch_focusmonitor: no route direction={} source_monitor={}",
                     ScrollerCore::direction_name(*direction),
                     sourceMonitor ? sourceMonitor->m_id : MONITOR_INVALID);
        return;
    }

    const auto targetMonitor = ScrollerCore::HyprlandRuntime::monitorInDirection(sourceMonitor, *monitorDirection);
    if (!targetMonitor) {
        spdlog::info("dispatch_focusmonitor: no adjacent monitor direction={} source_monitor={}",
                     ScrollerCore::direction_name(*direction),
                     sourceMonitor->m_id);
        return;
    }

    const auto targetWorkspaceId = CanvasLayoutInternal::preferred_workspace_id(targetMonitor, ScrollerCore::HyprlandRuntime::activeWorkspaceId(targetMonitor));
    const auto targetWorkspace = ScrollerCore::HyprlandRuntime::workspaceById(targetWorkspaceId);
    if (!CanvasLayoutInternal::focus_monitor_workspace(targetMonitor,
                                                       targetWorkspace,
                                                       targetWorkspaceId,
                                                       true,
                                                       "dispatch_focusmonitor")) {
        spdlog::warn("dispatch_focusmonitor: failed direction={} target_monitor={} workspace={}",
                     ScrollerCore::direction_name(*direction),
                     targetMonitor->m_id,
                     targetWorkspaceId);
    }
}

// movewindow <dir>: reorder active window inside lane/stack.
// This is the top of the window-move path:
// keybind -> dispatcher parse -> active canvas -> `CanvasLayout::move_window`.
void dispatch_movewindow(std::string arg) {
    const auto direction = parsedDirectionArg(arg);
    if (!direction) {
        spdlog::warn("dispatch_movewindow: unsupported arg='{}'", arg);
        return;
    }

    const auto action = workspaceLayoutForAction();
    if (action) {
        action.layout->move_window(action.workspace, *direction);
        return;
    }

    spdlog::warn("dispatch_movewindow: no layout for arg='{}'", arg);
}

// setmode row|col: switch between row mode and column mode.
void dispatch_setmode(std::string arg) {
    withParsedModeArgOrWarn("dispatch_setmode", arg, [&](Mode mode) {
        if (const auto action = workspaceLayoutForAction())
            action.layout->set_mode(action.workspace, mode);
    });
}

// togglefullscreen: expand the active scroller window to the monitor bounds.
void dispatch_togglefullscreen(std::string arg) {
    (void)arg;
    if (const auto action = workspaceLayoutForAction())
        action.layout->toggle_fullscreen(action.workspace);
}

} // namespace

namespace {

std::string luaStringArg(lua_State* state) {
    if (lua_gettop(state) < 1 || !lua_isstring(state, 1))
        return {};

    size_t length = 0;
    const char* value = lua_tolstring(state, 1, &length);
    return value ? std::string(value, length) : std::string();
}

int lua_move_focus(lua_State* state) {
    dispatch_movefocus(luaStringArg(state));
    return 0;
}

int lua_set_window_size(lua_State* state) {
    dispatch_setwindowsize(luaStringArg(state));
    return 0;
}

int lua_focus_monitor(lua_State* state) {
    dispatch_focusmonitor(luaStringArg(state));
    return 0;
}

// Use the same canvas transaction as overview acceptance, even when the
// caller switches directly from normal mode. No native per-monitor workspace
// dispatch is involved, so every canvas member changes together.
int lua_switch_workspace(lua_State* state) {
    const auto argument = luaStringArg(state);
    const auto direction = parsedDirectionArg(argument);
    const int number = argument == "10" ? 10 :
        (argument.size() == 1 && argument[0] >= '1' && argument[0] <= '9' ? argument[0] - '0' : 0);
    if (direction && *direction != Direction::Left && *direction != Direction::Right &&
        *direction != Direction::Up && *direction != Direction::Down)
        return 0;
    if (!direction && number == 0)
        return 0;
    auto& overview = Overview::session();
    const bool wasActive = overview.active();
    if (!wasActive)
        overview.open();
    if (overview.active()) {
        overview.markInputHandled();
        const bool moved = direction ? overview.moveCanvasSelection(*direction) : overview.selectCanvasNumber(number);
        if (!wasActive)
            overview.close(moved);
    }
    return 0;
}

int lua_move_window(lua_State* state) {
    dispatch_movewindow(luaStringArg(state));
    return 0;
}

int lua_swap_window(lua_State* state) {
    if (Overview::session().active())
        return 0;
    const auto direction = parsedDirectionArg(luaStringArg(state));
    if (!direction)
        return 0;
    if (const auto action = workspaceLayoutForAction())
        action.layout->swap_window(action.workspace, *direction);
    return 0;
}

int lua_set_mode(lua_State* state) {
    dispatch_setmode(luaStringArg(state));
    return 0;
}

int lua_toggle_fullscreen(lua_State* state) {
    dispatch_togglefullscreen(luaStringArg(state));
    return 0;
}

void registerLuaFunction(const char* name, PLUGIN_LUA_FN function) {
    if (!HyprlandAPI::addLuaFunction(PHANDLE, "scroller", name, function))
        spdlog::warn("failed to register Lua function hl.plugin.scroller.{}", name);
}

} // namespace

void registerLayoutLuaFunctions() {
    registerLuaFunction("set_window_size", lua_set_window_size);
    registerLuaFunction("move_focus", lua_move_focus);
    registerLuaFunction("focus_monitor", lua_focus_monitor);
    registerLuaFunction("switch_workspace", lua_switch_workspace);
    registerLuaFunction("move_window", lua_move_window);
    registerLuaFunction("swap_window", lua_swap_window);
    registerLuaFunction("set_mode", lua_set_mode);
    registerLuaFunction("toggle_fullscreen", lua_toggle_fullscreen);
}

} // namespace dispatchers::detail
