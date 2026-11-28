/**
 * @file core.cpp
 * @brief Lane construction, stack ownership, and payload-transfer mechanics.
 *
 * `Lane` is the bridge between karta-level commands and stack-level layout
 * work. A lane owns ordered `Stack` objects, tracks which stack is active, and
 * implements the "take one active window out here, reinsert it there" flows used
 * by move-window and cross-monitor handoff code.
 *
 * Reading guide:
 * - constructors and cache helpers explain lane ownership rules
 * - payload helpers explain how an active window leaves and re-enters a lane
 * - geometry setters describe how a lane reacts when the karta moves/resizes
 */
#include "lane.h"

#include <algorithm>
#include <cassert>

#include <hyprland/src/Compositor.hpp>

#include "core/layout_profile.h"
#include "core/monitor_geometry_runtime.h"
#include "core/window_key.h"

using KartaCore::Box;
using KartaModel::Stack;
using KartaModel::StackWidth;

Lane::Lane(PHLWINDOW window)
    : ephemeral(false), gap(0), mode(Mode::Row), active(nullptr) {
    const auto monitor = KartaCore::HyprlandRuntime::monitorById(window->monitorID());
    if (!monitor)
        return;

    mode = KartaCore::default_mode_for_monitor(monitor);
    update_sizes(monitor);
}

Lane::Lane(PHLMONITOR monitor, Mode laneMode)
    : ephemeral(false), gap(0), mode(laneMode), active(nullptr) {
    if (monitor)
        update_sizes(monitor);
}

Lane::~Lane() = default;

Stack *Lane::getStackForWindow(PHLWINDOW window) const {
    if (!window)
        return nullptr;

    // `stackByWindow` is a cache over the authoritative stack list. Validate a
    // cached owner before trusting it, then fall back to a whole-lane scan.
    const auto key = KartaCore::window_key(window);
    if (auto *cachedStack = stackByWindow.find_valid(key, [&](Stack *owner) {
            return owner && getStackNode(owner) && owner->has_window(window);
        }))
        return cachedStack;

    for (auto col = stacks.first(); col != nullptr; col = col->next()) {
        if (col->data()->has_window(window)) {
            stackByWindow.remember(key, col->data().get());
            return col->data().get();
        }
    }

    return nullptr;
}

Lane::StackNode *Lane::getStackNode(Stack *stack) const {
    if (!stack)
        return nullptr;

    for (auto node = stacks.first(); node != nullptr; node = node->next()) {
        if (node->data().get() == stack)
            return node;
    }

    return nullptr;
}

void Lane::rememberWindowStack(PHLWINDOW window, Stack *stack) {
    if (!window)
        return;

    if (!stack) {
        forgetWindowStack(window);
        return;
    }

    stackByWindow.remember(KartaCore::window_key(window), stack);
}

void Lane::forgetWindowStack(PHLWINDOW window) {
    if (!window)
        return;

    stackByWindow.forget(KartaCore::window_key(window));
}

void Lane::rememberStackWindows(Stack *stack) {
    if (!stack)
        return;

    stackByWindow.remember_owner(stack, [&](auto &&remember) {
        stack->for_each_window([&](PHLWINDOW window) {
            remember(KartaCore::window_key(window));
        });
    });
}

void Lane::forgetStackWindows(Stack *stack) {
    if (!stack)
        return;

    stackByWindow.forget_owner(stack);
}

void Lane::debugVerifyStackCache() const {
#ifndef NDEBUG
    assert(stackByWindow.matches_expected([&](auto &&addExpected) {
        for (auto col = stacks.first(); col != nullptr; col = col->next()) {
            auto *stack = col->data().get();
            stack->for_each_window([&](PHLWINDOW window) {
                addExpected(KartaCore::window_key(window), stack);
            });
        }
    }));
#endif
}

bool Lane::empty() const {
    return stacks.empty();
}

bool Lane::is_single_window_lane() const {
    return stacks.size() == 1 && active && active->data()->size() == 1;
}

