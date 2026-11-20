#pragma once

#include <cstddef>

#ifndef LUX_APP_VERSION_STR
// Fallback when not compiling via CMake (CMake overrides with project VERSION).
#define LUX_APP_VERSION_STR "1.0.8"
#endif
#define LUX_WIDEN_HELPER(x) L##x
#define LUX_WIDEN(x) LUX_WIDEN_HELPER(x)
#define LUX_APP_VERSION_STRING LUX_WIDEN(LUX_APP_VERSION_STR)

enum class SettingType {
  Bool,
  Int,
  String,
  Enum,
};

enum class SettingGroup {
  Appearance,
  Behavior,
  Themes,
  Animations,
  General,
  Shortcuts,
  Modules,
  Indicators,
  Runtime,
};

struct SettingEnumOption {
  const wchar_t* value;
  const wchar_t* label;
};

struct SettingDef {
  const wchar_t* key;  // flat: L"Modules.QuickLookup"
  SettingType type;
  SettingGroup group;
  const wchar_t* defaultString;  // also used for bool "0"/"1" and int decimal
  int minValue;
  int maxValue;
  const SettingEnumOption* enums;
  size_t enumCount;
};

inline constexpr SettingEnumOption kBackdropMaterialEnums[] = {
    {L"none", L"None"},
    {L"blur", L"Blur"},
    {L"acrylic", L"Acrylic"},
    {L"mica", L"Mica"},
    {L"solid", L"Solid"},
};

inline constexpr SettingEnumOption kAppearanceBackdropEnums[] = {
    {L"none", L"None"},
    {L"blur", L"Blur"},
    {L"acrylic", L"Acrylic"},
    {L"mica", L"Mica"},
    {L"solid", L"Solid"},
    {L"auto", L"Auto (Themes)"},
};

inline constexpr SettingEnumOption kAuraStyleEnums[] = {
    {L"off", L"Off"},
    {L"glow", L"Glow"},
    {L"equalizer", L"Equalizer"},
    {L"rays", L"Rays"},
    {L"pulse", L"Rings"},
    {L"orbit", L"Orbit"},
};

inline constexpr SettingEnumOption kTargetMonitorEnums[] = {
    {L"primary", L"Primary"},
    {L"cursor", L"Cursor monitor"},
    {L"follow", L"Follow active window"},
    {L"1", L"Monitor 1"},
    {L"2", L"Monitor 2"},
    {L"3", L"Monitor 3"},
};

inline constexpr SettingEnumOption kBlacklistModeEnums[] = {
    {L"foreground", L"Foreground"},
    {L"running", L"Running"},
};

inline constexpr SettingEnumOption kExpandModeEnums[] = {
    {L"hover", L"Hover"},
    {L"click", L"Click"},
    {L"both", L"Hover and click"},
};

inline constexpr SettingEnumOption kPositionEnums[] = {
    {L"top-center", L"Top center"},
    {L"top-left", L"Top left"},
    {L"top-right", L"Top right"},
    {L"bottom-center", L"Bottom center"},
    {L"bottom-left", L"Bottom left"},
    {L"bottom-right", L"Bottom right"},
};

inline constexpr SettingEnumOption kSpectrumStyleEnums[] = {
    {L"bars", L"Bars"},
    {L"orb", L"Orb"},
    {L"plasma", L"Plasma"},
    {L"led", L"LED"},
};

inline constexpr SettingEnumOption kShapeStyleEnums[] = {
    {L"pill", L"iPhone pill"},
    {L"w11", L"Windows 11 flyout"},
    {L"notch", L"macOS notch"},
};

inline constexpr SettingEnumOption kThemePresetEnums[] = {
    {L"obsidian", L"Obsidian"},  {L"graphite", L"Graphite"}, {L"slate", L"Slate"},
    {L"nord", L"Nord"},          {L"evergreen", L"Evergreen"}, {L"espresso", L"Espresso"},
    {L"plum", L"Plum"},          {L"porcelain", L"Porcelain"}, {L"custom", L"Custom"},
};

