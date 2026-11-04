#include "menu_theme.h"

#include <windows.h>

namespace MenuTheme {
namespace {

enum PreferredAppMode : int {
  kDefault = 0,
  kAllowDark = 1,
  kForceDark = 2,
  kForceLight = 3,
};

using SetPreferredAppModeFn = PreferredAppMode(WINAPI*)(PreferredAppMode);
using FlushMenuThemesFn = void(WINAPI*)();

SetPreferredAppModeFn g_setPreferredAppMode = nullptr;
FlushMenuThemesFn g_flushMenuThemes = nullptr;
bool g_resolved = false;

bool AppsUseLightTheme() {
  DWORD value = 1;
  DWORD size = sizeof(value);
  const LSTATUS st =
      RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                   L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size);
  if (st != ERROR_SUCCESS) {
    return true;
  }
  return value != 0;
}

void ResolveApis() {
  if (g_resolved) {
    return;
  }
  g_resolved = true;
  HMODULE ux = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
  if (!ux) {
    return;
  }
  // Undocumented ordinals (Win10 1903+ / Win11): 135 SetPreferredAppMode, 136 FlushMenuThemes.
  g_setPreferredAppMode =
      reinterpret_cast<SetPreferredAppModeFn>(GetProcAddress(ux, MAKEINTRESOURCEA(135)));
  g_flushMenuThemes = reinterpret_cast<FlushMenuThemesFn>(GetProcAddress(ux, MAKEINTRESOURCEA(136)));
}

void ApplyPreferredMode() {
  ResolveApis();
  if (!g_setPreferredAppMode) {
    return;
  }
  // ForceDark / ForceLight so classic popup menus match the OS app theme reliably.
  g_setPreferredAppMode(AppsUseLightTheme() ? kForceLight : kForceDark);
  if (g_flushMenuThemes) {
    g_flushMenuThemes();
  }
}

}  // namespace

void EnableSystemMenus() {
  ApplyPreferredMode();
}

void PreparePopup() {
  ApplyPreferredMode();
}

}  // namespace MenuTheme
