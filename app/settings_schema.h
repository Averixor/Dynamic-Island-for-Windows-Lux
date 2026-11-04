#pragma once

#include <cstddef>

#ifndef LUX_APP_VERSION_STR
#define LUX_APP_VERSION_STR "1.0.0"
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
};

inline constexpr SettingEnumOption kAuraStyleEnums[] = {
    {L"off", L"Off"},
    {L"glow", L"Glow"},
    {L"rays", L"Rays"},
    {L"pulse", L"Rings"},
    {L"orbit", L"Orbit"},
};

inline constexpr SettingEnumOption kLanguageEnums[] = {
    {L"uk", L"Ukrainian"}, {L"en", L"English"}, {L"fr", L"Francais"}, {L"it", L"Italiano"},
    {L"tr", L"Turkce"},    {L"zh", L"Chinese"}, {L"pl", L"Polski"},   {L"de", L"Deutsch"},
    {L"ja", L"Japanese"},  {L"ko", L"Korean"},  {L"es", L"Espanol"},  {L"hi", L"Hindi"},
    {L"pt", L"Portuguese"},{L"id", L"Indonesian"}, {L"ru", L"Russian"}, {L"auto", L"Auto"},
};

// Single source of truth for defaults. Expand to full YAML key set during port.
// LoadSettings in lux_overlay must read ONLY via SettingsStore (schema defaults),
// never hard-coded `?? 1` / `?? L"..."` for settings.
inline constexpr SettingEnumOption kLookupModifierEnums[] = {
    {L"ctrl_alt", L"Ctrl+Alt"},
    {L"ctrl_shift", L"Ctrl+Shift"},
    {L"win_alt", L"Win+Alt"},
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

inline constexpr SettingDef kSettingSchema[] = {
    {L"Appearance.Position", SettingType::String, SettingGroup::Appearance, L"top-center", 0, 0,
     nullptr, 0},
    {L"Behavior.AlwaysOnTop", SettingType::Bool, SettingGroup::Behavior, L"1", 0, 1, nullptr, 0},
    {L"Behavior.ExpandOnHover", SettingType::Bool, SettingGroup::Behavior, L"1", 0, 1, nullptr, 0},
    {L"Behavior.UnhideOnHover", SettingType::Bool, SettingGroup::Behavior, L"1", 0, 1, nullptr, 0},
    {L"Behavior.AllowDragMove", SettingType::Bool, SettingGroup::Behavior, L"1", 0, 1, nullptr, 0},
    {L"Behavior.AutoHideFullscreen", SettingType::Bool, SettingGroup::Behavior, L"1", 0, 1, nullptr,
     0},
    {L"Themes.BackdropMaterial", SettingType::Enum, SettingGroup::Themes, L"none", 0, 0,
     kBackdropMaterialEnums, sizeof(kBackdropMaterialEnums) / sizeof(kBackdropMaterialEnums[0])},
    {L"Themes.BackdropTint", SettingType::Int, SettingGroup::Themes, L"55", 0, 100, nullptr, 0},
    {L"Themes.BackdropFillOpacity", SettingType::Int, SettingGroup::Themes, L"45", 0, 100, nullptr,
     0},
    {L"Themes.AuraStyle", SettingType::Enum, SettingGroup::Themes, L"glow", 0, 0, kAuraStyleEnums,
     sizeof(kAuraStyleEnums) / sizeof(kAuraStyleEnums[0])},
    {L"Themes.AuraIntensity", SettingType::Int, SettingGroup::Themes, L"70", 0, 100, nullptr, 0},
    {L"Themes.AdaptiveTheme", SettingType::Bool, SettingGroup::Themes, L"1", 0, 1, nullptr, 0},
    {L"General.Language", SettingType::Enum, SettingGroup::General, L"uk", 0, 0, kLanguageEnums,
     sizeof(kLanguageEnums) / sizeof(kLanguageEnums[0])},
    {L"Modules.Media", SettingType::Bool, SettingGroup::Modules, L"1", 0, 1, nullptr, 0},
    {L"Modules.Volume", SettingType::Bool, SettingGroup::Modules, L"1", 0, 1, nullptr, 0},
    {L"Modules.Brightness", SettingType::Bool, SettingGroup::Modules, L"1", 0, 1, nullptr, 0},
    {L"Modules.Clipboard", SettingType::Bool, SettingGroup::Modules, L"1", 0, 1, nullptr, 0},
    {L"Modules.Battery", SettingType::Bool, SettingGroup::Modules, L"1", 0, 1, nullptr, 0},
    {L"Modules.Progress", SettingType::Bool, SettingGroup::Modules, L"1", 0, 1, nullptr, 0},
    {L"Modules.CapsLock", SettingType::Bool, SettingGroup::Modules, L"1", 0, 1, nullptr, 0},
    {L"Modules.Weather", SettingType::Bool, SettingGroup::Modules, L"1", 0, 1, nullptr, 0},
    {L"Modules.CalendarModule", SettingType::Bool, SettingGroup::Modules, L"1", 0, 1, nullptr, 0},
    {L"Modules.HardwareMonitorModule", SettingType::Bool, SettingGroup::Modules, L"1", 0, 1, nullptr,
     0},
    {L"Modules.BluetoothIndicator", SettingType::Bool, SettingGroup::Modules, L"1", 0, 1, nullptr,
     0},
    {L"Modules.DoNotDisturbIndicator", SettingType::Bool, SettingGroup::Modules, L"1", 0, 1, nullptr,
     0},
    {L"Modules.TimerModule", SettingType::Bool, SettingGroup::Modules, L"1", 0, 1, nullptr, 0},
    {L"Modules.MediaPillClock", SettingType::Bool, SettingGroup::Modules, L"1", 0, 1, nullptr, 0},
    {L"Modules.MediaPillDate", SettingType::Bool, SettingGroup::Modules, L"1", 0, 1, nullptr, 0},
    {L"Modules.ClockFormat", SettingType::Enum, SettingGroup::Modules, L"24h", 0, 0, kClockFormatEnums,
     sizeof(kClockFormatEnums) / sizeof(kClockFormatEnums[0])},
    {L"Modules.DateFormat", SettingType::Enum, SettingGroup::Modules, L"digits-md", 0, 0,
     kDateFormatEnums, sizeof(kDateFormatEnums) / sizeof(kDateFormatEnums[0])},
    {L"Modules.ShowSeconds", SettingType::Bool, SettingGroup::Modules, L"0", 0, 1, nullptr, 0},
    {L"Modules.DateFirst", SettingType::Bool, SettingGroup::Modules, L"0", 0, 1, nullptr, 0},
    {L"Modules.QuickLookup", SettingType::Bool, SettingGroup::Modules, L"1", 0, 1, nullptr, 0},
    {L"Shortcuts.LookupModifiers", SettingType::Enum, SettingGroup::Shortcuts, L"ctrl_alt", 0, 0,
     kLookupModifierEnums, sizeof(kLookupModifierEnums) / sizeof(kLookupModifierEnums[0])},
    {L"Shortcuts.LookupKey", SettingType::String, SettingGroup::Shortcuts, L"Space", 0, 0, nullptr,
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
