#include "layout/canvas/workspace_controller_logic.h"

#include <algorithm>
#include <optional>
#include <vector>

#include "overview/navigation/logic.h"

namespace CanvasLayoutState {
namespace {

bool is_cardinal(Direction direction) {
    return direction == Direction::Left || direction == Direction::Right ||
           direction == Direction::Up || direction == Direction::Down;
}

const CanvasTileNode* find_node(const std::vector<CanvasTileNode>& canvases, int canvasId) {
    const auto it = std::find_if(canvases.begin(), canvases.end(), [&](const auto& node) {
        return node.canvasId == canvasId;
    });
    return it == canvases.end() ? nullptr : &*it;
}

} // namespace

CanvasNavigationPlan planDirectionalCanvasMove(const std::vector<CanvasTileNode>& canvases,
                                               int currentCanvasId,
                                               Direction direction) {
    if (canvases.empty() || !is_cardinal(direction))
        return {};

    std::vector<OverviewLogic::TargetCandidate> candidates;
    candidates.reserve(canvases.size());
    std::optional<size_t> currentIndex;
    for (size_t index = 0; index < canvases.size(); ++index) {
        const auto& node = canvases[index];
        candidates.push_back({
            .monitorId = 0,
            .box = {
                static_cast<double>(node.tileX) * 128.0,
                static_cast<double>(node.tileY) * 128.0,
                96.0,
                96.0,
            },
        });
        if (node.canvasId == currentCanvasId)
            currentIndex = index;
    }

    if (!currentIndex)
        return {};

    if (const auto nextIndex = OverviewLogic::pickTargetIndex(candidates, *currentIndex, direction)) {
        return {
            .action = CanvasNavigationAction::SelectExisting,
            .targetCanvasId = canvases[*nextIndex].canvasId,
        };
    }

    // A blank provisional page is already the insertion point. Extending it
    // again would leave an empty hole when a later page receives a window.
    if (canvases[*currentIndex].ephemeral)
        return {};

    return {
        .action = CanvasNavigationAction::CreateSynthetic,
        .anchorCanvasId = currentCanvasId,
        .createDirection = direction,
    };
}

CanvasNavigationPlan planNumberedCanvasMove(const std::vector<CanvasTileNode>& canvases,
                                            int currentCanvasId,
                                            int number) {
    if (canvases.empty() || number < 1 || number > 10)
        return {};

    auto ordered = canvases;
    std::sort(ordered.begin(), ordered.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.canvasId < rhs.canvasId;
    });

    if (number <= static_cast<int>(ordered.size())) {
        return {
            .action = CanvasNavigationAction::SelectExisting,
            .targetCanvasId = ordered[static_cast<size_t>(number - 1)].canvasId,
        };
    }

    // Numbered navigation cannot bypass the one-empty-page boundary.
    if (number != static_cast<int>(ordered.size()) + 1)
        return {};

    const auto& anchor = ordered.back();
    const auto* current = find_node(ordered, currentCanvasId);
    if (!current || current->ephemeral || anchor.ephemeral)
        return {};

    return {
        .action = CanvasNavigationAction::CreateSynthetic,
        .anchorCanvasId = anchor.canvasId,
        .createDirection = Direction::Right,
    };
}

} // namespace CanvasLayoutState
