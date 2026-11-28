/**
 * @file actions.cpp
 * @brief Command-facing lane operations and focus movement.
 *
 * These methods mutate the active lane in response to user commands: inserting
 * and removing windows, moving focus, reordering stacks, and splitting/merging
 * windows between neighboring stacks.
 */
#include "lane.h"

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/output/Monitor.hpp>

#include "core/interval.h"
#include "core/layout_profile.h"

using KartaModel::Stack;
using KartaModel::StackWidth;

// Insert a new window into the active lane as a peer stack.
void Lane::add_active_window(PHLWINDOW window) {
    const bool singleWindowWorkspace = stacks.size() == 1 && stacks.first()->data()->size() == 1;
    if (singleWindowWorkspace)
        stacks.first()->data()->update_width(KartaModel::global_stack_width(), max.w, max.h);

    active = stacks.emplace_after(active, std::make_unique<Stack>(window, max.w, max.h, mode));
    rememberWindowStack(window, active->data().get());
    if (singleWindowWorkspace)
        active->data()->update_width(KartaModel::global_stack_width(), max.w, max.h);
    recalculate_lane_geometry();
    debugVerifyStackCache();
}

// Remove a window from this lane and keep stack/lane state coherent.
bool Lane::remove_window(PHLWINDOW window) {
    auto *col = getStackForWindow(window);
    auto *c = getStackNode(col);
    if (!col || !c)
        return true;

    forgetWindowStack(window);
    col->remove_window(window);

    if (col->size() == 0) {
        if (c == active)
            active = active != stacks.last() ? active->next() : active->prev();

        forgetStackWindows(col);
        stacks.erase(c);
        if (stacks.empty()) {
            debugVerifyStackCache();
            return false;
        }

        recalculate_lane_geometry();
        debugVerifyStackCache();
        return true;
    }

    col->recalculate_stack_geometry(calculate_gap_x(c), gap, max);
    debugVerifyStackCache();
    return true;
}

// Swap two windows when they both belong to the same stack in this lane.
bool Lane::swapWindows(PHLWINDOW a, PHLWINDOW b) {
    auto *stackA = getStackForWindow(a);
    auto *stackB = getStackForWindow(b);
    if (!stackA || !stackB || stackA != stackB)
        return false;

    return stackA->swap_windows(a, b);
}

// Focus the stack and window that owns the given compositor window.
void Lane::focus_window(PHLWINDOW window) {
    auto *stack = getStackForWindow(window);
    auto *stackNode = getStackNode(stack);
    if (!stack || !stackNode)
        return;

    stack->focus_window(window);
    active = stackNode;
    recalculate_lane_geometry();
}

// Report whether the active stack/window is already at the requested edge.
bool Lane::active_item_at_edge(Direction direction) const {
    if (!active)
        return false;

    if (direction == KartaCore::local_item_backward_direction(mode))
        return active == stacks.first();
    if (direction == KartaCore::local_item_forward_direction(mode))
        return active == stacks.last();
    if (direction == KartaCore::stack_item_backward_direction(mode) ||
        direction == KartaCore::stack_item_forward_direction(mode))
        return active->data()->active_at_edge(direction);

    return false;
}

bool Lane::active_stack_has_multiple_windows() const {
    return active && active->data()->size() > 1;
}

void Lane::focus_position(size_t stackIndex, std::optional<size_t> windowIndex) {
    set_active_stack_by_index(stackIndex);
    if (!active)
        return;
    if (windowIndex)
        active->data()->focus_index(*windowIndex);
    recalculate_lane_geometry();
}

// Give every stack the same width intent; the caller relayouts.
void Lane::apply_stack_width(StackWidth width) {
    for (auto stack = stacks.first(); stack != nullptr; stack = stack->next())
        stack->data()->apply_width(width, max.w, max.h);
}

// Resize the active window inside the current stack when resizing is allowed.
void Lane::resize_active_window(const Vector2D &delta) {
    if (!active)
        return;

    if (active->data()->maximized() ||
        active->data()->fullscreen() ||
        active->data()->expanded())
        return;

    active->data()->resize_active_window(max, calculate_gap_x(active), gap, delta);
    recalculate_lane_geometry();
}

// Change the lane traversal mode used by focus and insertion logic.
void Lane::set_mode(Mode m) {
    if (mode == m)
        return;

    mode = m;
    for (auto stack = stacks.first(); stack != nullptr; stack = stack->next())
        stack->data()->set_mode(mode, max.w, max.h);
    recalculate_lane_geometry();
}

