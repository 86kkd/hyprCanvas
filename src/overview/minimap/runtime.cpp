/** @file runtime.cpp
 * @brief Adapt the canvas repository and overview preview to one shared map.
 */
#include "runtime.h"
#include "core/core.h"
#include "core/monitor_geometry_runtime.h"
#include "layout/canvas/canvas_workspace_repository.h"
#include "overview/session/session.h"
#include "plugin/config.h"
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/managers/SessionLockManager.hpp>
#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/managers/eventLoop/EventLoopManager.hpp>
#include <algorithm>
#include <spdlog/spdlog.h>

namespace Overview::Minimap {
namespace {
std::optional<steady_tp> switchedAt;
int hudMonitor = -1;
SP<CEventLoopTimer> hudTimer;
int displayMonitor() {
    if (session().active())
        return session().model().origin().monitorId;
    return hudMonitor;
}
}
void notifySwitch(int fromCanvas, int toCanvas) {
    if (fromCanvas == toCanvas || fromCanvas < 0 || toCanvas < 0)
        return;
    const auto monitor = ScrollerCore::monitorForKeyboardAction();
    if (!monitor)
        return;
    if (const auto old = ScrollerCore::HyprlandRuntime::monitorById(hudMonitor))
        g_pHyprRenderer->damageMonitor(old);
    hudMonitor = monitor->m_id;
    switchedAt = std::chrono::steady_clock::now();
    // A quiet Wayland output may stop producing frames. A render.pre callback
    // cannot wake that output to expire its own HUD; own the deadline in the
    // compositor event loop, and request frames only for the short fade tail.
    if (!hudTimer) {
        hudTimer = makeShared<CEventLoopTimer>(std::nullopt, [](SP<CEventLoopTimer> timer, void*) {
            if (!switchedAt) {
                timer->updateTimeout(std::nullopt);
                return;
            }
            const auto elapsed = std::chrono::steady_clock::now() - *switchedAt;
            const bool expired = elapsed >= std::chrono::milliseconds(1500);
            if (expired) {
                switchedAt.reset();
                timer->updateTimeout(std::nullopt);
                spdlog::debug("canvas_minimap: expired monitor={}", hudMonitor);
            } else {
                timer->updateTimeout(std::chrono::milliseconds(16));
            }
            if (const auto output = ScrollerCore::HyprlandRuntime::monitorById(hudMonitor)) {
                g_pHyprRenderer->damageMonitor(output);
                output->scheduleFrame();
            }
        }, nullptr);
        g_pEventLoopManager->addTimer(hudTimer);
    }
    hudTimer->updateTimeout(std::chrono::milliseconds(1300));
    g_pHyprRenderer->damageMonitor(monitor);
    monitor->scheduleFrame();
    spdlog::debug("canvas_minimap: switch from={} to={} monitor={}", fromCanvas, toCanvas, hudMonitor);
}
void reset() {
    if (const auto monitor = ScrollerCore::HyprlandRuntime::monitorById(hudMonitor))
        g_pHyprRenderer->damageMonitor(monitor);
    switchedAt.reset();
    hudMonitor = -1;
    if (hudTimer) {
        g_pEventLoopManager->removeTimer(hudTimer);
        hudTimer.reset();
    }
}
double alpha(steady_tp now) {
    return opacity(session().active(), switchedAt ? std::chrono::duration<double, std::milli>(now - *switchedAt).count() : -1);
}
bool visible(PHLMONITOR monitor, steady_tp now) {
    static auto layoutName = CConfigValue<std::string>("general:layout");
    return *layoutName == "scroller"
        && (!g_pSessionLockManager || !g_pSessionLockManager->isSessionLocked())
        && scroller::plugin_config::canvasMap()
        && monitor && monitor->m_id == displayMonitor() && alpha(now) > 0;
}
Map mapForMonitor(PHLMONITOR monitor) {
    std::vector<Canvas> canvases;
    auto& repository = CanvasLayoutState::canvasRepository();
    for (const auto& canvas : repository.canvases())
        canvases.push_back({canvas.canvasId, canvas.tileX, canvas.tileY, canvas.ephemeral, false});
    int selected = repository.activeCanvasId();
    if (session().active()) {
        selected = session().model().selectionCanvasId().value_or(selected);
        for (const auto& node : session().model().canvasGraph()) {
            const auto existing = std::find_if(canvases.begin(), canvases.end(), [&](const auto& c) { return c.id == node.canvasId; });
            if (existing == canvases.end())
                canvases.push_back({node.canvasId, node.tileX, node.tileY, false, node.synthetic});
            else {
                existing->x = node.tileX;
                existing->y = node.tileY;
            }
        }
    }
    auto area = ScrollerCore::logical_workarea_box(monitor, 0);
    area.x -= monitor->m_position.x;
    area.y -= monitor->m_position.y;
    return layout(std::move(canvases), repository.activeCanvasId(), selected, area);
}
void click(double globalX, double globalY) {
    const auto monitor = ScrollerCore::HyprlandRuntime::monitorById(displayMonitor());
    if (!session().active() || !visible(monitor, std::chrono::steady_clock::now()))
        return;
    const auto cell = hit(mapForMonitor(monitor), globalX - monitor->m_position.x, globalY - monitor->m_position.y);
    if (!cell)
        return;
    if (!cell->create) {
        session().previewCanvas(cell->canvas.id);
        return;
    }
    const auto map = mapForMonitor(monitor);
    const auto anchor = std::find_if(map.cells.begin(), map.cells.end(), [](const auto& c) { return c.selected; });
    if (anchor == map.cells.end())
        return;
    const auto direction = cell->canvas.x < anchor->canvas.x ? Direction::Left :
                           cell->canvas.x > anchor->canvas.x ? Direction::Right :
                           cell->canvas.y < anchor->canvas.y ? Direction::Up : Direction::Down;
    session().moveCanvasSelection(direction);
}
} // namespace Overview::Minimap