Mode Lane::get_mode() const {
    return mode;
}

bool Lane::is_ephemeral() const {
    return ephemeral;
}

void Lane::set_ephemeral(bool value) {
    ephemeral = value;
}

bool Lane::has_window(PHLWINDOW window) const {
    return getStackForWindow(window) != nullptr;
}

std::vector<KartaModel::WindowGeometryEntry> Lane::capture_window_boxes() const {
    std::vector<KartaModel::WindowGeometryEntry> boxes;
    for (auto stackNode = stacks.first(); stackNode != nullptr; stackNode = stackNode->next()) {
        const auto stackBoxes = stackNode->data()->capture_window_boxes(calculate_gap_x(stackNode), gap);
        boxes.insert(boxes.end(), stackBoxes.begin(), stackBoxes.end());
    }
    return boxes;
}

PHLWINDOW Lane::get_active_window() const {
    if (!active)
        return nullptr;

    return active->data()->get_active_window();
}

bool Lane::is_active(PHLWINDOW window) const {
    return get_active_window() == window;
}

size_t Lane::stack_count() const {
    return stacks.size();
}

size_t Lane::active_stack_index() const {
    size_t index = 0;
    for (auto *node = stacks.first(); node != nullptr && node != active; node = node->next())
        ++index;
    return index;
}

size_t Lane::active_stack_window_count() const {
    return active ? active->data()->size() : 0;
}

size_t Lane::active_stack_window_index() const {
    return active ? active->data()->active_index() : 0;
}

ActiveWindowRestorePlan Lane::capture_active_window_restore_plan(Direction direction) const {
    ActiveWindowRestorePlan plan;
    plan.direction = direction;

    if (!active)
        return plan;

    auto *stack = active->data().get();
    if (!stack || stack->size() <= 1)
        return plan;

    // If the active stack still contains siblings after extraction, a failed
    // handoff should put the window back next to those siblings instead of
    // materializing a brand-new stack.
    plan.restoreIntoCurrentStack = true;
    plan.insertBeforeCurrent = stack->active_at_edge(KartaCore::stack_item_backward_direction(mode));
    return plan;
}

ActiveWindowPayload Lane::extract_active_window_payload() {
    if (!active)
        return {};

    // This is the central "move one active window out of the lane" primitive.
    // The returned payload must carry both the model window and enough sizing
    // intent for the destination lane to rebuild an equivalent stack.
    auto *stack = active->data().get();
    ActiveWindowPayload payload;
    // Free-width stacks need to carry their current rendered primary span so a
    // later reinsertion does not collapse back to the lane default preset.
    payload.width = stack->get_width();
    payload.primarySpan = stack_primary_span_for_transfer(stack);
    const auto payloadWindow = stack->get_active_window();
    const auto stackExpanded = stack->expanded();

    payload.window = stack->expel_active(gap);
    if (!payload.window)
        return {};
    payload.fullsize = payload.window->get_fullsize_before_join().value_or(stackExpanded);
    payload.window->set_fullsize_before_join(std::nullopt);

    forgetWindowStack(payloadWindow);

    if (stack->size() == 0) {
        // If the extracted window was the last member of its stack, the lane no
        // longer owns that stack at all. Remove the empty stack immediately so
        // later relayout code only sees live stacks.
        auto *emptyNode = active;
        active = emptyNode == stacks.last() ? emptyNode->prev() : emptyNode->next();
        forgetStackWindows(stack);
        stacks.erase(emptyNode);
        debugVerifyStackCache();
        return payload;
    }

    stack->fit_windows_to_span(calculate_gap_x(active), gap, max);
    debugVerifyStackCache();
    return payload;
}

double Lane::stack_primary_span_for_transfer(const Stack *stack) const {
    if (!stack)
        return 0.0;

    // Preset-width stacks can recompute their span from the destination lane's
    // bounds. Only free-width stacks need to carry an explicit axis span.
    if (!stack->get_width().is_free())
        return mode == Mode::Column ? max.h : max.w;

    return mode == Mode::Column ? stack->get_geom_h() : stack->get_geom_w();
}

