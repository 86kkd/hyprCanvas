#pragma once

#include <algorithm>
#include <optional>
#include <span>
#include <vector>

#include "core/intrusive_list.h"
#include "core/types.h"

namespace ScrollerModel::StackLogic {

// Visible shares along the stack axis: primary 61.8%, secondary 19.1%,
// all remaining strips 19.1%, shared equally regardless of window count.
inline constexpr double kDeckMainShare = 0.618;
inline constexpr double kDeckSecondaryShare = 0.191;
inline constexpr double kDeckOthersShare = 0.191;
inline double deck_peek(size_t count, double localSpan) {
    return count > 2 ? std::max(0.0, localSpan) * kDeckOthersShare / static_cast<double>(count - 2) : 0.0;
}

// A single rectangular hit area for the exposed top/bottom (or left/right)
// strip. Side slivers are deliberately not extra focus targets. Painter order
// is back to front; use rendered boxes, not animation goals.
inline ScrollerCore::Box deck_hit_box(std::span<const ScrollerCore::Box> boxes, size_t index,
                                      bool beforeActive, Mode mode, const ScrollerCore::Box& viewport) {
    if (index >= boxes.size())
        return {};
    auto box = boxes[index];
    for (size_t i = index + 1; i < boxes.size(); ++i) {
        const auto& front = boxes[i];
        if (front.w <= 0 || front.h <= 0 || box.w <= 0 || box.h <= 0)
            continue;
        if (front.x >= box.x + box.w || front.x + front.w <= box.x ||
            front.y >= box.y + box.h || front.y + front.h <= box.y)
            continue;
        if (mode == Mode::Row) {
            const auto end = box.y + box.h;
            if (beforeActive)
                box.h = std::max(0.0, front.y - box.y);
            else {
                box.y = std::max(box.y, front.y + front.h);
                box.h = std::max(0.0, end - box.y);
            }
        } else {
            const auto end = box.x + box.w;
            if (beforeActive)
                box.w = std::max(0.0, front.x - box.x);
            else {
                box.x = std::max(box.x, front.x + front.w);
                box.w = std::max(0.0, end - box.x);
            }
        }
    }
    const auto right = std::min(box.x + box.w, viewport.x + viewport.w);
    const auto bottom = std::min(box.y + box.h, viewport.y + viewport.h);
    box.x = std::max(box.x, viewport.x);
    box.y = std::max(box.y, viewport.y);
    box.w = std::max(0.0, right - box.x);
    box.h = std::max(0.0, bottom - box.y);
    return box;
}

// Back-to-front card placement in stack-local coordinates. Keep the model's
// normal sizes untouched so leaving the deck restores ordinary scrolling.
struct DeckCard {
    size_t index;
    double offset;
    double span;
    double inset;
    bool narrow = false;
};

inline std::vector<DeckCard> deck_cards(size_t count, size_t activeIndex,
                                        double localSpan, double crossSpan,
                                        std::optional<size_t> previousIndex = std::nullopt) {
    if (count <= 2 || activeIndex >= count || localSpan <= 0.0 || crossSpan <= 0.0)
        return {};

    // A newly created/restored stack has no focus history. Prefer the previous
    // neighbor, or the next neighbor when the first card is active.
    const auto secondaryIndex = previousIndex && *previousIndex < count && *previousIndex != activeIndex
        ? *previousIndex : (activeIndex > 0 ? activeIndex - 1 : activeIndex + 1);
    const auto peek = deck_peek(count, localSpan);
    const auto cardSpan = localSpan * kDeckMainShare;
    const auto insetStep = std::min(12.0, crossSpan * 0.12 / static_cast<double>(count - 1));
    std::vector<DeckCard> ordered;
    ordered.reserve(count);
    double offset = 0.0;
    for (size_t index = 0; index < count; ++index) {
        if (index > 0) {
            // Above the active card expose top edges; below it expose bottom
            // edges. Origins always follow the original list order.
            const auto exposedIndex = index <= activeIndex ? index - 1 : index;
            offset += exposedIndex == secondaryIndex ? localSpan * kDeckSecondaryShare : peek;
        }
        const auto distance = index > activeIndex ? index - activeIndex : activeIndex - index;
        ordered.push_back({index, offset, cardSpan, static_cast<double>(distance) * insetStep, index != activeIndex && index != secondaryIndex});
    }

    // Paint each side from the outside inward, then paint the active card last.
    std::vector<DeckCard> cards;
    cards.reserve(count);
    for (size_t index = 0; index < activeIndex; ++index)
        cards.push_back(ordered[index]);
    for (size_t index = count; index > activeIndex + 1; --index)
        cards.push_back(ordered[index - 1]);
    cards.push_back(ordered[activeIndex]);
    return cards;
}

// When the active node is erased, prefer the next sibling and fall back to the previous one.
template <typename T>
ListNode<T> *next_active_after_removal(ListNode<T> *active, ListNode<T> *last) {
    if (!active)
        return nullptr;

    return active != last ? active->next() : active->prev();
}

// Resolve the target local origin for an align command when it applies on this axis.
inline std::optional<double> aligned_local_position(Direction direction,
                                                    Direction backward,
                                                    Direction forward,
                                                    double localOrigin,
                                                    double localSpan,
                                                    double itemSpan) {
    if (direction == backward)
        return localOrigin;
    if (direction == forward)
        return localOrigin + localSpan - itemSpan;
    if (direction == Direction::Center)
        return localOrigin + 0.5 * (localSpan - itemSpan);

    return std::nullopt;
}

} // namespace ScrollerModel::StackLogic
