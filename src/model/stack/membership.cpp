/**
 * @file model/stack/membership.cpp
 * @brief Stack membership, focus movement, and active-window transitions.
 *
 * Geometry code assumes the linked-list order of `windows` is authoritative.
 * The helpers in this file therefore do two jobs together: mutate that list and
 * keep the `active` cursor pointing at the logical window the user expects.
 */
#include "model/stack/internal.h"

#include <hyprland/src/Compositor.hpp>

#include "core/layout_profile.h"

#include "model/stack/logic.h"

namespace CanvasModel {

Stack::WindowNode *Stack::findWindowNode(PHLWINDOW window) const {
    if (!window)
        return nullptr;

    // The list is usually short, so a linear scan keeps membership logic simple
    // and avoids maintaining extra pointer-to-node indexes.
    for (auto win = windows.first(); win != nullptr; win = win->next()) {
        if (win->data()->ptr().lock() == window)
            return win;
    }

    return nullptr;
}

bool Stack::has_window(PHLWINDOW window) const {
    return findWindowNode(window) != nullptr;
}

bool Stack::swap_windows(PHLWINDOW a, PHLWINDOW b) {
    if (a == b)
        return false;

    auto *na = findWindowNode(a);
    auto *nb = findWindowNode(b);
    if (!na || !nb)
        return false;

    // Swapping list nodes would otherwise leave `active` pointing at the old
    // node, so we repair the cursor to continue tracking the same logical window.
    windows.swap(na, nb);
    if (active == na)
        active = nb;
    else if (active == nb)
        active = na;
    return true;
}

bool Stack::swap_active_window_with(Stack &other) {
    if (this == &other || mode != other.mode || !active || !other.active ||
        !get_active_window() || !other.get_active_window() || fullscreen() || other.fullscreen())
        return false;

    auto *a = active->data().get();
    auto *b = other.active->data().get();
    a->set_fullsize_before_join(a->get_fullsize_before_join().value_or(expanded()));
    b->set_fullsize_before_join(b->get_fullsize_before_join().value_or(other.expanded()));
    a->swap_slot_geometry(*b);
    // Swap payloads only: active cursors and list positions stay in their stacks.
    auto *source = active;
    auto *target = other.active;
    windows.swap(source, target);
    if (previousActive == a)
        previousActive = b;
    if (other.previousActive == b)
        other.previousActive = a;
    return true;
}

void Stack::remove_window(PHLWINDOW window) {
    auto *win = findWindowNode(window);
    if (!win)
        return;

    // When the active node is removed, pick the successor that should inherit
    // focus before erasing the node so we do not lose neighborhood context.
    if (active && window == active->data()->ptr().lock())
        active = StackLogic::next_active_after_removal(active, windows.last());

    if (previousActive == win->data().get())
        previousActive = nullptr;
    windows.erase(win);
    if (windows.size() != 1 || !active)
        return;

    // A single surviving window should reset to the canonical full-height state
    // rather than preserving whatever fragment size it previously had.
    active->data()->update_height(WindowHeight::One, StackInternal::stack_local_span(geom, mode));
}

void Stack::activate_window(WindowNode *node) {
    if (node == active)
        return;
    previousActive = active ? active->data().get() : nullptr;
    active = node;
}

void Stack::focus_window(PHLWINDOW window) {
    if (auto *win = findWindowNode(window))
        activate_window(win);
}

PHLWINDOW Stack::get_active_window() {
    return active ? active->data()->ptr().lock() : nullptr;
}

bool Stack::active_at_edge(Direction direction) const {
    if (!active)
        return false;

    switch (direction) {
    case Direction::Left:
        return mode == Mode::Column && active == windows.first();
    case Direction::Right:
        return mode == Mode::Column && active == windows.last();
    case Direction::Up:
        return mode == Mode::Row && active == windows.first();
    case Direction::Down:
        return mode == Mode::Row && active == windows.last();
    default:
        return false;
    }
}

void Stack::move_active(Direction direction) {
    if (!active)
        return;

    const auto backward = CanvasCore::stack_item_backward_direction(mode);
    const auto forward = CanvasCore::stack_item_forward_direction(mode);
    if (direction == backward && active != windows.first()) {
        auto previous = active->prev();
        previousActive = previous->data().get();
        windows.swap(active, previous);
        return;
    }

    if (direction == forward && active != windows.last()) {
        auto next = active->next();
        previousActive = next->data().get();
        windows.swap(active, next);
    }
}

size_t Stack::active_index() const {
    size_t index = 0;
    for (auto *node = windows.first(); node != nullptr && node != active; node = node->next())
        ++index;
    return index;
}

void Stack::focus_index(size_t index) {
    auto *node = windows.first();
    for (size_t i = 0; node != nullptr && i < index; ++i)
        node = node->next();
    if (node)
        activate_window(node);
}

void Stack::admit_window(std::unique_ptr<Window> window) {
    if (!window)
        return;

    // New windows are inserted adjacent to the active window and inherit its
    // size so the subsequent relayout can start from a stable local geometry.
    if (active) {
        const auto *activeWindow = active->data().get();
        window->set_geom_h(activeWindow->get_geom_h());
        window->set_geom_y(activeWindow->get_geom_y() + activeWindow->get_geom_h());
    } else {
        window->set_geom_h(StackInternal::stack_local_span(geom, mode));
        window->set_geom_y(StackInternal::stack_local_origin(geom, mode));
    }

    activate_window(windows.emplace_after(active, std::move(window)));
}

void Stack::restore_window(std::unique_ptr<Window> window, bool insertBeforeActive) {
    if (!window)
        return;

    // Restoring a detached window differs from admitting a brand-new one: we
    // preserve its remembered size/position unless the stack is currently empty.
    if (!active) {
        window->set_geom_h(StackInternal::stack_local_span(geom, mode));
        window->set_geom_y(StackInternal::stack_local_origin(geom, mode));
        activate_window(windows.emplace_after(active, std::move(window)));
        return;
    }

    activate_window(insertBeforeActive
        ? windows.emplace_before(active, std::move(window))
        : windows.emplace_after(active, std::move(window)));
}

std::unique_ptr<Window> Stack::expel_active(double /*gap*/) {
    if (!active)
        return {};

    // Ownership is transferred back to the caller. We compute the replacement
    // active node before extracting so later code still has a valid focus cursor.
    auto *nextActive = StackLogic::next_active_after_removal(active, windows.last());
    auto window = windows.extract(active);
    active = nextActive;
    if (window) {
        window->set_deck_label(DeckLabelEdge::None);
        if (previousActive == window.get())
            previousActive = nullptr;
    }
    return window;
}

} // namespace CanvasModel
