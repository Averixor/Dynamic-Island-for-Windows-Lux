#pragma once

// Temporary adapters for porting the Windhawk .wh.cpp body.
// Delete after all call sites use SettingsStore / LuxLog directly.
//
// Covered symbols used by Dynamic Island for Windows Lux.wh.cpp:
//   Wh_GetIntSetting, Wh_GetStringSetting, Wh_FreeStringSetting,
//   Wh_GetIntValue, Wh_SetIntValue, Wh_GetModStoragePath,
//   Wh_Log, Wh_SetFunctionHook
// Entry points Wh_Mod* / WhTool_* stay in the .wh.cpp (gated by LUX_STANDALONE).

#include "logging.h"
#include "settings_schema.h"
#include "settings_store.h"

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

inline int Wh_GetIntSetting(PCWSTR name) {
  return SettingsGetInt(name ? name : L"", 0);
}

// Windhawk allocates; caller MUST Wh_FreeStringSetting (not LocalFree).
inline PWSTR Wh_GetStringSetting(PCWSTR name) {
  const std::wstring value = SettingsGetString(name ? name : L"", L"");
  const size_t chars = value.size() + 1;
  wchar_t* out = new (std::nothrow) wchar_t[chars];
  if (!out) {
    return nullptr;
  }
  wcsncpy_s(out, chars, value.c_str(), _TRUNCATE);
  return out;
}

inline void Wh_FreeStringSetting(PCWSTR value) {
  delete[] const_cast<PWSTR>(value);
}

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
  SHCreateDirectoryExW(nullptr, path.c_str(), nullptr);
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

inline BOOL Wh_SetFunctionHook(void* target, void* detour, void** original) {
  (void)target;
  (void)detour;
  (void)original;
  LuxLog(LuxLogLevel::Warn,
         L"Wh_SetFunctionHook is a no-op in standalone (target=%p detour=%p)", target, detour);
  return FALSE;
}
