/**
 * @file overview/render/hooks.cpp
 * @brief Internal overview render hook bootstrap.
 */
#include "overview/render/hooks.h"

#include <chrono>
#include <exception>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/devices/IKeyboard.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/managers/SeatManager.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <spdlog/spdlog.h>

#include "core/hyprland_runtime.h"
#include "overview/minimap/runtime.h"
#include <linux/input-event-codes.h>
#include "overview/render/pass_element.h"
#include "overview/render/draw.h"
#include "overview/render/state.h"
#include "overview/scene/scene.h"
#include "overview/session/session.h"
#include "overview/navigation/selection.h"

namespace Overview {
namespace {

CHyprSignalListener g_renderPreListener = nullptr;
CHyprSignalListener g_renderStageListener = nullptr;
CHyprSignalListener g_keyboardKeyListener = nullptr;
CHyprSignalListener g_mapButtonListener = nullptr;
bool g_mapPressed = false;

bool no_overview_modifiers_remaining() {
    constexpr Input::ModifierMask kTransientModifierMask =
        Input::HL_MODIFIER_SHIFT |
        Input::HL_MODIFIER_CTRL |
        Input::HL_MODIFIER_ALT |
        Input::HL_MODIFIER_MOD2 |
        Input::HL_MODIFIER_MOD3 |
        Input::HL_MODIFIER_META |
        Input::HL_MODIFIER_MOD5;

    if (!g_pSeatManager)
        return true;

    const auto keyboard = g_pSeatManager->m_keyboard.lock();
    if (!keyboard)
        return true;

    // Hold-to-preview overview should commit once the last transient modifier
    // (super/alt/ctrl/shift-style keys) has been released.
    return !static_cast<bool>(keyboard->getModifiers() & kTransientModifierMask);
}

void damage_monitor_if_animating(PHLMONITOR monitor, steady_tp now) {
    if (!monitor)
        return;

    auto& state = renderState();
    if (state.previewAnimationActive(monitor->m_id)) {
        g_pHyprRenderer->damageMonitor(monitor);
        return;
    }

    if (state.openingAnimationActive(monitor->m_id, now, session().active())) {
        g_pHyprRenderer->damageMonitor(monitor);
        return;
    }

    if (state.selectionAnimationActive(monitor->m_id, now))
        g_pHyprRenderer->damageMonitor(monitor);
}

void handle_session_transition(steady_tp now) {
    auto& overview = session();
    auto& state = renderState();
    const auto overviewActive = overview.active();
    if (overviewActive == state.lastOverviewActive())
        return;

    if (overviewActive) {
        state.clearSessionState();
        // Window previews render directly from live surface textures; only
        // background/bottom backdrop layers need an upfront snapshot.
        snapshotBackdropLayers(overview.model(), state);
        state.rebuildPreviewAnimations(overview.model());
        for (const auto& region : overview.model().monitors()) {
            state.markOpened(region.monitorId, now);
            if (region.monitor)
                g_pHyprRenderer->damageMonitor(region.monitor);
        }
        spdlog::info("overview_renderer: enabled monitors={}", overview.model().monitors().size());
    } else {
        for (const auto& [monitorId, _scene] : state.scenes()) {
            if (const auto monitor = ScrollerCore::HyprlandRuntime::monitorById(monitorId))
                g_pHyprRenderer->damageMonitor(monitor);
        }
        state.clearSessionState();
        spdlog::info("overview_renderer: disabled monitors=0");
    }

    state.setLastOverviewActive(overviewActive);
}

void update_live_scene_for_monitor(PHLMONITOR monitor, steady_tp now) {
    if (!monitor)
        return;

    auto& state = renderState();
    const auto scene = buildSceneForMonitor(monitor, session().model());
    if (!scene) {
        state.eraseScene(monitor->m_id);
        return;
    }

    state.updateSelectionPulse(*scene, now);
    state.setScene(monitor->m_id, *scene);
}

} // namespace

bool initializeRendererHooksImpl(HANDLE handle) {
    (void)handle;
    if (g_renderPreListener || g_renderStageListener || g_keyboardKeyListener)
        return true;

    try {
        g_renderPreListener = Event::bus()->m_events.render.pre.listen([](PHLMONITOR monitor) {
            if (!monitor)
                return;

            const auto now = std::chrono::steady_clock::now();
            handle_session_transition(now);

            if (session().active())
                update_live_scene_for_monitor(monitor, now);

            damage_monitor_if_animating(monitor, now);
        });

        g_renderStageListener = Event::bus()->m_events.render.stage.listen([](const Event::SRenderStageEvent& event) {
            if (!event.context)
                return;
            if (event.stage == RENDER_LAST_MOMENT) {
                if (Minimap::visible(event.monitor, std::chrono::steady_clock::now()))
                    g_pHyprRenderer->addPassElement(event.context->get(), makeUnique<CanvasMapPassElement>(event.monitor));
                return;
            }
            if (event.stage != RENDER_POST_WINDOWS)
                return;

            auto& ctx = event.context->get();
            const auto monitor = event.monitor;
            if (!monitor || !session().active())
                return;

            if (renderState().sceneForMonitor(monitor->m_id)) {
                enqueueMonitorBackdrop(ctx, monitor, renderState());
                g_pHyprRenderer->addPassElement(ctx, makeUnique<OverviewPassElement>(monitor));
            }
        });

        g_mapButtonListener = Event::bus()->m_events.input.mouse.button.listen([](IPointer::SButtonEvent event, Event::SCallbackInfo& info) {
            if (event.button != BTN_LEFT)
                return;
            if (event.state == WL_POINTER_BUTTON_STATE_RELEASED && g_mapPressed) {
                g_mapPressed = false;
                info.cancelled = true;
                return;
            }
            if (!session().active() || event.state != WL_POINTER_BUTTON_STATE_PRESSED)
                return;
            const auto monitor = ScrollerCore::HyprlandRuntime::monitorById(session().model().origin().monitorId);
            if (!Minimap::visible(monitor, std::chrono::steady_clock::now()))
                return;
            const auto pointer = Pointer::mgr()->position();
            const auto map = Minimap::mapForMonitor(monitor);
            const double x = pointer.x - monitor->m_position.x, y = pointer.y - monitor->m_position.y;
            if (x < map.panel.x || y < map.panel.y || x >= map.panel.x + map.panel.w || y >= map.panel.y + map.panel.h)
                return;
            info.cancelled = true;
            g_mapPressed = true;
            Minimap::click(pointer.x, pointer.y);
        });

        g_keyboardKeyListener = Event::bus()->m_events.input.keyboard.key.listen([](IKeyboard::SKeyEvent event, Event::SCallbackInfo& info) {
            (void)info;
            auto& overview = session();
            const auto released = event.state == WL_KEYBOARD_KEY_STATE_RELEASED;
            const auto handledByOverview = overview.consumeInputHandled(released);
            const auto noModifiersRemaining = released && event.updateMods && no_overview_modifiers_remaining();

            if (shouldCloseOverviewOnKeyRelease(overview.active(),
                                                released,
                                                event.updateMods,
                                                handledByOverview,
                                                noModifiersRemaining))
                overview.close(true);
        });

        spdlog::info("overview_renderer_init: using render-pass overlay backend");
        return true;
    } catch (const std::exception& e) {
        g_renderPreListener = nullptr;
        g_renderStageListener = nullptr;
        g_keyboardKeyListener = nullptr;
        g_mapButtonListener = nullptr;
        Minimap::reset();
        renderState().clearAll();
        spdlog::warn("overview_renderer_init: disabled after exception: {}", e.what());
        return false;
    }
}

void shutdownRendererHooksImpl(HANDLE handle) {
    (void)handle;
    g_renderPreListener = nullptr;
    g_renderStageListener = nullptr;
    g_keyboardKeyListener = nullptr;
    g_mapButtonListener = nullptr;
    g_mapPressed = false;
    Minimap::reset();
    renderState().clearAll();
    auto& ctx = g_pHyprRenderer->context();
    if (ctx.active()) {
        ctx.m_pass.removeAllOfType("OverviewPassElement");
        ctx.m_pass.removeAllOfType("CanvasMapPassElement");
    }
    spdlog::info("overview_renderer_shutdown");
}

} // namespace Overview
