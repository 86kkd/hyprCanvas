/**
 * @file plugin/dispatch/layout_commands.cpp
 * @brief Layout-oriented dispatcher entrypoints and registration.
 */
#include "plugin/dispatch/shared.h"

#include <lua.hpp>

#include "core/core.h"

namespace dispatchers::detail {
namespace {

// cyclesize(+1|-1): change active stack width/height step.
//
// TODO(set_window_size): Replace this next/prev preset cycler with
// `set_window_size(n)`, where `n` is how many stacks should fit side-by-side
// on the current monitor workarea (span ~= workarea / n). Typical monitors
// want n=2; ultrawides may want n=3 or 4. After the new command lands, delete
// `cycle_size` / `dispatch_cyclesize` entirely so the old 1/3·1/2·2/3 ladder
// cannot keep interacting with fullscreen / stack transfer paths.
void dispatch_cyclesize(std::string arg) {
    int step = 0;
    if (arg == "+1" || arg == "1" || arg == "next") {
        step = 1;
    } else if (arg == "-1" || arg == "prev" || arg == "previous") {
        step = -1;
    } else {
        return;
    }

    if (const auto action = workspaceLayoutForAction())
        action.layout->cycle_window_size(action.workspace, step);
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

// fitsize <active|visible|all|toend|tobeg>: resize visible windows so they
// fit requested range.
//
// TODO(remove-fit-size-command): Delete the public `fit_size` command. This is
// NOT overview: overview is a read-only snapshot/navigation layer, while
// fit_size mutates real stack spans so a chosen range fills the workarea.
// Remove Lua `fit_size` / dispatch / CanvasLayout::fit_size / Lane::fit_size
// command path. Keep or inline the internal `Stack::fit_size(FitSize::All, …)`
// helpers still used by move_window/relayout until those call sites are
// rewritten.
void dispatch_fitsize(std::string arg) {
    const auto fitSize = parsedFitSizeArg(arg);
    if (!fitSize)
        return;

    if (const auto action = workspaceLayoutForAction())
        action.layout->fit_size(action.workspace, *fitSize);
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

// TODO(set_window_size): Swap this Lua binding for `set_window_size` and remove
// `hl.plugin.scroller.cycle_size` once callers have migrated.
int lua_cycle_size(lua_State* state) {
    dispatch_cyclesize(luaStringArg(state));
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

int lua_set_mode(lua_State* state) {
    dispatch_setmode(luaStringArg(state));
    return 0;
}

// TODO(remove-fit-size-command): Remove this Lua binding with the public command.
int lua_fit_size(lua_State* state) {
    dispatch_fitsize(luaStringArg(state));
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
    // TODO(set_window_size): Register `set_window_size` here and drop `cycle_size`.
    registerLuaFunction("cycle_size", lua_cycle_size);
    registerLuaFunction("move_focus", lua_move_focus);
    registerLuaFunction("focus_monitor", lua_focus_monitor);
    registerLuaFunction("switch_workspace", lua_switch_workspace);
    registerLuaFunction("move_window", lua_move_window);
    registerLuaFunction("set_mode", lua_set_mode);
    // TODO(remove-fit-size-command): Drop this registration.
    registerLuaFunction("fit_size", lua_fit_size);
    registerLuaFunction("toggle_fullscreen", lua_toggle_fullscreen);
}

} // namespace dispatchers::detail