// Reorder stacks along the stack axis, or windows along the stack-item axis.
void Lane::move_active_stack(Direction dir) {
    if (!active)
        return;

    if (dir == KartaCore::local_item_forward_direction(mode)) {
        if (active != stacks.last()) {
            auto next = active->next();
            stacks.swap(active, next);
        }
    } else if (dir == KartaCore::local_item_backward_direction(mode)) {
        if (active != stacks.first()) {
            auto prev = active->prev();
            stacks.swap(active, prev);
        }
    } else if (dir == KartaCore::stack_item_backward_direction(mode) ||
               dir == KartaCore::stack_item_forward_direction(mode)) {
        active->data()->move_active(dir);
    } else {
        switch (dir) {
    case Direction::Begin:
        if (active != stacks.first())
            stacks.move_before(stacks.first(), active);
        break;
    case Direction::End:
        if (active != stacks.last())
            stacks.move_after(stacks.last(), active);
        break;
    case Direction::Center:
        return;
        default:
            return;
        }
    }

    recalculate_lane_geometry();
}

bool Lane::swap_active_window_with_stack(size_t stackIndex) {
    if (!active)
        return false;
    auto *target = stacks.first();
    for (size_t i = 0; target && i < stackIndex; ++i)
        target = target->next();
    if (!target || (target != active->prev() && target != active->next()))
        return false;

    auto *sourceStack = active->data().get();
    auto *targetStack = target->data().get();
    const auto sourceWindow = sourceStack->get_active_window();
    const auto targetWindow = targetStack->get_active_window();
    if (!sourceStack->swap_active_window_with(*targetStack))
        return false;
    rememberWindowStack(sourceWindow, targetStack);
    rememberWindowStack(targetWindow, sourceStack);
    active = target;
    recalculate_lane_geometry();
    debugVerifyStackCache();
    return true;
}

void Lane::move_active_window_to_adjacent_stack(Direction dir) {
    if (!active)
        return;

    const auto backward = KartaCore::local_item_backward_direction(mode);
    const auto forward = KartaCore::local_item_forward_direction(mode);
    if (dir != backward && dir != forward)
        return;

    auto *sourceStack = active->data().get();
    if (sourceStack->maximized() ||
        sourceStack->fullscreen())
        return;

    auto *target = dir == backward ? active->prev() : active->next();
    if (!target)
        return;

    auto payload = extract_active_window_payload();
    const auto movedWindow = payload.window ? payload.window->ptr().lock() : nullptr;
    if (!payload)
        return;
    const auto targetWindowCountBefore = target->data()->size();
    const auto targetExpanded = target->data()->expanded();

    // Join the destination stack even when it is karta-fullsize. Expanded
    // stacks already know how to present multiple windows as a fullsize deck,
    // so admitting must not clear that flag or fall back to a sibling stack.
    active = target;
    payload.window->set_fullsize_before_join(payload.fullsize);
    active->data()->admit_window(payload.release_window());
    rememberWindowStack(movedWindow, active->data().get());

    if (targetWindowCountBefore == 1 && !targetExpanded)
        active->data()->fit_windows_to_span(calculate_gap_x(active), gap, max);
    recalculate_lane_geometry();
    debugVerifyStackCache();
}

void Lane::move_active_window_to_new_stack(Direction dir) {
    if (!active)
        return;

    const auto backward = KartaCore::local_item_backward_direction(mode);
    const auto forward = KartaCore::local_item_forward_direction(mode);
    if (dir != backward && dir != forward)
        return;

    auto *sourceStack = active->data().get();
    if (sourceStack->maximized() ||
        sourceStack->fullscreen() ||
        sourceStack->size() == 1)
        return;

    // The leftover deck keeps its fullsize flag; the expelled window gets back
    // the fullsize state it had before joining this stack.
    auto *sourceNode = active;
    sourceStack = sourceNode->data().get();
    auto payload = extract_active_window_payload();
    const auto movedWindow = payload.window ? payload.window->ptr().lock() : nullptr;
    if (!payload)
        return;
    const auto restoreFullsize = payload.fullsize;
    auto stack = create_stack_from_payload(std::move(payload));
    if (!stack)
        return;
    auto *stackPtr = stack.get();

    StackNode *inserted = nullptr;
    if (dir == backward) {
        inserted = stacks.emplace_before(sourceNode, std::move(stack));
    } else {
        inserted = stacks.emplace_after(sourceNode, std::move(stack));
    }
    position_stack_relative_to_reference(stackPtr, sourceStack, dir);
    if (restoreFullsize)
        (void)stackPtr->toggle_fullscreen(max);

    active = inserted;
    rememberWindowStack(movedWindow, stackPtr);
    recalculate_lane_geometry();
    debugVerifyStackCache();
}
