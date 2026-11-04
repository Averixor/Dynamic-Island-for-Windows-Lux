#include "autostart.h"

#include "logging.h"

#include <string>

namespace {
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kApprovedKey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run";
constexpr wchar_t kValueName[] = L"DynamicIslandLux";

std::wstring ExePath() {
  wchar_t path[MAX_PATH] = {};
  const DWORD n = GetModuleFileNameW(nullptr, path, ARRAYSIZE(path));
  if (n == 0 || n >= ARRAYSIZE(path)) {
    return {};
  }
  return path;
}

// Quoted always — paths with spaces (e.g. "Dynamic Island…") break Run otherwise.
std::wstring QuotedExeCommand() {
  return L"\"" + ExePath() + L"\"";
}

bool ReadApprovedDisabled() {
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kApprovedKey, 0, KEY_READ, &key) != ERROR_SUCCESS) {
    return false;
  }
  BYTE data[12] = {};
  DWORD type = 0;
  DWORD size = sizeof(data);
  const LONG rc = RegQueryValueExW(key, kValueName, nullptr, &type, data, &size);
  RegCloseKey(key);
  if (rc != ERROR_SUCCESS || type != REG_BINARY || size < 1) {
    return false;
  }
  // StartupApproved: low bit clear ⇒ disabled by Task Manager / Settings.
  return (data[0] & 0x01) == 0;
}

bool WriteStartupApproved(bool enabled) {
  HKEY key = nullptr;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, kApprovedKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key,
                      nullptr) != ERROR_SUCCESS) {
    return false;
  }
  // 12-byte binary blob Explorer expects; 0x02 = enabled, 0x03 = disabled.
  BYTE data[12] = {};
  data[0] = enabled ? 0x02 : 0x03;
  const LONG rc =
      RegSetValueExW(key, kValueName, 0, REG_BINARY, data, static_cast<DWORD>(sizeof(data)));
  RegCloseKey(key);
  return rc == ERROR_SUCCESS;
}
}  // namespace

bool AutostartIsEnabled() {
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_READ, &key) != ERROR_SUCCESS) {
    return false;
  }
  wchar_t value[MAX_PATH + 8] = {};
  DWORD type = 0;
  DWORD size = sizeof(value);
  const LONG rc = RegQueryValueExW(key, kValueName, nullptr, &type, reinterpret_cast<LPBYTE>(value),
                                   &size);
  RegCloseKey(key);
  if (rc != ERROR_SUCCESS || type != REG_SZ) {
    return false;
  }
  if (ReadApprovedDisabled()) {
    return false;
  }
  return true;
}

bool AutostartSetEnabled(bool enabled) {
  HKEY key = nullptr;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key,
                      nullptr) != ERROR_SUCCESS) {
    LUX_LOGE(L"Autostart: cannot open HKCU Run key");
    return false;
  }
  LONG rc = ERROR_SUCCESS;
  if (enabled) {
    const std::wstring cmd = QuotedExeCommand();
    rc = RegSetValueExW(key, kValueName, 0, REG_SZ, reinterpret_cast<const BYTE*>(cmd.c_str()),
                        static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t)));
  } else {
    rc = RegDeleteValueW(key, kValueName);
    if (rc == ERROR_FILE_NOT_FOUND) {
      rc = ERROR_SUCCESS;
    }
  }
  RegCloseKey(key);
  if (rc != ERROR_SUCCESS) {
    LUX_LOGE(L"Autostart: RegSet/Delete Run failed (%ld)", rc);
    return false;
  }
  // Keep StartupApproved in sync so Task Manager "Disabled" does not stick after re-enable.
  if (!WriteStartupApproved(enabled)) {
    LUX_LOGW(L"Autostart: StartupApproved\\Run write failed (Run value still updated)");
  }
  return true;
}
