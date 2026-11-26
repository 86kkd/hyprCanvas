#pragma once

#include "core/linked_list.h"

namespace ScrollerCore {

// Scale every node in an inclusive range so the combined span matches targetSpan.
template <typename T, typename GetSpanFn, typename SetSpanFn>
bool scale_range_to_span(ListNode<T> *from,
                         ListNode<T> *to,
                         double targetSpan,
                         GetSpanFn &&getSpan,
                         SetSpanFn &&setSpan) {
    if (!from || !to || targetSpan <= 0.0)
        return false;

    double totalSpan = 0.0;
    for (auto *node = from; node != nullptr; node = node->next()) {
        totalSpan += getSpan(node);
        if (node == to)
            break;
    }

    if (totalSpan <= 0.0)
        return false;

    const auto scale = targetSpan / totalSpan;
    for (auto *node = from; node != nullptr; node = node->next()) {
        setSpan(node, getSpan(node) * scale);
        if (node == to)
            break;
    }

    return true;
}

} // namespace ScrollerCore
