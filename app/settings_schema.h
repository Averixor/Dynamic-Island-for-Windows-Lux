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

inline constexpr SettingDef kSettingSchema[] = {
    {L"Appearance.Position", SettingType::String, SettingGroup::Appearance, L"top-center", 0, 0,
     nullptr, 0},
    {L"Themes.BackdropMaterial", SettingType::Enum, SettingGroup::Themes, L"none", 0, 0,
     kBackdropMaterialEnums, sizeof(kBackdropMaterialEnums) / sizeof(kBackdropMaterialEnums[0])},
    {L"Themes.BackdropTint", SettingType::Int, SettingGroup::Themes, L"55", 0, 100, nullptr, 0},
    {L"Themes.BackdropFillOpacity", SettingType::Int, SettingGroup::Themes, L"45", 0, 100, nullptr,
     0},
    {L"General.Language", SettingType::Enum, SettingGroup::General, L"uk", 0, 0, kLanguageEnums,
     sizeof(kLanguageEnums) / sizeof(kLanguageEnums[0])},
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
