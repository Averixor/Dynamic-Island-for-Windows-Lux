#pragma once

#include <string>

#include <windows.h>

// Colors the settings dialog and tray/island menus use so they follow the island
// theme (built-in presets, custom hex, accent mode, adaptive) instead of only
// the Windows light/dark app setting.

struct SettingsThemeSource {
  std::wstring preset;
  std::wstring pillBg;
  std::wstring textPrimary;
  std::wstring textSecondary;
  std::wstring borderHex;
  std::wstring accentMode;
  std::wstring customAccent;
  bool adaptive = false;
  COLORREF systemAccent = RGB(76, 201, 240);
  bool highContrast = false;
};

struct SettingsDialogPalette {
  COLORREF windowBg = RGB(8, 8, 10);
  COLORREF navBg = RGB(8, 8, 10);
  COLORREF navSelected = RGB(20, 20, 24);
  COLORREF elevated = RGB(22, 22, 26);
  COLORREF controlBg = RGB(32, 32, 36);
  COLORREF text = RGB(255, 255, 255);
  COLORREF muted = RGB(155, 155, 165);
  COLORREF headerText = RGB(76, 201, 240);
  COLORREF border = RGB(30, 30, 34);
  COLORREF divider = RGB(40, 40, 46);
  COLORREF accent = RGB(76, 201, 240);
  COLORREF accentHover = RGB(106, 214, 245);
  COLORREF accentPressed = RGB(46, 160, 196);
  COLORREF accentText = RGB(8, 8, 10);
  COLORREF focus = RGB(76, 201, 240);
  COLORREF caption = RGB(8, 8, 10);
  COLORREF captionText = RGB(255, 255, 255);
  COLORREF bannerBg = RGB(48, 36, 16);
  COLORREF bannerText = RGB(255, 244, 220);
  COLORREF pillBg = RGB(8, 8, 10);
  COLORREF pillText = RGB(255, 255, 255);
  COLORREF pillMuted = RGB(155, 155, 165);
  COLORREF pillBorder = RGB(30, 30, 34);
  bool dark = true;
};

// Reads a setting value for dependency checks. The returned string must be the
// current control or stored value ("1"/"0", enum id, or hex).
struct SettingsValueLookup {
  virtual ~SettingsValueLookup() = default;
  virtual std::wstring Value(const wchar_t* key) const = 0;
};

SettingsDialogPalette BuildSettingsDialogPalette(const SettingsThemeSource& source);
bool SettingsControlIsEnabled(const wchar_t* key, const SettingsValueLookup& lookup);

// Saved settings (the island's active theme), not an unsaved dialog preview.
bool SettingsThemeActiveMenuIsLight();

COLORREF SettingsThemeSystemAccent();
bool SettingsThemeTryParseHex(const wchar_t* text, COLORREF* out);
void ApplySettingsWindowChrome(HWND hwnd, const SettingsDialogPalette& palette);
void AllowSettingsControlDarkMode(HWND hwnd, bool dark);

// Contrast and dependency checks. Logs and returns false on failure.
bool SettingsThemeSelfTest();
