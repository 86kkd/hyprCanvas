#pragma once
#include <vector>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprutils/math/Box.hpp>
#include "core/types.h"

namespace CanvasModel::DeckPointer {
struct Card {
    PHLWINDOWREF window;
    Hyprutils::Math::CBox visualBox;
    bool beforeActive = false;
};
void initialize();
void shutdown();
void update(const void* owner, std::vector<Card> cards, Mode mode, const CanvasCore::Box& viewport);
void forget(const void* owner);
} // namespace CanvasModel::DeckPointer