inline constexpr SettingEnumOption kClickThroughModifierEnums[] = {
    {L"ctrl", L"Ctrl"},
    {L"alt", L"Alt"},
    {L"ctrl_or_alt", L"Ctrl or Alt"},
};

inline constexpr SettingEnumOption kProgressStyleEnums[] = {
    {L"slim", L"Slim"},
    {L"bar", L"Bar"},
    {L"wavy", L"Wavy"},
    {L"squiggle", L"Squiggle"},
};

inline constexpr SettingEnumOption kAnimationStyleEnums[] = {
    {L"smooth", L"Smooth"},
    {L"default", L"Default"},
    {L"bouncy", L"Bouncy"},
    {L"snappy", L"Snappy"},
};

inline constexpr SettingEnumOption kAnimationSpeedEnums[] = {
    {L"very-slow", L"Very slow"},
    {L"slow", L"Slow"},
    {L"normal", L"Normal"},
    {L"fast", L"Fast"},
    {L"very-fast", L"Very fast"},
    {L"ultra-fast", L"Ultra fast"},
};

inline constexpr SettingEnumOption kAccentColorModeEnums[] = {
    {L"auto", L"Auto"},
    {L"system", L"System"},
    {L"custom", L"Custom"},
};

inline constexpr SettingEnumOption kContourBorderModeEnums[] = {
    {L"default", L"Default"},
    {L"auto", L"Auto"},
    {L"borderless", L"Borderless"},
};

inline constexpr SettingEnumOption kCalendarAccentEnums[] = {
    {L"accent", L"Accent"},
    {L"system", L"System"},
};

inline constexpr SettingEnumOption kFirstDayOfWeekEnums[] = {
    {L"system", L"System"},
    {L"monday", L"Monday"},
    {L"sunday", L"Sunday"},
};

inline constexpr SettingEnumOption kLanguageEnums[] = {
    {L"uk", L"Ukrainian"}, {L"en", L"English"}, {L"fr", L"Francais"}, {L"it", L"Italiano"},
    {L"tr", L"Turkce"},    {L"zh", L"Chinese"}, {L"pl", L"Polski"},   {L"de", L"Deutsch"},
    {L"ja", L"Japanese"},  {L"ko", L"Korean"},  {L"es", L"Espanol"},  {L"hi", L"Hindi"},
    {L"pt", L"Portuguese"},{L"id", L"Indonesian"}, {L"ru", L"Russian"}, {L"auto", L"Auto"},
};

inline constexpr SettingEnumOption kLookupModifierEnums[] = {
    {L"ctrl_alt", L"Ctrl+Alt"},
    {L"ctrl_shift", L"Ctrl+Shift"},
    {L"win_alt", L"Win+Alt"},
};

inline constexpr SettingEnumOption kHideShowModifierEnums[] = {
    {L"ctrl_alt", L"Ctrl+Alt"},
    {L"ctrl_shift", L"Ctrl+Shift"},
    {L"alt_shift", L"Alt+Shift"},
    {L"win_alt", L"Win+Alt"},
    {L"ctrl_alt_shift", L"Ctrl+Alt+Shift"},
};

inline constexpr SettingEnumOption kClockFormatEnums[] = {
    {L"system", L"System"},
    {L"24h", L"24h (14:30)"},
    {L"24h-sec", L"24h + sec"},
    {L"24h-dot", L"24h dot (14.30)"},
    {L"24h-compact", L"24h compact (1430)"},
    {L"12h", L"12h AM/PM"},
    {L"12h-sec", L"12h + sec"},
    {L"12h-no-ampm", L"12h no AM/PM"},
    {L"12h-dot", L"12h dot"},
    {L"24h-leading", L"24h no leading zero"},
    {L"12h-pad", L"12h padded"},
};

inline constexpr SettingEnumOption kDateFormatEnums[] = {
    {L"digits-md", L"31.10"},
    {L"digits-mdy-short", L"31.10.26"},
    {L"digits-mdy", L"31.10.2026"},
    {L"digits-dmy-slash", L"31/10/2026"},
    {L"digits-ymd", L"2026-10-31"},
    {L"words-d-month", L"31 October"},
    {L"words-d-month-year", L"31 October 2026"},
    {L"words-d-mon", L"31 Oct"},
    {L"words-d-mon-year", L"31 Oct 2026"},
    {L"words-weekday-d-month", L"Friday, 31 October"},
    {L"words-weekday-short", L"Fri, 31 Oct"},
};

