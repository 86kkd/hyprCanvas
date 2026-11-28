#include "model/stack/pointer.h"
#include "model/stack/logic.h"
#include "core/hyprland_runtime.h"
#include <unordered_map>
#include <cmath>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/layout/target/Target.hpp>
#include <hyprland/src/render/WindowRenderPresentation.hpp>

namespace KartaModel::DeckPointer {
namespace {
struct Deck {
    std::vector<Card> cards;
    Mode mode;
    KartaCore::Box viewport;
};
std::unordered_map<const void*, Deck> decks;
CHyprSignalListener mouseMove;
CHyprSignalListener renderPre;
bool updating = false;

void refresh(Deck& deck) {
    std::vector<KartaCore::Box> boxes;
    boxes.reserve(deck.cards.size());
    for (const auto& card : deck.cards) {
        const auto window = card.window.lock();
        if (!window || !window->mapped() || window->isFloating()) {
            boxes.emplace_back();
            continue;
        }
        auto box = window->geometricBox(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
        const auto presentation = window->presentation().renderPresentation();
        box.expand(window->presentation().borderSize());
        box.translate(presentation.workspaceOffset + presentation.floatingOffset);
        boxes.emplace_back(box.x, box.y, box.w, box.h);
    }
    for (size_t i = 0; i < deck.cards.size(); ++i) {
        const auto window = deck.cards[i].window.lock();
        if (!window || !window->mapped() || window->isFloating())
            continue;
        const auto target = window->layoutTarget();
        if (!target)
            continue;
        const auto visible = StackLogic::deck_hit_box(boxes, i, deck.cards[i].beforeActive, deck.mode, deck.viewport);
        // An empty box must not be enlarged into a hit area by border grabbing.
        const auto x = std::ceil(visible.x);
        const auto y = std::ceil(visible.y);
        const auto width = std::floor(visible.x + visible.w) - x;
        const auto height = std::floor(visible.y + visible.h) - y;
        const CBox hitBox = width > 0.0 && height > 0.0
            ? CBox{x, y, width, height} : CBox{-1e9, -1e9, 0.0, 0.0};
        const auto old = target->position();
        if (old.x == hitBox.x && old.y == hitBox.y && old.w == hitBox.w && old.h == hitBox.h)
            continue;
        // Hyprland tests logicalBox for tiled mouse focus, while visualBox
        // controls client size and its native animation. Do not reconfigure the
        // client on every animation frame or change its visual goal here.
        target->setPositionGlobal(Layout::STargetBox{.logicalBox = hitBox, .visualBox = deck.cards[i].visualBox},
                                  Layout::TARGET_UPDATE_NO_CLIENT_CONFIGURE);
    }
}

void refreshAll() {
    if (updating)
        return;
    updating = true;
    for (auto& [owner, deck] : decks)
        refresh(deck);
    updating = false;
}
}

void initialize() {
    mouseMove = Event::bus()->m_events.input.mouse.move.listen([](Vector2D, Event::SCallbackInfo&) { refreshAll(); });
    renderPre = Event::bus()->m_events.render.pre.listen([](PHLMONITOR) { refreshAll(); });
}

void shutdown() {
    mouseMove.reset();
    renderPre.reset();
    decks.clear();
}

void update(const void* owner, std::vector<Card> cards, Mode mode, const KartaCore::Box& viewport) {
    auto& deck = decks.insert_or_assign(owner, Deck{std::move(cards), mode, viewport}).first->second;
    if (!updating) {
        updating = true;
        refresh(deck);
        updating = false;
    }
}

void forget(const void* owner) {
    decks.erase(owner);
}
} // namespace KartaModel::DeckPointer
