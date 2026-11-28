/**
 * @file focus_pointer_sync.cpp
 * @brief Coalesce focus-driven pointer updates after layout and mapping settle.
 */
#include "focus_pointer_sync.h"
#include "hyprland_runtime.h"
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/managers/eventLoop/EventLoopManager.hpp>
#include <cmath>

namespace KartaCore::FocusPointerSync {
namespace {
UP<SEventLoopDoLaterLock> pending;
CHyprSignalListener mouseMove;
CHyprSignalListener mouseButton;
uint64_t generation = 0;
Vector2D requestedPointer;

void cancel() {
    ++generation;
    pending.reset();
}
}

void initialize() {
    mouseMove = Event::bus()->m_events.input.mouse.move.listen([](Vector2D position, Event::SCallbackInfo&) {
        // Refocus may emit a move notification at unchanged coordinates.
        if (position != requestedPointer)
            cancel();
    });
    mouseButton = Event::bus()->m_events.input.mouse.button.listen([](IPointer::SButtonEvent, Event::SCallbackInfo&) { cancel(); });
}

void shutdown() {
    cancel();
    mouseMove.reset();
    mouseButton.reset();
}

void request(PHLWINDOW window, Policy policy) {
    cancel();
    if (!window || policy == Policy::Preserve)
        return;

    // Both navigation and mapping must wait for committed target geometry.
    // Never force focus here: compositor no-focus rules remain authoritative.
    const auto ticket = generation;
    const auto pointer = Pointer::mgr()->position();
    requestedPointer = pointer;
    pending = g_pEventLoopManager->doLaterLock([weak = PHLWINDOWREF(window), ticket, pointer] {
        const auto target = weak.lock();
        if (ticket != generation || !target || !HyprlandRuntime::isWindowActive(target) ||
            Pointer::mgr()->position() != pointer)
            return;
        const auto size = HyprlandRuntime::windowSize(target);
        const auto center = HyprlandRuntime::windowPosition(target) + size * 0.5;
        if (size.x <= 0 || size.y <= 0 || !std::isfinite(center.x) || !std::isfinite(center.y))
            return;
        // Position-only warp bypasses no_warps without generating input activity
        // that would wake a hidden cursor or reset its inactivity timer.
        Pointer::mgr()->warpTo(center);
    });
}
}
