#pragma once

#include <string>

#include <hyprland/src/config/shared/complex/ComplexDataTypes.hpp>
#include <hyprland/src/config/values/ConfigValues.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

namespace scroller::plugin_config {

inline constexpr const char*     kDefaultColumnWidth = "2";
inline constexpr Config::INTEGER kDefaultFocusWrap = 0;
inline constexpr Config::INTEGER kDefaultCanvasMap = 1;
#ifdef COLORS_IPC
inline constexpr Config::INTEGER kDefaultFreeColumnBorder = 0xff9e1515;
#endif

// Primary options live under `plugin:hyprCanvas:*`. Legacy `plugin:scroller:*`
// options remain registered as backward-compatible fallbacks.
inline SP<Config::Values::CStringValue> columnDefaultWidthValue;
inline SP<Config::Values::CIntValue>    focusWrapValue;
inline SP<Config::Values::CIntValue>    canvasMapValue;
inline SP<Config::Values::CStringValue> legacyColumnDefaultWidthValue;
inline SP<Config::Values::CIntValue>    legacyFocusWrapValue;
inline SP<Config::Values::CIntValue>    legacyCanvasMapValue;
#ifdef COLORS_IPC
inline SP<Config::Values::CColorValue>  freeColumnBorderValue;
inline SP<Config::Values::CColorValue>  legacyFreeColumnBorderValue;
#endif

namespace detail {

template <typename ConfigValuePtr, typename ValueType>
ValueType resolveOption(const ConfigValuePtr& primary,
                        const ConfigValuePtr& legacy,
                        const ValueType& defaultValue) {
    if (primary) {
        const auto value = primary->value();
        if (value != defaultValue)
            return value;
    }
    if (legacy) {
        const auto value = legacy->value();
        if (value != defaultValue)
            return value;
    }
    return defaultValue;
}

} // namespace detail

inline void registerConfigValues(HANDLE handle) {
    const auto widthDescription = "How many stacks a new stack's span should share the workarea with (1-8), or floating.";
    const auto focusWrapDescription = "Whether scroller focus wraps at lane and stack edges.";
    const auto canvasMapDescription = "Show the canvas map after switches and during overview.";

    columnDefaultWidthValue = Config::Values::makeConfigValue<Config::Values::CStringValue>(
        "plugin:hyprCanvas:column_default_width",
        widthDescription,
        Config::STRING{kDefaultColumnWidth},
        {});
    focusWrapValue = Config::Values::makeConfigValue<Config::Values::CIntValue>(
        "plugin:hyprCanvas:focus_wrap",
        focusWrapDescription,
        kDefaultFocusWrap,
        Config::Values::SIntValueOptions{
            .min = Config::INTEGER{0},
            .max = Config::INTEGER{1},
        });
    canvasMapValue = Config::Values::makeConfigValue<Config::Values::CIntValue>(
        "plugin:hyprCanvas:canvas_map",
        canvasMapDescription,
        kDefaultCanvasMap,
        Config::Values::SIntValueOptions{.min = Config::INTEGER{0}, .max = Config::INTEGER{1}});

    legacyColumnDefaultWidthValue = Config::Values::makeConfigValue<Config::Values::CStringValue>(
        "plugin:scroller:column_default_width",
        widthDescription,
        Config::STRING{kDefaultColumnWidth},
        {});
    legacyFocusWrapValue = Config::Values::makeConfigValue<Config::Values::CIntValue>(
        "plugin:scroller:focus_wrap",
        focusWrapDescription,
        kDefaultFocusWrap,
        Config::Values::SIntValueOptions{
            .min = Config::INTEGER{0},
            .max = Config::INTEGER{1},
        });
    legacyCanvasMapValue = Config::Values::makeConfigValue<Config::Values::CIntValue>(
        "plugin:scroller:canvas_map",
        canvasMapDescription,
        kDefaultCanvasMap,
        Config::Values::SIntValueOptions{.min = Config::INTEGER{0}, .max = Config::INTEGER{1}});

    HyprlandAPI::addConfigValueV2(handle, canvasMapValue);
    HyprlandAPI::addConfigValueV2(handle, columnDefaultWidthValue);
    HyprlandAPI::addConfigValueV2(handle, focusWrapValue);
    HyprlandAPI::addConfigValueV2(handle, legacyCanvasMapValue);
    HyprlandAPI::addConfigValueV2(handle, legacyColumnDefaultWidthValue);
    HyprlandAPI::addConfigValueV2(handle, legacyFocusWrapValue);

#ifdef COLORS_IPC
    const auto freeBorderDescription = "Border color for free-sized scroller columns.";
    freeColumnBorderValue = Config::Values::makeConfigValue<Config::Values::CColorValue>(
        "plugin:hyprCanvas:col.freecolumn_border",
        freeBorderDescription,
        kDefaultFreeColumnBorder,
        {});
    legacyFreeColumnBorderValue = Config::Values::makeConfigValue<Config::Values::CColorValue>(
        "plugin:scroller:col.freecolumn_border",
        freeBorderDescription,
        kDefaultFreeColumnBorder,
        {});
    HyprlandAPI::addConfigValueV2(handle, freeColumnBorderValue);
    HyprlandAPI::addConfigValueV2(handle, legacyFreeColumnBorderValue);
#endif
}

inline std::string columnDefaultWidth() {
    return detail::resolveOption(columnDefaultWidthValue, legacyColumnDefaultWidthValue, std::string{kDefaultColumnWidth});
}

inline bool canvasMap() {
    return detail::resolveOption(canvasMapValue, legacyCanvasMapValue, kDefaultCanvasMap) != 0;
}

inline bool focusWrap() {
    return detail::resolveOption(focusWrapValue, legacyFocusWrapValue, kDefaultFocusWrap) != 0;
}

#ifdef COLORS_IPC
inline Config::CGradientValueData freeColumnBorder() {
    return Config::CGradientValueData(
        CHyprColor(detail::resolveOption(freeColumnBorderValue, legacyFreeColumnBorderValue, kDefaultFreeColumnBorder)));
}
#endif

} // namespace scroller::plugin_config
