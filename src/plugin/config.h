#pragma once

#include <string>

#include <hyprland/src/config/shared/complex/ComplexDataTypes.hpp>
#include <hyprland/src/config/values/ConfigValues.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

namespace scroller::plugin_config {

inline SP<Config::Values::CStringValue> columnDefaultWidthValue;
inline SP<Config::Values::CIntValue>    focusWrapValue;
inline SP<Config::Values::CIntValue>    canvasMapValue;
#ifdef COLORS_IPC
inline SP<Config::Values::CColorValue>  freeColumnBorderValue;
#endif

inline void registerConfigValues(HANDLE handle) {
    columnDefaultWidthValue = Config::Values::makeConfigValue<Config::Values::CStringValue>(
        "plugin:scroller:column_default_width",
        "How many stacks a new stack's span should share the workarea with (1-8), or floating.",
        Config::STRING{"2"},
        {});
    focusWrapValue = Config::Values::makeConfigValue<Config::Values::CIntValue>(
        "plugin:scroller:focus_wrap",
        "Whether scroller focus wraps at lane and stack edges.",
        Config::INTEGER{0},
        Config::Values::SIntValueOptions{
            .min = Config::INTEGER{0},
            .max = Config::INTEGER{1},
        });

    canvasMapValue = Config::Values::makeConfigValue<Config::Values::CIntValue>(
        "plugin:scroller:canvas_map", "Show the canvas map after switches and during overview.",
        Config::INTEGER{1}, Config::Values::SIntValueOptions{.min = Config::INTEGER{0}, .max = Config::INTEGER{1}});
    HyprlandAPI::addConfigValueV2(handle, canvasMapValue);
    HyprlandAPI::addConfigValueV2(handle, columnDefaultWidthValue);
    HyprlandAPI::addConfigValueV2(handle, focusWrapValue);

#ifdef COLORS_IPC
    freeColumnBorderValue = Config::Values::makeConfigValue<Config::Values::CColorValue>(
        "plugin:scroller:col.freecolumn_border",
        "Border color for free-sized scroller columns.",
        Config::INTEGER{0xff9e1515},
        {});
    HyprlandAPI::addConfigValueV2(handle, freeColumnBorderValue);
#endif
}

inline std::string columnDefaultWidth() {
    return columnDefaultWidthValue ? columnDefaultWidthValue->value() : "2";
}

inline bool canvasMap() {
    return !canvasMapValue || canvasMapValue->value() != 0;
}

inline bool focusWrap() {
    return focusWrapValue && focusWrapValue->value() != 0;
}

#ifdef COLORS_IPC
inline Config::CGradientValueData freeColumnBorder() {
    return Config::CGradientValueData(CHyprColor(freeColumnBorderValue ? freeColumnBorderValue->value() : 0xff9e1515));
}
#endif

} // namespace scroller::plugin_config
