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
  // StartupApproved: first byte 0x02/0x03 typically means disabled by Task Manager.
  return (data[0] & 0x01) == 0;
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
    LUX_LOGE(L"Autostart: cannot open Run key");
    return false;
  }
  LONG rc = ERROR_SUCCESS;
  if (enabled) {
    std::wstring cmd = L"\"" + ExePath() + L"\"";
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
    LUX_LOGE(L"Autostart: RegSet/Delete failed (%ld)", rc);
    return false;
  }
  return true;
}
