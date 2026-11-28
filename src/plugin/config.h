#pragma once

#include <string>

#include <hyprland/src/config/shared/complex/ComplexDataTypes.hpp>
#include <hyprland/src/config/values/ConfigValues.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

namespace karta::plugin_config {

inline constexpr const char*     kDefaultColumnWidth = "2";
inline constexpr Config::INTEGER kDefaultFocusWrap = 0;
inline constexpr Config::INTEGER kDefaultKartaMap = 1;
#ifdef COLORS_IPC
inline constexpr Config::INTEGER kDefaultFreeColumnBorder = 0xff9e1515;
#endif

// Plugin options live under `plugin:karta:*`.
inline SP<Config::Values::CStringValue> columnDefaultWidthValue;
inline SP<Config::Values::CIntValue>    focusWrapValue;
inline SP<Config::Values::CIntValue>    kartaMapValue;
#ifdef COLORS_IPC
inline SP<Config::Values::CColorValue>  freeColumnBorderValue;
#endif

namespace detail {

template <typename ConfigValuePtr, typename ValueType>
ValueType resolveOption(const ConfigValuePtr& option,
                        const ValueType& defaultValue) {
    if (option)
        return option->value();
    return defaultValue;
}

} // namespace detail

inline void registerConfigValues(HANDLE handle) {
    const auto widthDescription = "How many stacks a new stack's span should share the workarea with (1-8), or floating.";
    const auto focusWrapDescription = "Whether karta focus wraps at lane and stack edges.";
    const auto kartaMapDescription = "Show the karta map after switches and during overview.";

    columnDefaultWidthValue = Config::Values::makeConfigValue<Config::Values::CStringValue>(
        "plugin:karta:column_default_width",
        widthDescription,
        Config::STRING{kDefaultColumnWidth},
        {});
    focusWrapValue = Config::Values::makeConfigValue<Config::Values::CIntValue>(
        "plugin:karta:focus_wrap",
        focusWrapDescription,
        kDefaultFocusWrap,
        Config::Values::SIntValueOptions{
            .min = Config::INTEGER{0},
            .max = Config::INTEGER{1},
        });
    kartaMapValue = Config::Values::makeConfigValue<Config::Values::CIntValue>(
        "plugin:karta:karta_map",
        kartaMapDescription,
        kDefaultKartaMap,
        Config::Values::SIntValueOptions{.min = Config::INTEGER{0}, .max = Config::INTEGER{1}});

    HyprlandAPI::addConfigValueV2(handle, kartaMapValue);
    HyprlandAPI::addConfigValueV2(handle, columnDefaultWidthValue);
    HyprlandAPI::addConfigValueV2(handle, focusWrapValue);

#ifdef COLORS_IPC
    const auto freeBorderDescription = "Border color for free-sized karta columns.";
    freeColumnBorderValue = Config::Values::makeConfigValue<Config::Values::CColorValue>(
        "plugin:karta:col.freecolumn_border",
        freeBorderDescription,
        kDefaultFreeColumnBorder,
        {});
    HyprlandAPI::addConfigValueV2(handle, freeColumnBorderValue);
#endif
}

inline std::string columnDefaultWidth() {
    return detail::resolveOption(columnDefaultWidthValue, std::string{kDefaultColumnWidth});
}

inline bool kartaMap() {
    return detail::resolveOption(kartaMapValue, kDefaultKartaMap) != 0;
}

inline bool focusWrap() {
    return detail::resolveOption(focusWrapValue, kDefaultFocusWrap) != 0;
}

#ifdef COLORS_IPC
inline Config::CGradientValueData freeColumnBorder() {
    return Config::CGradientValueData(
        CHyprColor(detail::resolveOption(freeColumnBorderValue, kDefaultFreeColumnBorder)));
}
#endif

} // namespace karta::plugin_config