std::unique_ptr<Stack> Lane::create_stack_from_payload(ActiveWindowPayload payload) {
    if (!payload)
        return nullptr;

    // Rebuild a single-window stack in this lane's orientation. The payload is
    // already detached from the source lane, so from here on the destination
    // lane becomes the sole owner of the transferred model window.
    auto window = payload.release_window();
    if (!window)
        return nullptr;

    window->set_geom_h(mode == Mode::Column ? max.w : max.h);
    window->set_geom_y(mode == Mode::Column ? max.x : max.y);

    // `primarySpan` refers to the lane axis: width in row mode, height in
    // column mode. Keep the name axis-neutral so transfer code reads the same
    // in both orientations.
    const auto targetPrimarySpan =
        payload.width.is_free() && payload.primarySpan > 0.0
            ? std::min(payload.primarySpan, mode == Mode::Column ? max.h : max.w)
            : (mode == Mode::Column ? max.h : max.w);
    return std::make_unique<Stack>(std::move(window),
                                   payload.width,
                                   mode == Mode::Column ? max.w : targetPrimarySpan,
                                   mode == Mode::Column ? targetPrimarySpan : max.h,
                                   mode);
}

void Lane::position_stack_relative_to_reference(Stack *stack, const Stack *reference, Direction direction) {
    if (!stack || !reference)
        return;

    const auto backward = KartaCore::local_item_backward_direction(mode);
    if (mode == Mode::Column) {
        const auto y = direction == backward
            ? reference->get_geom_y() - stack->get_geom_h()
            : reference->get_geom_y() + reference->get_geom_h();
        stack->set_geom_pos(max.x, y);
        return;
    }

    const auto x = direction == backward
        ? reference->get_geom_x() - stack->get_geom_w()
        : reference->get_geom_x() + reference->get_geom_w();
    stack->set_geom_pos(x, max.y);
}

void Lane::insert_window_payload(ActiveWindowPayload payload, Direction direction) {
    if (!payload)
        return;

    // Fullsize destinations accept incoming windows into the same deck so the
    // expanded flag and restore geometry stay on the original stack.
    if (active && active->data()->expanded()) {
        payload.window->set_fullsize_before_join(payload.fullsize);
        auto window = payload.release_window();
        const auto compositorWindow = window ? window->ptr().lock() : nullptr;
        if (!window)
            return;
        active->data()->admit_window(std::move(window));
        rememberWindowStack(compositorWindow, active->data().get());
        recalculate_lane_geometry();
        debugVerifyStackCache();
        return;
    }
    const bool singleWindowLane = stacks.size() == 1 && stacks.first()->data()->size() == 1;
    if (singleWindowLane)
        stacks.first()->data()->update_width(KartaModel::global_stack_width(), max.w, max.h);

    auto stack = create_stack_from_payload(std::move(payload));
    if (!stack)
        return;
    auto *stackPtr = stack.get();
    const auto compositorWindow = stackPtr->get_active_window();
    if (singleWindowLane)
        stackPtr->update_width(KartaModel::global_stack_width(), max.w, max.h);
    stackPtr->set_geom_pos(max.x, max.y);
    if (!active) {
        stacks.push_back(std::move(stack));
        active = stacks.last();
        rememberWindowStack(compositorWindow, stackPtr);
        recalculate_lane_geometry();
        debugVerifyStackCache();
        return;
    }

    // Insert relative to the current active stack, then repair widths when row
    // mode would otherwise overflow the visible lane width.
    auto *current = active;
    auto *currentStack = current->data().get();

    auto inserted = stacks.emplace_after(current, std::move(stack));
    if (direction == Direction::Left || direction == Direction::Up || direction == Direction::Begin)
        stacks.move_before(current, inserted);

    if (mode == Mode::Row) {
        const auto currentWidth = currentStack->get_geom_w();
        const auto insertedWidth = stackPtr->get_geom_w();
        const auto totalWidth = currentWidth + insertedWidth;
        if (totalWidth <= max.w || currentWidth <= 0.0 || insertedWidth <= 0.0) {
            active = inserted;
            rememberWindowStack(compositorWindow, stackPtr);
            recalculate_lane_geometry();
            debugVerifyStackCache();
            return;
        }

        // Two free-width stacks can easily overflow a row lane. Scale both
        // widths down proportionally so insertion preserves relative intent but
        // still fits the lane box.
        const auto scale = max.w / totalWidth;
        currentStack->set_width_free();
        currentStack->set_geom_w(currentWidth * scale);
        stackPtr->set_width_free();
        stackPtr->set_geom_w(insertedWidth * scale);
    }

    active = inserted;
    rememberWindowStack(compositorWindow, stackPtr);
    recalculate_lane_geometry();
    debugVerifyStackCache();
}

