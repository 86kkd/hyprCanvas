#include "layout_snapshot.h"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace ScrollerSnapshot {
namespace {

// LANE/STACK lines keep a column for the removed Auto/Lazy reorder policy so
// the format stays readable by older builds. Its value is written as Auto and
// ignored on read.
constexpr int kRetiredReorderToken = 0;

// Before v6 the STACK width column held the OneThird/OneHalf/TwoThirds/Free
// enum. Two thirds has no whole visible-stack count, so it becomes a free
// stack that keeps its stored span.
int visible_stack_width_from_legacy(int legacyWidth) {
    switch (legacyWidth) {
    case 0:
        return 3;
    case 1:
        return 2;
    case 2:
    case 4:
        return 0;
    default:
        return 2;
    }
}

template <typename T>
bool parse_token(std::string_view token, T &out);

template <>
bool parse_token<unsigned long long>(std::string_view token, unsigned long long &out) {
    const auto *begin = token.data();
    const auto *end = token.data() + token.size();
    auto [ptr, ec] = std::from_chars(begin, end, out);
    return ec == std::errc{} && ptr == end;
}

bool parse_size_token(std::string_view token, size_t &out) {
    unsigned long long value = 0;
    if (!parse_token(token, value))
        return false;
    out = static_cast<size_t>(value);
    return true;
}

bool parse_key_token(std::string_view token, uintptr_t &out) {
    unsigned long long value = 0;
    if (!parse_token(token, value))
        return false;
    out = static_cast<uintptr_t>(value);
    return true;
}

template <>
bool parse_token<int>(std::string_view token, int &out) {
    const auto *begin = token.data();
    const auto *end = token.data() + token.size();
    auto [ptr, ec] = std::from_chars(begin, end, out);
    return ec == std::errc{} && ptr == end;
}

template <>
bool parse_token<double>(std::string_view token, double &out) {
    std::string owned(token);
    char *end = nullptr;
    out = std::strtod(owned.c_str(), &end);
    return end == owned.c_str() + owned.size();
}

bool parse_bool_token(std::string_view token, bool &out) {
    int value = 0;
    if (!parse_token(token, value))
        return false;
    if (value != 0 && value != 1)
        return false;
    out = value != 0;
    return true;
}

std::vector<std::string_view> split_tokens(std::string_view line) {
    std::vector<std::string_view> tokens;
    size_t start = 0;
    while (start < line.size()) {
        const auto first = line.find_first_not_of(" \t\r", start);
        if (first == std::string_view::npos)
            break;
        const auto last = line.find_first_of(" \t\r", first);
        if (last == std::string_view::npos) {
            tokens.emplace_back(line.substr(first));
            break;
        }
        tokens.emplace_back(line.substr(first, last - first));
        start = last + 1;
    }
    return tokens;
}

void append_box(std::ostringstream &out, const ScrollerCore::Box &box) {
    out << box.x << ' ' << box.y << ' ' << box.w << ' ' << box.h;
}

bool parse_box(const std::vector<std::string_view> &tokens, size_t index, ScrollerCore::Box &box) {
    return index + 3 < tokens.size() &&
           parse_token(tokens[index + 0], box.x) &&
           parse_token(tokens[index + 1], box.y) &&
           parse_token(tokens[index + 2], box.w) &&
           parse_token(tokens[index + 3], box.h);
}

} // namespace

std::string serialize_repository(const RepositorySnapshot &snapshots) {
    std::ostringstream out;
    out << "VERSION " << kFormatVersion << '\n';

    std::vector<int> workspaceIds;
    workspaceIds.reserve(snapshots.size());
    for (const auto &[workspaceId, _] : snapshots)
        workspaceIds.push_back(workspaceId);
    std::sort(workspaceIds.begin(), workspaceIds.end());

    for (const auto workspaceId : workspaceIds) {
        const auto it = snapshots.find(workspaceId);
        if (it == snapshots.end())
            continue;

        const auto &canvas = it->second;
        out << "WORKSPACE " << canvas.workspaceId << ' ' << canvas.activeLaneIndex << ' '
            << canvas.lanes.size() << ' ' << canvas.version << '\n';
        for (const auto &lane : canvas.lanes) {
            out << "LANE " << lane.mode << ' ' << kRetiredReorderToken << ' ' << (lane.ephemeral ? 1 : 0) << ' '
                << lane.activeStackIndex << ' ' << lane.stacks.size() << '\n';
            for (const auto &stack : lane.stacks) {
                out << "STACK " << stack.width << ' ' << kRetiredReorderToken << ' '
                    << (stack.fullscreened ? 1 : 0) << ' ' << (stack.maximized ? 1 : 0) << ' ';
                append_box(out, stack.geom);
                out << ' ';
                append_box(out, stack.memGeom);
                out << ' ' << stack.activeWindowKey << ' ' << stack.windows.size() << '\n';
                for (const auto &window : stack.windows) {
                    out << "WINDOW " << window.key << ' ' << window.heightMode << ' '
                        << window.geomY << ' ' << window.geomH << ' '
                        << window.memY << ' ' << window.memH << '\n';
                }
            }
        }
    }

    return out.str();
}

