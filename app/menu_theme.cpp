#include "menu_theme.h"

#include <windows.h>

#include <mutex>

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
// Win10 1809 only: BOOL AllowDarkModeForApp(BOOL). Replaced by SetPreferredAppMode in 1903.
using AllowDarkModeForAppFn = BOOL(WINAPI*)(BOOL);

SetPreferredAppModeFn g_setPreferredAppMode = nullptr;
FlushMenuThemesFn g_flushMenuThemes = nullptr;
AllowDarkModeForAppFn g_allowDarkModeForApp = nullptr;
std::once_flag g_resolveOnce;

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

DWORD WindowsBuildNumber() {
  using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
  HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
  if (!ntdll) {
    return 0;
  }
  auto rtl = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion"));
  if (!rtl) {
    return 0;
  }
  OSVERSIONINFOW info = {};
  info.dwOSVersionInfoSize = sizeof(info);
  if (rtl(&info) != 0) {
    return 0;
  }
  return info.dwBuildNumber;
}

void ResolveApis() {
  std::call_once(g_resolveOnce, [] {
    // Ordinals 135/136 are undocumented. 1809 uses AllowDarkModeForApp; 1903+ adds
    // SetPreferredAppMode + FlushMenuThemes. Skip them below the build that introduced them.
    const DWORD build = WindowsBuildNumber();
    if (build != 0 && build < 17763) {
      return;
    }
    HMODULE ux = GetModuleHandleW(L"uxtheme.dll");
    if (!ux) {
      ux = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    }
    if (!ux) {
      return;
    }
    // Undocumented ordinals (Win10 1903+ / Win11):
    //   135 SetPreferredAppMode, 136 FlushMenuThemes.
    // On 1809 ordinal 135 is AllowDarkModeForApp(BOOL) — keep both paths.
    auto* ord135 = GetProcAddress(ux, MAKEINTRESOURCEA(135));
    auto* ord136 = GetProcAddress(ux, MAKEINTRESOURCEA(136));
    g_flushMenuThemes = reinterpret_cast<FlushMenuThemesFn>(ord136);
    if (ord136 && (build == 0 || build >= 18362)) {
      // FlushMenuThemes exists ⇒ 1903+ SetPreferredAppMode ABI.
      g_setPreferredAppMode = reinterpret_cast<SetPreferredAppModeFn>(ord135);
    } else if (build == 0 || build >= 17763) {
      g_allowDarkModeForApp = reinterpret_cast<AllowDarkModeForAppFn>(ord135);
    }
  });
}

void ApplyPreferredMode() {
  ResolveApis();
  const bool light = AppsUseLightTheme();
  if (g_setPreferredAppMode) {
    g_setPreferredAppMode(light ? kForceLight : kForceDark);
    if (g_flushMenuThemes) {
      g_flushMenuThemes();
    }
    return;
  }
  if (g_allowDarkModeForApp) {
    g_allowDarkModeForApp(light ? FALSE : TRUE);
  }
}

}  // namespace

void EnableSystemMenus() {
  PreparePopup();
}

void PreparePopup() {
  ApplyPreferredMode();
}

}  // namespace MenuTheme