void Lane::restore_active_window_payload(ActiveWindowPayload payload, const ActiveWindowRestorePlan &plan) {
    if (!payload)
        return;

    // Failed handoff fallback: either rebuild a standalone stack or put the
    // window back into the original active stack, depending on what was
    // captured before extraction.
    if (!plan.restoreIntoCurrentStack || !active || !active->data()) {
        insert_window_payload(std::move(payload), plan.direction);
        return;
    }

    auto *stack = active->data().get();
    payload.window->set_fullsize_before_join(payload.fullsize);
    auto window = payload.release_window();
    const auto compositorWindow = window ? window->ptr().lock() : nullptr;

    stack->restore_window(std::move(window), plan.insertBeforeCurrent);
    if (compositorWindow)
        rememberWindowStack(compositorWindow, stack);

    recalculate_lane_geometry();
    debugVerifyStackCache();
}

void Lane::set_karta_geometry(const Box &full_box, const Box &max_box, int gap_size) {
    // Lanes store stack-local positions relative to the karta workarea. If the
    // workarea origin shifts, every owned stack must shift by the same delta so
    // their local coordinates remain visually stable after relayout.
    const auto previousLocalOrigin = mode == Mode::Column ? max.x : max.y;
    const auto nextLocalOrigin = mode == Mode::Column ? max_box.x : max_box.y;
    const auto localDelta = nextLocalOrigin - previousLocalOrigin;
    if (localDelta != 0.0) {
        for (auto stack = stacks.first(); stack != nullptr; stack = stack->next())
            stack->data()->shift_local_geometry(localDelta);
    }

    full = full_box;
    max = max_box;
    gap = gap_size;
}

void Lane::set_restored_karta_geometry(const Box &full_box, const Box &max_box, int gap_size) {
    full = full_box;
    max = max_box;
    gap = gap_size;
}

void Lane::append_restored_stack(std::unique_ptr<Stack> stack) {
    if (!stack)
        return;

    auto *stackPtr = stack.get();
    stacks.push_back(std::move(stack));
    rememberStackWindows(stackPtr);
    if (!active)
        active = stacks.first();
    debugVerifyStackCache();
}

void Lane::set_active_stack_by_index(size_t index) {
    auto *node = stacks.first();
    for (size_t i = 0; node != nullptr && i < index; ++i)
        node = node->next();

    active = node ? node : stacks.first();
}

KartaLayoutSnapshot::LaneSnapshot Lane::capture_snapshot() const {
    KartaLayoutSnapshot::LaneSnapshot snapshot;
    snapshot.mode = static_cast<int>(mode);
    snapshot.ephemeral = ephemeral;

    size_t index = 0;
    for (auto node = stacks.first(); node != nullptr; node = node->next(), ++index) {
        if (node == active)
            snapshot.activeStackIndex = index;
        snapshot.stacks.push_back(node->data()->capture_snapshot());
    }

    return snapshot;
}
