#pragma once

#include <hyprland/src/render/decorations/IHyprWindowDecoration.hpp>
#include <hyprland/src/helpers/signal/Signal.hpp>
#include "core/types.h"

namespace Render { class ITexture; }

namespace ScrollerModel {

enum class DeckLabelEdge { None, Top, Bottom, Left, Right };

// An in-client decoration: it reserves no layout space and is occluded together
// with its window, rather than being a monitor-wide overlay.
class StackLabelDecoration final : public IHyprWindowDecoration {
public:
    explicit StackLabelDecoration(PHLWINDOW window);
    void configure(DeckLabelEdge edge, double exposedSpan);
    SDecorationPositioningInfo getPositioningInfo() override;
    void onPositioningReply(const SDecorationPositioningReply&) override {}
    void draw(Render::CRenderContext&, PHLMONITOR, const float&, const Render::SWindowRenderPresentation&) override;
    eDecorationType getDecorationType() override { return DECORATION_CUSTOM; }
    eDecorationLayer getDecorationLayer() override { return DECORATION_LAYER_OVER; }
    uint64_t getDecorationFlags() override { return DECORATION_NON_SOLID; }
    std::string getDisplayName() override { return "Scroller stack label"; }
    void updateWindow(PHLWINDOW) override;
    void damageEntire() override;
private:
    CHyprSignalListener titleListener;
    DeckLabelEdge edge = DeckLabelEdge::None;
    double exposedSpan = 0.0;
    std::string cachedTitle;
    int cachedWidth = 0;
    int cachedFontSize = 0;
    SP<Render::ITexture> texture;
};

} // namespace ScrollerModel
