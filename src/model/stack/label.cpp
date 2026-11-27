#include "model/stack/label.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <hyprland/src/desktop/view/window/Window.hpp>
#include <hyprland/src/desktop/view/window/WindowMetadata.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/render/WindowRenderPresentation.hpp>
#include <hyprland/src/render/pass/RectPassElement.hpp>
#include <hyprland/src/render/pass/TexPassElement.hpp>

#include "overview/render/style.h"
#include "overview/session/session.h"

namespace CanvasModel {

StackLabelDecoration::StackLabelDecoration(PHLWINDOW window) : IHyprWindowDecoration(window) {
    titleListener = Event::bus()->m_events.window.title.listen([this](PHLWINDOW changed) {
        if (changed == owningWindow())
            updateWindow(changed);
    });
}

SDecorationPositioningInfo StackLabelDecoration::getPositioningInfo() {
    return {.policy = DECORATION_POSITION_ABSOLUTE, .edges = 0, .desiredExtents = {}, .reserved = false};
}

void StackLabelDecoration::configure(DeckLabelEdge nextEdge, double nextSpan) {
    if (edge == nextEdge && exposedSpan == nextSpan)
        return;
    edge = nextEdge;
    exposedSpan = nextSpan;
    damageEntire();
}

void StackLabelDecoration::damageEntire() {
    if (const auto window = owningWindow(); window && g_pHyprRenderer)
        g_pHyprRenderer->damageWindow(window);
}

void StackLabelDecoration::updateWindow(PHLWINDOW window) {
    if (window && window->metadata().title() != cachedTitle) {
        cachedTitle = window->metadata().title();
        texture.reset();
        if (edge != DeckLabelEdge::None)
            damageEntire();
    }
}

void StackLabelDecoration::draw(Render::CRenderContext& ctx, PHLMONITOR monitor, const float& alpha,
                                const Render::SWindowRenderPresentation& presentation) {
    const auto window = owningWindow();
    if (!window || !monitor || edge == DeckLabelEdge::None || Overview::session().active())
        return;

    const bool vertical = edge == DeckLabelEdge::Left || edge == DeckLabelEdge::Right;
    const auto size = window->size(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
    const double thickness = std::min({34.0, exposedSpan - 4.0, (vertical ? size.x : size.y) - 4.0});
    const double length = std::min(420.0, (vertical ? size.y : size.x) - 16.0);
    // Do not let text spill outside tiny strips when a stack contains many cards.
    if (thickness < 12.0 || length < 32.0)
        return;

    const double scale = monitor->m_scale;
    const int fontSize = std::max(1, static_cast<int>(std::round(std::min(18.0, thickness - 10.0) * scale)));
    const int maxWidth = std::max(1, static_cast<int>((length - 24.0) * scale));
    const auto title = window->metadata().title().empty() ? std::string("window") : window->metadata().title();
    if (!texture || title != cachedTitle || maxWidth != cachedWidth || fontSize != cachedFontSize) {
        Hyprgraphics::CTextResource::STextResourceData data;
        data.text = title;
        std::replace(data.text.begin(), data.text.end(), '\n', ' ');
        data.fontSize = fontSize;
        data.color = Hyprgraphics::CColor{Overview::Style::titleText(1.0F).asRGB()};
        data.maxSize = Vector2D(static_cast<double>(maxWidth), std::ceil(thickness * scale));
        data.ellipsize = true;
        data.wrap = false;
        data.weight = 600;
        texture = g_pHyprRenderer->renderText(std::move(data));
        cachedTitle = title;
        cachedWidth = maxWidth;
        cachedFontSize = fontSize;
    }
    if (!texture || texture->m_size.x <= 0 || texture->m_size.y <= 0)
        return;

    const double textScale = std::min(1.0, (thickness - 4.0) * scale / texture->m_size.y);
    const double textW = texture->m_size.x * textScale;
    const double textH = texture->m_size.y * textScale;
    const double labelLength = std::min(length, textW / scale + 24.0);
    auto origin = window->position(Desktop::View::IGeometric::GEOMETRIC_CURRENT)
        + presentation.workspaceOffset + presentation.floatingOffset - monitor->m_position;
    CBox panel;
    if (vertical) {
        panel = {origin.x + (edge == DeckLabelEdge::Right ? size.x - thickness - 2.0 : 2.0),
                 origin.y + 8.0, thickness, labelLength};
    } else {
        panel = {origin.x + 8.0, origin.y + (edge == DeckLabelEdge::Bottom ? size.y - thickness - 2.0 : 2.0),
                 labelLength, thickness};
    }
    panel.scale(scale).round();
    CRectPassElement::SRectData backdrop;
    backdrop.box = panel;
    backdrop.color = Overview::Style::titleBackdrop(1.0F);
    backdrop.round = static_cast<int>(std::min(7.0 * scale, std::min(panel.w, panel.h) * 0.5));
    g_pHyprRenderer->addPassElement(ctx, makeUnique<CRectPassElement>(backdrop));

    CTexPassElement::SRenderData text;
    text.tex = texture;
    text.a = alpha;
    text.blockBlurOptimization = true;
    if (vertical) {
        const auto center = panel.middle();
        text.box = {center.x - textW * 0.5, center.y - textH * 0.5, textW, textH};
        text.box.rot = std::numbers::pi / 2.0;
    } else {
        text.box = {panel.x + 12.0 * scale, panel.y + (panel.h - textH) * 0.5, textW, textH};
    }
    g_pHyprRenderer->addPassElement(ctx, makeUnique<CTexPassElement>(std::move(text)));
}

} // namespace CanvasModel
