#pragma once

// Standalone shims for the Windhawk symbols used by
// `Dynamic Island for Windows Lux.wh.cpp`. Prefer SettingsStore / LuxLog at new
// call sites; keep these wrappers so the archive TU stays portable.
//
// Covered:
//   Wh_GetIntSetting, Wh_GetStringSetting, Wh_FreeStringSetting,
//   Wh_GetIntValue, Wh_SetIntValue, Wh_GetModStoragePath,
//   Wh_Log, Wh_SetFunctionHook
// Wh_Mod* / WhTool_* stay in the .wh.cpp (gated by LUX_STANDALONE).

#include "logging.h"
#include "settings_schema.h"
#include "settings_store.h"

#include <windows.h>
#include <shlobj.h>

#include <cstdarg>
#include <cwchar>
#include <new>
#include <string>

#ifndef WH_MOD_ID
#define WH_MOD_ID L"dynamic-island-for-windows-lux"
#endif

#ifndef WH_MOD_VERSION
#define WH_MOD_VERSION LUX_APP_VERSION_STRING
#endif

namespace windhawk_compat_detail {

inline const wchar_t* SchemaDefault(PCWSTR name) {
  if (!name || !name[0]) {
    return nullptr;
  }
  for (const SettingDef& def : kSettingSchema) {
    if (wcscmp(def.key, name) == 0) {
      return def.defaultString;
    }
  }
  return nullptr;
}

}  // namespace windhawk_compat_detail

// User settings.json. Falls back to schema default (not hard-coded 0) so a
// missing key never silently disables a feature that defaults to on.
inline int Wh_GetIntSetting(PCWSTR name) {
  const wchar_t* schemaDefault = windhawk_compat_detail::SchemaDefault(name);
  const int fallback = (schemaDefault && schemaDefault[0]) ? _wtoi(schemaDefault) : 0;
  return SettingsGetInt(name ? name : L"", fallback);
}

// Windhawk allocates; caller MUST Wh_FreeStringSetting (not LocalFree / delete).
inline PWSTR Wh_GetStringSetting(PCWSTR name) {
  const wchar_t* schemaDefault = windhawk_compat_detail::SchemaDefault(name);
  const std::wstring value =
      SettingsGetString(name ? name : L"", schemaDefault ? schemaDefault : L"");
  const size_t chars = value.size() + 1;
  wchar_t* out = new (std::nothrow) wchar_t[chars];
  if (!out) {
    return nullptr;
  }
  wcsncpy_s(out, chars, value.c_str(), _TRUNCATE);
  return out;
}

inline void Wh_FreeStringSetting(PCWSTR value) {
  // Matches Windhawk null-tolerant free; our allocator is operator new[].
  delete[] const_cast<PWSTR>(value);
}

// Ephemeral runtime.json (overrides, drag offsets, pins).
inline int Wh_GetIntValue(PCWSTR name, int defaultValue) {
  return RuntimeGetInt(name ? name : L"", defaultValue);
}

inline void Wh_SetIntValue(PCWSTR name, int value) {
  RuntimeSetInt(name ? name : L"", value);
  RuntimeSaveDebounced();
}

inline BOOL Wh_GetModStoragePath(PWSTR buffer, size_t bufferChars) {
  if (!buffer || bufferChars == 0) {
    return FALSE;
  }
  wchar_t base[MAX_PATH] = {};
  if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, base))) {
    return FALSE;
  }
  std::wstring path = base;
  path += L"\\DynamicIslandLux\\modstorage";
  const int created = SHCreateDirectoryExW(nullptr, path.c_str(), nullptr);
  if (created != ERROR_SUCCESS && created != ERROR_ALREADY_EXISTS &&
      created != ERROR_FILE_EXISTS) {
    LuxLog(LuxLogLevel::Warn, L"Wh_GetModStoragePath: create failed (%d) for %s", created,
           path.c_str());
    return FALSE;
  }
  if (path.size() + 1 > bufferChars) {
    return FALSE;
  }
  wcsncpy_s(buffer, bufferChars, path.c_str(), _TRUNCATE);
  return TRUE;
}

inline void Wh_Log(PCWSTR fmt, ...) {
  wchar_t body[2048] = {};
  va_list args;
  va_start(args, fmt);
  _vsnwprintf_s(body, _TRUNCATE, fmt ? fmt : L"", args);
  va_end(args);
  LuxLog(LuxLogLevel::Info, L"%s", body);
}

// Standalone never installs Windhawk hooks (tool-mod path is #ifndef LUX_STANDALONE).
inline BOOL Wh_SetFunctionHook(void* target, void* detour, void** original) {
  (void)original;
  static bool logged = false;
  if (!logged) {
    logged = true;
    LuxLog(LuxLogLevel::Debug,
           L"Wh_SetFunctionHook is a no-op in standalone (target=%p detour=%p)", target, detour);
  }
  return FALSE;
}