// Single source of truth for defaults. LoadSettings must read via SettingsStore
// so every Wh_Get* key used by the island has a schema row (missing → fallback 0).
inline constexpr SettingDef kSettingSchema[] = {
    {L"Appearance.Position", SettingType::Enum, SettingGroup::Appearance, L"top-center", 0, 0,
     kPositionEnums, sizeof(kPositionEnums) / sizeof(kPositionEnums[0])},
    {L"Appearance.SizeScale", SettingType::String, SettingGroup::Appearance, L"1.0", 0, 0, nullptr,
     0},
    {L"Appearance.AutoDpiScale", SettingType::Bool, SettingGroup::Appearance, L"1", 0, 1, nullptr, 0},
    {L"Appearance.OffsetX", SettingType::Int, SettingGroup::Appearance, L"0", -5000, 5000, nullptr,
     0},
    {L"Appearance.OffsetY", SettingType::Int, SettingGroup::Appearance, L"0", -5000, 5000, nullptr,
     0},
    {L"Appearance.SeparateExpandedOffsetY", SettingType::Bool, SettingGroup::Appearance, L"0", 0, 1,
     nullptr, 0},
    {L"Appearance.OffsetYExpanded", SettingType::Int, SettingGroup::Appearance, L"0", -5000, 5000,
     nullptr, 0},
    {L"Appearance.TargetMonitor", SettingType::Enum, SettingGroup::Appearance, L"primary", 0, 0,
     kTargetMonitorEnums, sizeof(kTargetMonitorEnums) / sizeof(kTargetMonitorEnums[0])},
    {L"Appearance.SpectrumStyle", SettingType::Enum, SettingGroup::Appearance, L"bars", 0, 0,
     kSpectrumStyleEnums, sizeof(kSpectrumStyleEnums) / sizeof(kSpectrumStyleEnums[0])},
    {L"Appearance.ProgressStyle", SettingType::Enum, SettingGroup::Appearance, L"slim", 0, 0,
     kProgressStyleEnums, sizeof(kProgressStyleEnums) / sizeof(kProgressStyleEnums[0])},
    // Prefer Themes when Appearance is auto so one Themes row drives blur/acrylic/none.
    {L"Appearance.BackdropMaterial", SettingType::Enum, SettingGroup::Appearance, L"auto", 0, 0,
     kAppearanceBackdropEnums, sizeof(kAppearanceBackdropEnums) / sizeof(kAppearanceBackdropEnums[0])},
    {L"Appearance.UseDirectComposition", SettingType::Bool, SettingGroup::Appearance, L"0", 0, 1,
     nullptr, 0},
    {L"Appearance.ShapeStyle", SettingType::Enum, SettingGroup::Appearance, L"pill", 0, 0,
     kShapeStyleEnums, sizeof(kShapeStyleEnums) / sizeof(kShapeStyleEnums[0])},
    {L"Appearance.BorderMergedMode", SettingType::Bool, SettingGroup::Appearance, L"0", 0, 1,
     nullptr, 0},
    {L"Behavior.AlwaysOnTop", SettingType::Bool, SettingGroup::Behavior, L"1", 0, 1, nullptr, 0},
    {L"Behavior.AutoHideIdleSeconds", SettingType::Int, SettingGroup::Behavior, L"0", -1, 3600,
     nullptr, 0},
    {L"Behavior.ExpandMode", SettingType::Enum, SettingGroup::Behavior, L"both", 0, 0,
     kExpandModeEnums, sizeof(kExpandModeEnums) / sizeof(kExpandModeEnums[0])},
    {L"Behavior.UnhideOnHover", SettingType::Bool, SettingGroup::Behavior, L"1", 0, 1, nullptr, 0},
    {L"Behavior.AllowDragMove", SettingType::Bool, SettingGroup::Behavior, L"1", 0, 1, nullptr, 0},
    {L"Behavior.AutoHideFullscreen", SettingType::Bool, SettingGroup::Behavior, L"1", 0, 1, nullptr,
     0},
    {L"Behavior.WheelStep", SettingType::Int, SettingGroup::Behavior, L"2", 1, 20, nullptr, 0},
    {L"Behavior.AppBlacklist", SettingType::String, SettingGroup::Behavior, L"", 0, 0, nullptr, 0},
    {L"Behavior.BlacklistMode", SettingType::Enum, SettingGroup::Behavior, L"foreground", 0, 0,
     kBlacklistModeEnums, sizeof(kBlacklistModeEnums) / sizeof(kBlacklistModeEnums[0])},
    {L"Behavior.ClickThroughModifier", SettingType::Enum, SettingGroup::Behavior, L"ctrl_or_alt", 0,
     0, kClickThroughModifierEnums,
     sizeof(kClickThroughModifierEnums) / sizeof(kClickThroughModifierEnums[0])},
    {L"Animations.ExpandedMediaTransitions", SettingType::Bool, SettingGroup::Animations, L"0", 0, 1,
     nullptr, 0},
    {L"Animations.PillCoverSpin", SettingType::Bool, SettingGroup::Animations, L"1", 0, 1, nullptr,
     0},
    {L"Animations.TargetFPS", SettingType::String, SettingGroup::Animations, L"auto", 0, 0, nullptr,
     0},
    {L"Animations.AnimationStyle", SettingType::Enum, SettingGroup::Animations, L"smooth", 0, 0,
     kAnimationStyleEnums, sizeof(kAnimationStyleEnums) / sizeof(kAnimationStyleEnums[0])},
    {L"Animations.AnimationSpeed", SettingType::Enum, SettingGroup::Animations, L"normal", 0, 0,
     kAnimationSpeedEnums, sizeof(kAnimationSpeedEnums) / sizeof(kAnimationSpeedEnums[0])},
    {L"Themes.BackdropMaterial", SettingType::Enum, SettingGroup::Themes, L"acrylic", 0, 0,
     kBackdropMaterialEnums, sizeof(kBackdropMaterialEnums) / sizeof(kBackdropMaterialEnums[0])},
    {L"Themes.BackdropTint", SettingType::Int, SettingGroup::Themes, L"55", 0, 100, nullptr, 0},
    {L"Themes.BackdropFillOpacity", SettingType::Int, SettingGroup::Themes, L"45", 0, 100, nullptr,
     0},
    {L"Themes.PillOpacity", SettingType::Int, SettingGroup::Themes, L"96", 35, 100, nullptr, 0},
    {L"Themes.TintIntensity", SettingType::Int, SettingGroup::Themes, L"72", 0, 100, nullptr, 0},
    {L"Themes.TextScale", SettingType::Int, SettingGroup::Themes, L"100", 70, 160, nullptr, 0},
    {L"Themes.AuraStyle", SettingType::Enum, SettingGroup::Themes, L"glow", 0, 0, kAuraStyleEnums,
     sizeof(kAuraStyleEnums) / sizeof(kAuraStyleEnums[0])},
    {L"Themes.AuraIntensity", SettingType::Int, SettingGroup::Themes, L"70", 0, 100, nullptr, 0},
    {L"Themes.AdaptiveTheme", SettingType::Bool, SettingGroup::Themes, L"1", 0, 1, nullptr, 0},
    {L"Themes.ThemePreset", SettingType::Enum, SettingGroup::Themes, L"obsidian", 0, 0,
     kThemePresetEnums, sizeof(kThemePresetEnums) / sizeof(kThemePresetEnums[0])},
    {L"Themes.AccentColorMode", SettingType::Enum, SettingGroup::Themes, L"auto", 0, 0,
     kAccentColorModeEnums, sizeof(kAccentColorModeEnums) / sizeof(kAccentColorModeEnums[0])},
    {L"Themes.CustomAccentHex", SettingType::String, SettingGroup::Themes, L"#4CC9F0", 0, 0, nullptr,
     0},
    {L"Themes.AccentBloom", SettingType::Int, SettingGroup::Themes, L"100", 0, 200, nullptr, 0},
    {L"Themes.MaterialDepth", SettingType::Bool, SettingGroup::Themes, L"1", 0, 1, nullptr, 0},
    {L"Themes.DropShadow", SettingType::Bool, SettingGroup::Themes, L"1", 0, 1, nullptr, 0},
    {L"Themes.ClockAccentGlow", SettingType::Bool, SettingGroup::Themes, L"1", 0, 1, nullptr, 0},
    {L"Themes.ContourBorderMode", SettingType::Enum, SettingGroup::Themes, L"default", 0, 0,
     kContourBorderModeEnums, sizeof(kContourBorderModeEnums) / sizeof(kContourBorderModeEnums[0])},
    {L"Themes.ContourBorderHex", SettingType::String, SettingGroup::Themes, L"#333338", 0, 0,
     nullptr, 0},
    {L"Themes.PillBgColor", SettingType::String, SettingGroup::Themes, L"#08080A", 0, 0, nullptr, 0},
    {L"Themes.TextPrimaryColor", SettingType::String, SettingGroup::Themes, L"#FFFFFF", 0, 0,
     nullptr, 0},
    {L"Themes.TextSecondaryColor", SettingType::String, SettingGroup::Themes, L"#9B9BA5", 0, 0,
     nullptr, 0},
    {L"Themes.FontFamily", SettingType::String, SettingGroup::Themes, L"", 0, 0, nullptr, 0},
    {L"Themes.CalendarAccent", SettingType::Enum, SettingGroup::Themes, L"accent", 0, 0,
     kCalendarAccentEnums, sizeof(kCalendarAccentEnums) / sizeof(kCalendarAccentEnums[0])},
    {L"Indicators.PrivacyDots", SettingType::Bool, SettingGroup::Indicators, L"1", 0, 1, nullptr, 0},
    {L"Indicators.PrivacyDotsMic", SettingType::Bool, SettingGroup::Indicators, L"1", 0, 1, nullptr,
     0},
    {L"Indicators.PrivacyDotsCam", SettingType::Bool, SettingGroup::Indicators, L"1", 0, 1, nullptr,
     0},
    {L"General.Language", SettingType::Enum, SettingGroup::General, L"uk", 0, 0, kLanguageEnums,
     sizeof(kLanguageEnums) / sizeof(kLanguageEnums[0])},
    {L"General.LaunchAtStartup", SettingType::Bool, SettingGroup::General, L"0", 0, 1, nullptr, 0},
    {L"Modules.Media", SettingType::Bool, SettingGroup::Modules, L"1", 0, 1, nullptr, 0},
    {L"Modules.MediaAutoExpand", SettingType::Bool, SettingGroup::Modules, L"0", 0, 1, nullptr, 0},
    {L"Modules.MediaLikeBridge", SettingType::Bool, SettingGroup::Modules, L"1", 0, 1, nullptr, 0},
    {L"Modules.MediaLikeBridgePort", SettingType::Int, SettingGroup::Modules, L"18473", 1024, 65535,
     nullptr, 0},
    {L"Modules.MediaExpandBlocklist", SettingType::String, SettingGroup::Modules, L"", 0, 0, nullptr,
     0},
    {L"Modules.Volume", SettingType::Bool, SettingGroup::Modules, L"1", 0, 1, nullptr, 0},
    {L"Modules.Clipboard", SettingType::Bool, SettingGroup::Modules, L"1", 0, 1, nullptr, 0},
    {L"Modules.Battery", SettingType::Bool, SettingGroup::Modules, L"1", 0, 1, nullptr, 0},
    {L"Modules.Progress", SettingType::Bool, SettingGroup::Modules, L"1", 0, 1, nullptr, 0},
    {L"Modules.StatusCountdownProgress", SettingType::Bool, SettingGroup::Modules, L"0", 0, 1,
     nullptr, 0},
    {L"Modules.CapsLock", SettingType::Bool, SettingGroup::Modules, L"1", 0, 1, nullptr, 0},
    {L"Modules.Weather", SettingType::Bool, SettingGroup::Modules, L"1", 0, 1, nullptr, 0},
    {L"Modules.WeatherCity", SettingType::String, SettingGroup::Modules, L"", 0, 0, nullptr, 0},
    {L"Modules.WeatherFahrenheit", SettingType::Bool, SettingGroup::Modules, L"0", 0, 1, nullptr,
     0},
    {L"Modules.CalendarModule", SettingType::Bool, SettingGroup::Modules, L"1", 0, 1, nullptr, 0},
    {L"Modules.HardwareMonitorModule", SettingType::Bool, SettingGroup::Modules, L"1", 0, 1, nullptr,
     0},
    {L"Modules.ShowMetricText", SettingType::Bool, SettingGroup::Modules, L"1", 0, 1, nullptr, 0},
    {L"Modules.BluetoothIndicator", SettingType::Bool, SettingGroup::Modules, L"1", 0, 1, nullptr,
     0},
    {L"Modules.BluetoothShowBattery", SettingType::Bool, SettingGroup::Modules, L"1", 0, 1, nullptr,
     0},
    {L"Modules.DoNotDisturbIndicator", SettingType::Bool, SettingGroup::Modules, L"1", 0, 1, nullptr,
     0},
    {L"Modules.NotificationRespectDnD", SettingType::Bool, SettingGroup::Modules, L"1", 0, 1,
     nullptr, 0},
    {L"Modules.Notifications", SettingType::Bool, SettingGroup::Modules, L"1", 0, 1, nullptr, 0},
    {L"Modules.MessengerApps", SettingType::String, SettingGroup::Modules,
     L"telegram,discord,whatsapp,viber,slack,teams,signal,skype,messenger,mail,outlook,thunderbird",
     0, 0, nullptr, 0},
    {L"Modules.TimerModule", SettingType::Bool, SettingGroup::Modules, L"1", 0, 1, nullptr, 0},
    {L"Modules.TimerContourProgress", SettingType::Bool, SettingGroup::Modules, L"1", 0, 1, nullptr,
     0},
    {L"Modules.MediaPillClock", SettingType::Bool, SettingGroup::Modules, L"1", 0, 1, nullptr, 0},
    {L"Modules.MediaPillDate", SettingType::Bool, SettingGroup::Modules, L"1", 0, 1, nullptr, 0},
    {L"Modules.ClockFormat", SettingType::Enum, SettingGroup::Modules, L"24h", 0, 0, kClockFormatEnums,
     sizeof(kClockFormatEnums) / sizeof(kClockFormatEnums[0])},
    {L"Modules.DateFormat", SettingType::Enum, SettingGroup::Modules, L"digits-md", 0, 0,
     kDateFormatEnums, sizeof(kDateFormatEnums) / sizeof(kDateFormatEnums[0])},
    {L"Modules.ShowSeconds", SettingType::Bool, SettingGroup::Modules, L"0", 0, 1, nullptr, 0},
    {L"Modules.DateFirst", SettingType::Bool, SettingGroup::Modules, L"0", 0, 1, nullptr, 0},
    {L"Modules.FirstDayOfWeek", SettingType::Enum, SettingGroup::Modules, L"system", 0, 0,
     kFirstDayOfWeekEnums, sizeof(kFirstDayOfWeekEnums) / sizeof(kFirstDayOfWeekEnums[0])},
    {L"Modules.QuickLookup", SettingType::Bool, SettingGroup::Modules, L"1", 0, 1, nullptr, 0},
    {L"Modules.Lyrics", SettingType::Bool, SettingGroup::Modules, L"0", 0, 1, nullptr, 0},
    {L"Modules.LyricsVisualizer", SettingType::Bool, SettingGroup::Modules, L"1", 0, 1, nullptr, 0},
    {L"Modules.KaraokeLyrics", SettingType::Bool, SettingGroup::Modules, L"1", 0, 1, nullptr, 0},
    {L"Modules.KaraokeLetterGlow", SettingType::Bool, SettingGroup::Modules, L"1", 0, 1, nullptr, 0},
    {L"Modules.LyricsNeighborLinesVisible", SettingType::Bool, SettingGroup::Modules, L"0", 0, 1,
     nullptr, 0},
    {L"Modules.LyricsOffsetMs", SettingType::Int, SettingGroup::Modules, L"0", -3000, 3000, nullptr,
     0},
    {L"Modules.LyricsOffsetBluetoothMs", SettingType::Int, SettingGroup::Modules, L"0", -3000, 3000,
     nullptr, 0},
    {L"Modules.LyricsCache", SettingType::Bool, SettingGroup::Modules, L"1", 0, 1, nullptr, 0},
    {L"Modules.LyricsCacheMaxMB", SettingType::Int, SettingGroup::Modules, L"25", 0, 1024, nullptr,
     0},
    {L"Modules.CollapsedLyrics", SettingType::Bool, SettingGroup::Modules, L"0", 0, 1, nullptr, 0},
    {L"Modules.LyricsPinnedFirst", SettingType::Bool, SettingGroup::Modules, L"0", 0, 1, nullptr, 0},
    {L"Modules.MediaTabLyrics", SettingType::Bool, SettingGroup::Modules, L"1", 0, 1, nullptr, 0},
    {L"Shortcuts.LookupModifiers", SettingType::Enum, SettingGroup::Shortcuts, L"ctrl_alt", 0, 0,
     kLookupModifierEnums, sizeof(kLookupModifierEnums) / sizeof(kLookupModifierEnums[0])},
    {L"Shortcuts.LookupKey", SettingType::String, SettingGroup::Shortcuts, L"Space", 0, 0, nullptr,
     0},
    {L"Shortcuts.HideShowHotkeyEnabled", SettingType::Bool, SettingGroup::Shortcuts, L"1", 0, 1,
     nullptr, 0},
    {L"Shortcuts.HideShowModifiers", SettingType::Enum, SettingGroup::Shortcuts, L"ctrl_alt", 0, 0,
     kHideShowModifierEnums, sizeof(kHideShowModifierEnums) / sizeof(kHideShowModifierEnums[0])},
    {L"Shortcuts.HideShowKey", SettingType::String, SettingGroup::Shortcuts, L"D", 0, 0, nullptr,
     0},
    {L"Modules.FileTrayModule", SettingType::Bool, SettingGroup::Modules, L"1", 0, 1, nullptr, 0},
    {L"Modules.FileTrayMaxItems", SettingType::Int, SettingGroup::Modules, L"10", 1, 25, nullptr, 0},
    {L"Modules.FileTrayPersist", SettingType::Bool, SettingGroup::Modules, L"1", 0, 1, nullptr, 0},
    {L"Modules.GameOverlay", SettingType::Bool, SettingGroup::Modules, L"0", 0, 1, nullptr, 0},
    {L"Modules.GameOverlayShowFps", SettingType::Bool, SettingGroup::Modules, L"1", 0, 1, nullptr,
     0},
    {L"Modules.GameOverlayShowCpu", SettingType::Bool, SettingGroup::Modules, L"1", 0, 1, nullptr,
     0},
    {L"Modules.GameOverlayShowGpu", SettingType::Bool, SettingGroup::Modules, L"1", 0, 1, nullptr,
     0},
    {L"Modules.GameOverlayShowRam", SettingType::Bool, SettingGroup::Modules, L"1", 0, 1, nullptr,
     0},
    {L"Modules.GameOverlayShowDisk", SettingType::Bool, SettingGroup::Modules, L"1", 0, 1, nullptr,
     0},
    {L"Modules.GameOverlayCompact", SettingType::Bool, SettingGroup::Modules, L"0", 0, 1, nullptr,
     0},
    {L"app.version", SettingType::String, SettingGroup::General, LUX_APP_VERSION_STRING, 0, 0,
     nullptr, 0},
};

inline constexpr size_t kSettingSchemaCount = sizeof(kSettingSchema) / sizeof(kSettingSchema[0]);
