#include "layout/karta/workspace_controller_logic.h"

#include <algorithm>
#include <optional>
#include <vector>

#include "overview/navigation/logic.h"

namespace KartaLayoutState {
namespace {

bool is_cardinal(Direction direction) {
    return direction == Direction::Left || direction == Direction::Right ||
           direction == Direction::Up || direction == Direction::Down;
}

const KartaTileNode* find_node(const std::vector<KartaTileNode>& kartas, int kartaId) {
    const auto it = std::find_if(kartas.begin(), kartas.end(), [&](const auto& node) {
        return node.kartaId == kartaId;
    });
    return it == kartas.end() ? nullptr : &*it;
}

} // namespace

KartaNavigationPlan planDirectionalKartaMove(const std::vector<KartaTileNode>& kartas,
                                               int currentKartaId,
                                               Direction direction) {
    if (kartas.empty() || !is_cardinal(direction))
        return {};

    std::vector<OverviewLogic::TargetCandidate> candidates;
    candidates.reserve(kartas.size());
    std::optional<size_t> currentIndex;
    for (size_t index = 0; index < kartas.size(); ++index) {
        const auto& node = kartas[index];
        candidates.push_back({
            .monitorId = 0,
            .box = {
                static_cast<double>(node.tileX) * 128.0,
                static_cast<double>(node.tileY) * 128.0,
                96.0,
                96.0,
            },
        });
        if (node.kartaId == currentKartaId)
            currentIndex = index;
    }

    if (!currentIndex)
        return {};

    if (const auto nextIndex = OverviewLogic::pickTargetIndex(candidates, *currentIndex, direction)) {
        return {
            .action = KartaNavigationAction::SelectExisting,
            .targetKartaId = kartas[*nextIndex].kartaId,
        };
    }

    // A blank provisional page is already the insertion point. Extending it
    // again would leave an empty hole when a later page receives a window.
    if (kartas[*currentIndex].ephemeral)
        return {};

    return {
        .action = KartaNavigationAction::CreateSynthetic,
        .anchorKartaId = currentKartaId,
        .createDirection = direction,
    };
}

KartaNavigationPlan planNumberedKartaMove(const std::vector<KartaTileNode>& kartas,
                                            int currentKartaId,
                                            int number) {
    if (kartas.empty() || number < 1 || number > 10)
        return {};

    auto ordered = kartas;
    std::sort(ordered.begin(), ordered.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.kartaId < rhs.kartaId;
    });

    if (number <= static_cast<int>(ordered.size())) {
        return {
            .action = KartaNavigationAction::SelectExisting,
            .targetKartaId = ordered[static_cast<size_t>(number - 1)].kartaId,
        };
    }

    // Numbered navigation cannot bypass the one-empty-page boundary.
    if (number != static_cast<int>(ordered.size()) + 1)
        return {};

    const auto& anchor = ordered.back();
    const auto* current = find_node(ordered, currentKartaId);
    if (!current || current->ephemeral || anchor.ephemeral)
        return {};

    return {
        .action = KartaNavigationAction::CreateSynthetic,
        .anchorKartaId = anchor.kartaId,
        .createDirection = Direction::Right,
    };
}

} // namespace KartaLayoutState
