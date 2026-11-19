#pragma once

#include <algorithm>
#include <charconv>
#include <optional>
#include <string_view>

namespace ScrollerModel {

// Primary-axis sizing intent for a stack. A sized stack takes `workarea / n`,
// where `n` is how many stacks should fit side by side on the monitor. A free
// stack keeps whatever span it already has (pointer resize, carried geometry).
class StackWidth {
  public:
    static constexpr int kMinVisibleStacks = 1;
    static constexpr int kMaxVisibleStacks = 8;
    static constexpr int kDefaultVisibleStacks = 2;

    static constexpr StackWidth visible(int count) {
        return StackWidth(std::clamp(count, kMinVisibleStacks, kMaxVisibleStacks));
    }
    static constexpr StackWidth free() {
        return StackWidth(0);
    }

    // Snapshot encoding: the visible-stack count, or 0 for a free stack.
    static constexpr StackWidth from_snapshot(int value) {
        if (value == 0)
            return free();
        if (value < kMinVisibleStacks || value > kMaxVisibleStacks)
            return visible(kDefaultVisibleStacks);
        return visible(value);
    }
    constexpr int to_snapshot() const {
        return visibleStacks;
    }

    constexpr bool is_free() const {
        return visibleStacks == 0;
    }
    constexpr int visible_stacks() const {
        return visibleStacks;
    }
    // Span along the lane axis for a workarea of `max`; free stacks report the
    // full workarea and are expected to carry their own span.
    constexpr double extent(double max) const {
        return is_free() ? max : max / static_cast<double>(visibleStacks);
    }

    constexpr bool operator==(const StackWidth &) const = default;

  private:
    explicit constexpr StackWidth(int count) : visibleStacks(count) {}

    int visibleStacks;
};

// Parse a visible-stack count such as "3"; out-of-range or non-numeric
// input yields nothing.
inline std::optional<int> parse_visible_stack_count(std::string_view arg) {
    int value = 0;
    const auto *end = arg.data() + arg.size();
    const auto [ptr, ec] = std::from_chars(arg.data(), end, value);
    if (ec != std::errc() || ptr != end)
        return std::nullopt;
    if (value < StackWidth::kMinVisibleStacks || value > StackWidth::kMaxVisibleStacks)
        return std::nullopt;
    return value;
}

} // namespace ScrollerModel
