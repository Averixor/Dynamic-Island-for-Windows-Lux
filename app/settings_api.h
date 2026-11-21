#pragma once

// Small app-owned accessors shared with the island implementation.
#include "logging.h"
#include "settings_schema.h"
#include "settings_store.h"

#include <windows.h>
#include <shlobj.h>

#include <cstdarg>
#include <cwchar>
#include <new>
#include <string>
#include <utility>

namespace app_settings_detail {

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

}  // namespace app_settings_detail

inline int AppSettingGetInt(PCWSTR name) {
  const wchar_t* schemaDefault = app_settings_detail::SchemaDefault(name);
  const int fallback = (schemaDefault && schemaDefault[0]) ? _wtoi(schemaDefault) : 0;
  return SettingsGetInt(name ? name : L"", fallback);
}

inline PWSTR AppSettingGetString(PCWSTR name) {
  const wchar_t* schemaDefault = app_settings_detail::SchemaDefault(name);
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

inline void AppSettingFreeString(PCWSTR value) {
  delete[] const_cast<PWSTR>(value);
}

inline int AppRuntimeGetInt(PCWSTR name, int defaultValue) {
  return RuntimeGetInt(name ? name : L"", defaultValue);
}

inline void AppRuntimeSetInt(PCWSTR name, int value) {
  RuntimeSetInt(name ? name : L"", value);
  RuntimeSaveDebounced();
}

inline BOOL AppStoragePath(PWSTR buffer, size_t bufferChars) {
  if (!buffer || bufferChars == 0) {
    return FALSE;
  }
  wchar_t appData[MAX_PATH] = {};
  if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, appData))) {
    return FALSE;
  }

  std::wstring root = appData;
  root += L"\\DynamicIslandLux";
  const int rootResult = SHCreateDirectoryExW(nullptr, root.c_str(), nullptr);
  if (rootResult != ERROR_SUCCESS && rootResult != ERROR_ALREADY_EXISTS &&
      rootResult != ERROR_FILE_EXISTS) {
    LuxLog(LuxLogLevel::Warn, L"AppStoragePath: could not create %s (%d)", root.c_str(),
           rootResult);
    return FALSE;
  }

  std::wstring path = root + L"\\storage";
  const int created = SHCreateDirectoryExW(nullptr, path.c_str(), nullptr);
  if (created != ERROR_SUCCESS && created != ERROR_ALREADY_EXISTS &&
      created != ERROR_FILE_EXISTS) {
    LuxLog(LuxLogLevel::Warn, L"AppStoragePath: could not create %s (%d)", path.c_str(), created);
    return FALSE;
  }
  if (path.size() + 1 > bufferChars) {
    return FALSE;
  }
  wcsncpy_s(buffer, bufferChars, path.c_str(), _TRUNCATE);
  return TRUE;
}

inline void AppLog(PCWSTR fmt, ...) {
  wchar_t body[2048] = {};
  va_list args;
  va_start(args, fmt);
  _vsnwprintf_s(body, _TRUNCATE, fmt ? fmt : L"", args);
  va_end(args);
  LuxLog(LuxLogLevel::Info, L"%s", body);
}