std::optional<RepositorySnapshot> deserialize_repository(std::string_view data) {
    RepositorySnapshot snapshots;
    std::istringstream input{std::string(data)};
    std::string line;

    bool sawVersion = false;
    int formatVersion = 0;
    CanvasSnapshot *currentCanvas = nullptr;
    LaneSnapshot *currentLane = nullptr;
    StackSnapshot *currentStack = nullptr;
    int retiredReorder = 0;

    while (std::getline(input, line)) {
        const auto tokens = split_tokens(line);
        if (tokens.empty())
            continue;

        if (tokens[0] == "VERSION") {
            int version = 0;
            if (tokens.size() != 2 ||
                !parse_token(tokens[1], version) ||
                (version != kLegacyFormatVersion &&
                 version != kGridFormatVersion &&
                 version != kGridModeFormatVersion &&
                 version != kFullscreenFormatVersion &&
                 version != kStableWindowKeyFormatVersion &&
                 version != kFormatVersion))
                return std::nullopt;
            formatVersion = version;
            sawVersion = true;
            continue;
        }

        if (!sawVersion)
            return std::nullopt;

        if (tokens[0] == "WORKSPACE") {
            CanvasSnapshot canvas;
            size_t laneCount = 0;
            if ((tokens.size() != 4 && tokens.size() != 5) ||
                !parse_token(tokens[1], canvas.workspaceId) ||
                !parse_size_token(tokens[2], canvas.activeLaneIndex) ||
                !parse_size_token(tokens[3], laneCount))
                return std::nullopt;
            if (tokens.size() == 5) {
                if (formatVersion < kStableWindowKeyFormatVersion ||
                    !parse_token(tokens[4], canvas.version) ||
                    canvas.version < kLegacyFormatVersion ||
                    canvas.version > kFormatVersion)
                    return std::nullopt;
            } else {
                canvas.version = formatVersion;
            }
            canvas.lanes.reserve(laneCount);
            currentCanvas = &snapshots[canvas.workspaceId];
            *currentCanvas = std::move(canvas);
            currentLane = nullptr;
            currentStack = nullptr;
            continue;
        }

        if (tokens[0] == "LANE") {
            if (!currentCanvas)
                return std::nullopt;

            LaneSnapshot lane;
            size_t stackCount = 0;
            if (tokens.size() != 6 ||
                !parse_token(tokens[1], lane.mode) ||
                !parse_token(tokens[2], retiredReorder) ||
                !parse_bool_token(tokens[3], lane.ephemeral) ||
                !parse_size_token(tokens[4], lane.activeStackIndex) ||
                !parse_size_token(tokens[5], stackCount))
                return std::nullopt;
            lane.stacks.reserve(stackCount);
            currentCanvas->lanes.push_back(std::move(lane));
            currentLane = &currentCanvas->lanes.back();
            currentStack = nullptr;
            continue;
        }

        if (tokens[0] == "STACK") {
            if (!currentLane)
                return std::nullopt;

            StackSnapshot stack;
            size_t windowCount = 0;
            if (tokens.size() != 15 ||
                !parse_token(tokens[1], stack.width) ||
                !parse_token(tokens[2], retiredReorder) ||
                !parse_bool_token(tokens[3], stack.fullscreened) ||
                !parse_bool_token(tokens[4], stack.maximized) ||
                !parse_box(tokens, 5, stack.geom) ||
                !parse_box(tokens, 9, stack.memGeom) ||
                !parse_key_token(tokens[13], stack.activeWindowKey) ||
                !parse_size_token(tokens[14], windowCount))
                return std::nullopt;
            if (formatVersion < kVisibleStackWidthFormatVersion)
                stack.width = visible_stack_width_from_legacy(stack.width);
            stack.windows.reserve(windowCount);
            currentLane->stacks.push_back(std::move(stack));
            currentStack = &currentLane->stacks.back();
            continue;
        }

        if (tokens[0] == "WINDOW") {
            if (!currentStack)
                return std::nullopt;

            WindowSnapshot window;
            if (tokens.size() != 7 ||
                !parse_key_token(tokens[1], window.key) ||
                !parse_token(tokens[2], window.heightMode) ||
                !parse_token(tokens[3], window.geomY) ||
                !parse_token(tokens[4], window.geomH) ||
                !parse_token(tokens[5], window.memY) ||
                !parse_token(tokens[6], window.memH))
                return std::nullopt;
            currentStack->windows.push_back(std::move(window));
            continue;
        }

        // Older files may still carry state from the removed scrollergrid
        // layout. Accept those lines so lane state from the same file restores.
        if (tokens[0] == "GRID" || tokens[0] == "GRIDITEM") {
            if (!currentCanvas || formatVersion < kGridFormatVersion)
                return std::nullopt;
            currentLane = nullptr;
            currentStack = nullptr;
            continue;
        }

        return std::nullopt;
    }

    if (!sawVersion)
        return std::nullopt;

    return snapshots;
}

} // namespace ScrollerSnapshot
