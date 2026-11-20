#include "settings_theme.h"

#include "logging.h"
#include "settings_store.h"

#include <dwmapi.h>

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <string>
#include <initializer_list>
#include <unordered_map>
#include <utility>

namespace {

struct PresetColors {
  const wchar_t* id;
  COLORREF bg;
  COLORREF fg;
  COLORREF secondary;
  COLORREF border;
};

// Keep in lockstep with kThemePalettes in the island translation unit.
constexpr PresetColors kPresets[] = {
    {L"obsidian", RGB(0x08, 0x08, 0x0A), RGB(0xFF, 0xFF, 0xFF), RGB(0x9B, 0x9B, 0xA5),
     RGB(0x1E, 0x1E, 0x22)},
    {L"graphite", RGB(0x1C, 0x1C, 0x1E), RGB(0xFF, 0xFF, 0xFF), RGB(0xA8, 0xA8, 0xAE),
     RGB(0x32, 0x32, 0x36)},
    {L"slate", RGB(0x11, 0x17, 0x21), RGB(0xE9, 0xEE, 0xF6), RGB(0x92, 0xA2, 0xB8),
     RGB(0x23, 0x2C, 0x3A)},
    {L"nord", RGB(0x2E, 0x34, 0x40), RGB(0xEC, 0xEF, 0xF4), RGB(0xA7, 0xB0, 0xC0),
     RGB(0x3B, 0x42, 0x52)},
    {L"evergreen", RGB(0x0C, 0x15, 0x12), RGB(0xE4, 0xF1, 0xEA), RGB(0x8C, 0xAE, 0x9D),
     RGB(0x1A, 0x2A, 0x23)},
    {L"espresso", RGB(0x1A, 0x15, 0x12), RGB(0xF6, 0xEE, 0xE7), RGB(0xB7, 0xA3, 0x95),
     RGB(0x2E, 0x24, 0x20)},
    {L"plum", RGB(0x16, 0x11, 0x1C), RGB(0xF1, 0xEA, 0xF6), RGB(0xAC, 0x9B, 0xB9),
     RGB(0x28, 0x1F, 0x33)},
    {L"porcelain", RGB(0xF5, 0xF6, 0xF8), RGB(0x14, 0x16, 0x1A), RGB(0x5B, 0x62, 0x70),
     RGB(0xD8, 0xDB, 0xE1)},
};

constexpr COLORREF kFallbackAccent = RGB(0x4C, 0xC9, 0xF0);

bool EqualsNoCase(const std::wstring& text, const wchar_t* literal) {
  return _wcsicmp(text.c_str(), literal) == 0;
}

int Channel(int component) {
  return (std::clamp)(component, 0, 255);
}

double SrgbToLinear(int component) {
  const double s = Channel(component) / 255.0;
  if (s <= 0.04045) {
    return s / 12.92;
  }
  return std::pow((s + 0.055) / 1.055, 2.4);
}

double Luminance(COLORREF color) {
  return 0.2126 * SrgbToLinear(GetRValue(color)) + 0.7152 * SrgbToLinear(GetGValue(color)) +
         0.0722 * SrgbToLinear(GetBValue(color));
}

double Contrast(COLORREF a, COLORREF b) {
  const double left = Luminance(a);
  const double right = Luminance(b);
  const double lighter = (std::max)(left, right);
  const double darker = (std::min)(left, right);
  return (lighter + 0.05) / (darker + 0.05);
}

COLORREF Mix(COLORREF from, COLORREF to, double amount) {
  const double t = (std::clamp)(amount, 0.0, 1.0);
  auto mixChannel = [&](int a, int b) {
    return Channel(static_cast<int>(std::lround(a + (b - a) * t)));
  };
  return RGB(mixChannel(GetRValue(from), GetRValue(to)), mixChannel(GetGValue(from), GetGValue(to)),
             mixChannel(GetBValue(from), GetBValue(to)));
}

COLORREF EnsureContrast(COLORREF foreground, COLORREF background, double minimum) {
  if (Contrast(foreground, background) >= minimum) {
    return foreground;
  }
  const COLORREF toward =
      Contrast(RGB(255, 255, 255), background) >= Contrast(RGB(0, 0, 0), background) ? RGB(255, 255, 255)
                                                                                      : RGB(0, 0, 0);
  COLORREF best = toward;
  for (int step = 1; step <= 16; ++step) {
    const COLORREF mixed = Mix(foreground, toward, step / 16.0);
    if (Contrast(mixed, background) >= minimum) {
      return mixed;
    }
    best = mixed;
  }
  return best;
}

int HexNibble(wchar_t ch) {
  if (ch >= L'0' && ch <= L'9') {
    return ch - L'0';
  }
  if (ch >= L'a' && ch <= L'f') {
    return ch - L'a' + 10;
  }
  if (ch >= L'A' && ch <= L'F') {
    return ch - L'A' + 10;
  }
  return -1;
}

bool ParseHex(const std::wstring& text, COLORREF* out) {
  if (!out) {
    return false;
  }
  const size_t first = text.find_first_not_of(L" \t\r\n\"'");
  if (first == std::wstring::npos) {
    return false;
  }
  const size_t last = text.find_last_not_of(L" \t\r\n\"'");
  std::wstring digits = text.substr(first, last - first + 1);
  if (!digits.empty() && digits[0] == L'#') {
    digits.erase(digits.begin());
  }
  if (digits.size() != 3 && digits.size() != 4 && digits.size() != 6 && digits.size() != 8) {
    return false;
  }
  for (wchar_t ch : digits) {
    if (HexNibble(ch) < 0) {
      return false;
    }
  }
  int red = 0;
  int green = 0;
  int blue = 0;
  if (digits.size() == 3 || digits.size() == 4) {
    red = HexNibble(digits[0]) * 17;
    green = HexNibble(digits[1]) * 17;
    blue = HexNibble(digits[2]) * 17;
  } else {
    red = HexNibble(digits[0]) * 16 + HexNibble(digits[1]);
    green = HexNibble(digits[2]) * 16 + HexNibble(digits[3]);
    blue = HexNibble(digits[4]) * 16 + HexNibble(digits[5]);
  }
  *out = RGB(red, green, blue);
  return true;
}

bool IsOneOf(COLORREF color, std::initializer_list<COLORREF> options) {
  for (COLORREF option : options) {
    if (color == option) {
      return true;
    }
  }
  return false;
}

const PresetColors& PresetById(const std::wstring& preset) {
  std::wstring id = preset;
  if (EqualsNoCase(id, L"oled-black")) {
    id = L"obsidian";
  } else if (EqualsNoCase(id, L"fluent") || EqualsNoCase(id, L"mica") || EqualsNoCase(id, L"dark-gray")) {
    id = L"graphite";
  } else if (EqualsNoCase(id, L"midnight-blue")) {
    id = L"slate";
  } else if (EqualsNoCase(id, L"deep-purple")) {
    id = L"plum";
  }
  for (const PresetColors& entry : kPresets) {
    if (EqualsNoCase(id, entry.id)) {
      return entry;
    }
  }
  return kPresets[0];
}

COLORREF ResolveRole(const std::wstring& text, COLORREF presetColor, bool customPreset,
                     std::initializer_list<COLORREF> sentinels) {
  COLORREF parsed = 0;
  if (!ParseHex(text, &parsed)) {
    return presetColor;
  }
  if (!customPreset && IsOneOf(parsed, sentinels)) {
    return presetColor;
  }
  return parsed;
}

double ClampUnit(double value, double lo, double hi) {
  return (std::clamp)(value, lo, hi);
}

void RgbToHsl(COLORREF color, double* hue, double* saturation, double* light) {
  const double red = GetRValue(color) / 255.0;
  const double green = GetGValue(color) / 255.0;
  const double blue = GetBValue(color) / 255.0;
  const double maxChannel = (std::max)(red, (std::max)(green, blue));
  const double minChannel = (std::min)(red, (std::min)(green, blue));
  *light = (maxChannel + minChannel) / 2.0;
  if (maxChannel == minChannel) {
    *hue = 0.0;
    *saturation = 0.0;
    return;
  }
  const double delta = maxChannel - minChannel;
  *saturation = *light > 0.5 ? delta / (2.0 - maxChannel - minChannel) : delta / (maxChannel + minChannel);
  if (maxChannel == red) {
    *hue = (green - blue) / delta + (green < blue ? 6.0 : 0.0);
  } else if (maxChannel == green) {
    *hue = (blue - red) / delta + 2.0;
  } else {
    *hue = (red - green) / delta + 4.0;
  }
  *hue /= 6.0;
}

double HueToRgb(double p, double q, double t) {
  if (t < 0.0) {
    t += 1.0;
  }
  if (t > 1.0) {
    t -= 1.0;
  }
  if (t < 1.0 / 6.0) {
    return p + (q - p) * 6.0 * t;
  }
  if (t < 1.0 / 2.0) {
    return q;
  }
  if (t < 2.0 / 3.0) {
    return p + (q - p) * (2.0 / 3.0 - t) * 6.0;
  }
  return p;
}

COLORREF HslToRgb(double hue, double saturation, double light) {
  double red = light;
  double green = light;
  double blue = light;
  if (saturation > 0.0) {
    const double q = light < 0.5 ? light * (1.0 + saturation) : light + saturation - light * saturation;
    const double p = 2.0 * light - q;
    red = HueToRgb(p, q, hue + 1.0 / 3.0);
    green = HueToRgb(p, q, hue);
    blue = HueToRgb(p, q, hue - 1.0 / 3.0);
  }
  return RGB(Channel(static_cast<int>(std::lround(red * 255.0))),
             Channel(static_cast<int>(std::lround(green * 255.0))),
             Channel(static_cast<int>(std::lround(blue * 255.0))));
}

void ApplyAdaptive(COLORREF sample, COLORREF* bg, COLORREF* text, COLORREF* muted) {
  double hue = 0.0;
  double saturation = 0.0;
  double light = 0.0;
  RgbToHsl(sample, &hue, &saturation, &light);
  const bool lightBg = Luminance(sample) > 0.52;
  const double sat =
      ClampUnit(saturation * (lightBg ? 0.22 : 0.42), lightBg ? 0.02 : 0.05, lightBg ? 0.14 : 0.28);
  if (lightBg) {
    *bg = HslToRgb(hue, sat, ClampUnit(0.86 + light * 0.08, 0.84, 0.94));
    *text = HslToRgb(hue, ClampUnit(saturation * 0.18, 0.02, 0.12), 0.14);
    *muted = HslToRgb(hue, ClampUnit(saturation * 0.16, 0.02, 0.10), 0.36);
  } else {
    *bg = HslToRgb(hue, sat, ClampUnit(0.055 + light * 0.12, 0.045, 0.16));
    *text = HslToRgb(hue, ClampUnit(saturation * 0.10, 0.0, 0.08), 0.96);
    *muted = HslToRgb(hue, ClampUnit(saturation * 0.14, 0.02, 0.12), 0.68);
  }
}

COLORREF TuneAgainstText(COLORREF base, COLORREF toward, double amount, COLORREF text) {
  double mix = amount;
  for (int attempt = 0; attempt < 8; ++attempt) {
    const COLORREF color = Mix(base, toward, mix);
    if (Contrast(text, color) >= 4.5) {
      return color;
    }
    mix *= 0.5;
  }
  return base;
}

bool EnabledFlag(const SettingsValueLookup& lookup, const wchar_t* key) {
  return EqualsNoCase(lookup.Value(key), L"1");
}

bool KeyIs(const wchar_t* key, const wchar_t* expected) {
  return key && expected && wcscmp(key, expected) == 0;
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

class MapLookup final : public SettingsValueLookup {
 public:
  explicit MapLookup(std::initializer_list<std::pair<const wchar_t*, const wchar_t*>> items) {
    for (const auto& item : items) {
      values_.emplace(item.first, item.second);
    }
  }
  std::wstring Value(const wchar_t* key) const override {
    const auto found = values_.find(key ? std::wstring(key) : std::wstring());
    if (found == values_.end()) {
      return {};
    }
    return found->second;
  }

 private:
  std::unordered_map<std::wstring, std::wstring> values_;
};

}  // namespace

SettingsDialogPalette BuildSettingsDialogPalette(const SettingsThemeSource& source) {
  SettingsDialogPalette palette;
  if (source.highContrast) {
    palette.windowBg = GetSysColor(COLOR_WINDOW);
    palette.navBg = GetSysColor(COLOR_BTNFACE);
    palette.navSelected = GetSysColor(COLOR_HIGHLIGHT);
    palette.elevated = GetSysColor(COLOR_BTNFACE);
    palette.controlBg = GetSysColor(COLOR_WINDOW);
    palette.text = GetSysColor(COLOR_WINDOWTEXT);
    palette.muted = GetSysColor(COLOR_GRAYTEXT);
    palette.headerText = GetSysColor(COLOR_HIGHLIGHT);
    palette.border = GetSysColor(COLOR_WINDOWFRAME);
    palette.divider = GetSysColor(COLOR_WINDOWFRAME);
    palette.accent = GetSysColor(COLOR_HIGHLIGHT);
    palette.accentHover = GetSysColor(COLOR_HOTLIGHT);
    palette.accentPressed = GetSysColor(COLOR_HIGHLIGHT);
    palette.accentText = GetSysColor(COLOR_HIGHLIGHTTEXT);
    palette.focus = GetSysColor(COLOR_HIGHLIGHT);
    palette.caption = GetSysColor(COLOR_ACTIVECAPTION);
    palette.captionText = GetSysColor(COLOR_CAPTIONTEXT);
    palette.bannerBg = GetSysColor(COLOR_INFOBK);
    palette.bannerText = GetSysColor(COLOR_INFOTEXT);
    palette.pillBg = palette.windowBg;
    palette.pillText = palette.text;
    palette.pillMuted = palette.muted;
    palette.pillBorder = palette.border;
    palette.dark = Contrast(RGB(255, 255, 255), palette.windowBg) >= Contrast(RGB(0, 0, 0), palette.windowBg);
    return palette;
  }

  const bool customPreset = EqualsNoCase(source.preset, L"custom");
  const PresetColors& preset = PresetById(customPreset ? L"obsidian" : source.preset);
  COLORREF bg = ResolveRole(source.pillBg, preset.bg, customPreset, {RGB(0x08, 0x08, 0x0A), RGB(0x0D, 0x0D, 0x0F)});
  COLORREF text =
      ResolveRole(source.textPrimary, preset.fg, customPreset, {RGB(0xFF, 0xFF, 0xFF), RGB(0xF7, 0xF7, 0xF7)});
  COLORREF muted = ResolveRole(source.textSecondary, preset.secondary, customPreset,
                               {RGB(0x9B, 0x9B, 0xA5), RGB(0xB0, 0xB0, 0xB8), RGB(0x88, 0x88, 0x88)});
  COLORREF border =
      ResolveRole(source.borderHex, preset.border, customPreset, {RGB(0x1E, 0x1E, 0x22), RGB(0x33, 0x33, 0x38)});

  if (source.adaptive) {
    ApplyAdaptive(source.systemAccent, &bg, &text, &muted);
  }

  text = EnsureContrast(text, bg, 4.6);
  muted = EnsureContrast(muted, bg, 3.4);
  if (Contrast(border, bg) < 1.35) {
    border = Mix(border, text, 0.45);
  }

  palette.dark = Contrast(RGB(255, 255, 255), bg) >= Contrast(RGB(0, 0, 0), bg);
  palette.pillBg = bg;
  palette.pillText = text;
  palette.pillMuted = muted;
  palette.pillBorder = border;
  palette.windowBg = bg;
  palette.elevated = palette.dark ? Mix(bg, RGB(255, 255, 255), 0.07) : Mix(bg, RGB(255, 255, 255), 0.55);
  palette.controlBg = palette.dark ? Mix(bg, RGB(255, 255, 255), 0.13) : RGB(255, 255, 255);
  palette.navBg = palette.dark ? Mix(bg, RGB(0, 0, 0), 0.28) : Mix(bg, RGB(255, 255, 255), 0.35);
  palette.text = EnsureContrast(text, palette.elevated, 4.6);
  palette.text = EnsureContrast(palette.text, palette.windowBg, 4.6);
  palette.muted = EnsureContrast(muted, palette.elevated, 3.4);
  palette.border = border;
  palette.divider = Mix(border, bg, 0.45);
  palette.caption = palette.dark ? Mix(bg, RGB(0, 0, 0), 0.30) : Mix(bg, RGB(255, 255, 255), 0.25);
  palette.captionText = EnsureContrast(palette.text, palette.caption, 4.6);

  COLORREF accent = kFallbackAccent;
  if (EqualsNoCase(source.accentMode, L"system")) {
    accent = source.systemAccent;
  } else if (EqualsNoCase(source.accentMode, L"custom")) {
    COLORREF parsed = 0;
    accent = ParseHex(source.customAccent, &parsed) ? parsed : kFallbackAccent;
  }
  accent = EnsureContrast(accent, palette.windowBg, 3.0);
  const COLORREF accentText = Contrast(RGB(255, 255, 255), accent) >= Contrast(RGB(0, 0, 0), accent)
                                  ? RGB(255, 255, 255)
                                  : RGB(8, 10, 14);
  palette.accentText = EnsureContrast(accentText, accent, 4.6);
  palette.accent = accent;
  palette.accentHover = TuneAgainstText(accent, RGB(255, 255, 255), 0.18, palette.accentText);
  palette.accentPressed = TuneAgainstText(accent, RGB(0, 0, 0), 0.18, palette.accentText);
  palette.focus = accent;
  palette.headerText = EnsureContrast(accent, palette.elevated, 4.6);
  palette.navSelected = Mix(palette.navBg, accent, palette.dark ? 0.22 : 0.16);
  palette.bannerBg = Mix(bg, RGB(176, 112, 24), palette.dark ? 0.30 : 0.18);
  palette.bannerText = EnsureContrast(palette.text, palette.bannerBg, 4.6);
  return palette;
}

bool SettingsControlIsEnabled(const wchar_t* key, const SettingsValueLookup& lookup) {
  if (!key || !key[0]) {
    return true;
  }
  if (KeyIs(key, L"Themes.CustomAccentHex")) {
    return EqualsNoCase(lookup.Value(L"Themes.AccentColorMode"), L"custom");
  }
  if (KeyIs(key, L"Themes.ContourBorderHex")) {
    // The schema has no "custom" border mode. Default paints this hex; Auto
    // substitutes the accent and Borderless draws nothing.
    const std::wstring mode = lookup.Value(L"Themes.ContourBorderMode");
    return EqualsNoCase(mode, L"default") || EqualsNoCase(mode, L"custom");
  }
  if (KeyIs(key, L"Themes.PillBgColor") || KeyIs(key, L"Themes.TextPrimaryColor") ||
      KeyIs(key, L"Themes.TextSecondaryColor")) {
    return EqualsNoCase(lookup.Value(L"Themes.ThemePreset"), L"custom");
  }
  if (KeyIs(key, L"Themes.AuraIntensity")) {
    return !EqualsNoCase(lookup.Value(L"Themes.AuraStyle"), L"off");
  }
  if (KeyIs(key, L"Themes.BackdropTint") || KeyIs(key, L"Themes.BackdropFillOpacity")) {
    const std::wstring appearance = lookup.Value(L"Appearance.BackdropMaterial");
    const std::wstring themes = lookup.Value(L"Themes.BackdropMaterial");
    const bool useThemes = appearance.empty() || EqualsNoCase(appearance, L"auto");
    const std::wstring& effective = useThemes ? themes : appearance;
    return !EqualsNoCase(effective, L"none");
  }
  if (KeyIs(key, L"Appearance.OffsetYExpanded")) {
    return EnabledFlag(lookup, L"Appearance.SeparateExpandedOffsetY");
  }
  if (KeyIs(key, L"Modules.MediaAutoExpand") || KeyIs(key, L"Modules.MediaExpandBlocklist") ||
      KeyIs(key, L"Modules.MediaPillClock") || KeyIs(key, L"Modules.MediaPillDate")) {
    return EnabledFlag(lookup, L"Modules.Media");
  }
  if (KeyIs(key, L"Modules.ClockFormat") || KeyIs(key, L"Modules.ShowSeconds")) {
    return EnabledFlag(lookup, L"Modules.Media") && EnabledFlag(lookup, L"Modules.MediaPillClock");
  }
  if (KeyIs(key, L"Modules.DateFormat") || KeyIs(key, L"Modules.DateFirst")) {
    return EnabledFlag(lookup, L"Modules.Media") && EnabledFlag(lookup, L"Modules.MediaPillDate");
  }
  if (KeyIs(key, L"Modules.WeatherCity") || KeyIs(key, L"Modules.WeatherFahrenheit")) {
    return EnabledFlag(lookup, L"Modules.Weather");
  }
  if (KeyIs(key, L"Modules.FirstDayOfWeek")) {
    return EnabledFlag(lookup, L"Modules.CalendarModule");
  }
  if (KeyIs(key, L"Modules.ShowMetricText")) {
    return EnabledFlag(lookup, L"Modules.HardwareMonitorModule");
  }
  if (KeyIs(key, L"Modules.BluetoothShowBattery")) {
    return EnabledFlag(lookup, L"Modules.BluetoothIndicator");
  }
  if (KeyIs(key, L"Modules.NotificationRespectDnD")) {
    return EnabledFlag(lookup, L"Modules.DoNotDisturbIndicator");
  }
  if (KeyIs(key, L"Modules.TimerContourProgress")) {
    return EnabledFlag(lookup, L"Modules.TimerModule");
  }
  if (KeyIs(key, L"Modules.StatusCountdownProgress")) {
    return EnabledFlag(lookup, L"Modules.Progress");
  }
  if (KeyIs(key, L"Modules.LyricsVisualizer") || KeyIs(key, L"Modules.KaraokeLyrics") ||
      KeyIs(key, L"Modules.LyricsNeighborLinesVisible") || KeyIs(key, L"Modules.LyricsOffsetMs") ||
      KeyIs(key, L"Modules.LyricsOffsetBluetoothMs") || KeyIs(key, L"Modules.LyricsCache") ||
      KeyIs(key, L"Modules.CollapsedLyrics") || KeyIs(key, L"Modules.LyricsPinnedFirst") ||
      KeyIs(key, L"Modules.MediaTabLyrics")) {
    return EnabledFlag(lookup, L"Modules.Lyrics");
  }
  if (KeyIs(key, L"Modules.KaraokeLetterGlow")) {
    return EnabledFlag(lookup, L"Modules.Lyrics") && EnabledFlag(lookup, L"Modules.KaraokeLyrics");
  }
  if (KeyIs(key, L"Modules.LyricsCacheMaxMB")) {
    return EnabledFlag(lookup, L"Modules.Lyrics") && EnabledFlag(lookup, L"Modules.LyricsCache");
  }
  if (KeyIs(key, L"Modules.FileTrayMaxItems") || KeyIs(key, L"Modules.FileTrayPersist")) {
    return EnabledFlag(lookup, L"Modules.FileTrayModule");
  }
  if (KeyIs(key, L"Modules.GameOverlayShowFps") || KeyIs(key, L"Modules.GameOverlayShowCpu") ||
      KeyIs(key, L"Modules.GameOverlayShowGpu") || KeyIs(key, L"Modules.GameOverlayShowRam") ||
      KeyIs(key, L"Modules.GameOverlayShowDisk") || KeyIs(key, L"Modules.GameOverlayCompact")) {
    return EnabledFlag(lookup, L"Modules.GameOverlay");
  }
  if (KeyIs(key, L"Indicators.PrivacyDotsMic") || KeyIs(key, L"Indicators.PrivacyDotsCam")) {
    return EnabledFlag(lookup, L"Indicators.PrivacyDots");
  }
  if (KeyIs(key, L"Shortcuts.LookupModifiers") || KeyIs(key, L"Shortcuts.LookupKey")) {
    return EnabledFlag(lookup, L"Modules.QuickLookup");
  }
  if (KeyIs(key, L"Shortcuts.HideShowModifiers") || KeyIs(key, L"Shortcuts.HideShowKey")) {
    return EnabledFlag(lookup, L"Shortcuts.HideShowHotkeyEnabled");
  }
  if (KeyIs(key, L"Modules.MediaLikeBridgePort")) {
    return EnabledFlag(lookup, L"Modules.MediaLikeBridge");
  }
  if (KeyIs(key, L"Modules.MessengerApps")) {
    return EnabledFlag(lookup, L"Modules.Notifications");
  }
  return true;
}

bool SettingsThemeActiveMenuIsLight() {
  SettingsThemeSource source;
  source.preset = SettingsGetString(L"Themes.ThemePreset", L"obsidian");
  source.pillBg = SettingsGetString(L"Themes.PillBgColor", L"#08080A");
  source.textPrimary = SettingsGetString(L"Themes.TextPrimaryColor", L"#FFFFFF");
  source.textSecondary = SettingsGetString(L"Themes.TextSecondaryColor", L"#9B9BA5");
  source.borderHex = SettingsGetString(L"Themes.ContourBorderHex", L"#333338");
  source.accentMode = SettingsGetString(L"Themes.AccentColorMode", L"auto");
  source.customAccent = SettingsGetString(L"Themes.CustomAccentHex", L"#4CC9F0");
  source.systemAccent = SettingsThemeSystemAccent();
  // Menus follow the selected preset or custom background. Adaptive sampling
  // tracks the desktop and would flicker the menu chrome.
  source.adaptive = false;
  const SettingsDialogPalette palette = BuildSettingsDialogPalette(source);
  return !palette.dark;
}

bool SettingsThemeTryParseHex(const wchar_t* text, COLORREF* out) {
  return ParseHex(text ? std::wstring(text) : std::wstring(), out);
}

COLORREF SettingsThemeSystemAccent() {
  DWORD color = 0;
  BOOL opaque = FALSE;
  using DwmGetColorizationColorFn = HRESULT(WINAPI*)(DWORD*, BOOL*);
  HMODULE dwm = GetModuleHandleW(L"dwmapi.dll");
  if (!dwm) {
    dwm = LoadLibraryExW(L"dwmapi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
  }
  auto proc = dwm ? reinterpret_cast<DwmGetColorizationColorFn>(GetProcAddress(dwm, "DwmGetColorizationColor"))
                  : nullptr;
  if (proc && SUCCEEDED(proc(&color, &opaque))) {
    return RGB((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF);
  }
  return kFallbackAccent;
}

void ApplySettingsWindowChrome(HWND hwnd, const SettingsDialogPalette& palette) {
  if (!hwnd) {
    return;
  }
  const BOOL dark = palette.dark ? TRUE : FALSE;
  // 20 is DWMWA_USE_IMMERSIVE_DARK_MODE (20H1+). 19 is the 1809/1903 attribute.
  if (FAILED(DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark)))) {
    DwmSetWindowAttribute(hwnd, 19, &dark, sizeof(dark));
  }
  // Caption, caption text, and border colors are Win11 (build 22000+). Older
  // builds return E_INVALIDARG; the client area is still painted from the palette.
  if (WindowsBuildNumber() >= 22000) {
    const COLORREF caption = palette.caption;
    const COLORREF captionText = palette.captionText;
    const COLORREF border = palette.border;
    DwmSetWindowAttribute(hwnd, 35, &caption, sizeof(caption));
    DwmSetWindowAttribute(hwnd, 36, &captionText, sizeof(captionText));
    DwmSetWindowAttribute(hwnd, 34, &border, sizeof(border));
  }
  SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

void AllowSettingsControlDarkMode(HWND hwnd, bool dark) {
  if (!hwnd) {
    return;
  }
  using AllowDarkModeForWindowFn = BOOL(WINAPI*)(HWND, BOOL);
  const DWORD build = WindowsBuildNumber();
  if (build == 0 || build >= 17763) {
    HMODULE ux = GetModuleHandleW(L"uxtheme.dll");
    if (!ux) {
      ux = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    }
    // Ordinal 133 is AllowDarkModeForWindow, present since Windows 10 1809.
    auto allow = ux ? reinterpret_cast<AllowDarkModeForWindowFn>(GetProcAddress(ux, MAKEINTRESOURCEA(133)))
                    : nullptr;
    if (allow) {
      allow(hwnd, dark ? TRUE : FALSE);
    }
  }
  SetWindowTheme(hwnd, dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
}

bool SettingsThemeSelfTest() {
  bool failed = false;
  auto check = [&](bool condition, const wchar_t* name) {
    if (!condition) {
      LUX_LOGE(L"Settings theme self-test failed: %s", name);
      failed = true;
    }
  };

  for (const PresetColors& preset : kPresets) {
    SettingsThemeSource source;
    source.preset = preset.id;
    source.accentMode = L"auto";
    const SettingsDialogPalette palette = BuildSettingsDialogPalette(source);
    const std::wstring label = preset.id;
    check(Contrast(palette.text, palette.windowBg) >= 4.5, (label + L" text contrast").c_str());
    check(Contrast(palette.muted, palette.windowBg) >= 3.0, (label + L" muted contrast").c_str());
    check(Contrast(palette.accentText, palette.accent) >= 4.5, (label + L" accent text").c_str());
    check(Contrast(palette.headerText, palette.elevated) >= 4.5, (label + L" header contrast").c_str());
    check(Contrast(palette.captionText, palette.caption) >= 4.5, (label + L" caption contrast").c_str());
  }

  {
    SettingsThemeSource porcelain;
    porcelain.preset = L"porcelain";
    const SettingsDialogPalette palette = BuildSettingsDialogPalette(porcelain);
    check(!palette.dark, L"porcelain is light");
  }
  {
    SettingsThemeSource obsidian;
    obsidian.preset = L"obsidian";
    const SettingsDialogPalette palette = BuildSettingsDialogPalette(obsidian);
    check(palette.dark, L"obsidian is dark");
  }
  {
    SettingsThemeSource source;
    source.preset = L"nord";
    source.pillBg = L"#08080A";
    source.textPrimary = L"#FFFFFF";
    source.textSecondary = L"#9B9BA5";
    source.borderHex = L"#333338";
    const SettingsDialogPalette palette = BuildSettingsDialogPalette(source);
    check(GetRValue(palette.pillBg) > 0x20 && GetRValue(palette.pillBg) < 0x40, L"nord ignores obsidian sentinels");
  }
  {
    SettingsThemeSource source;
    source.preset = L"custom";
    source.pillBg = L"#FFFFFF";
    source.textPrimary = L"#FFFFFF";
    source.textSecondary = L"#FFFFFF";
    source.borderHex = L"#FFFFFF";
    source.accentMode = L"custom";
    source.customAccent = L"#FFFFFF";
    const SettingsDialogPalette palette = BuildSettingsDialogPalette(source);
    check(Contrast(palette.text, palette.windowBg) >= 4.5, L"custom white-on-white text");
    check(palette.text != RGB(255, 255, 255), L"custom white text was repaired");
    check(Contrast(palette.accentText, palette.accent) >= 4.5, L"custom white accent text");
  }
  {
    SettingsThemeSource source;
    source.preset = L"custom";
    source.pillBg = L"#101010";
    source.textPrimary = L"#F2F2F2";
    source.textSecondary = L"#A0A0A0";
    source.borderHex = L"#303030";
    source.accentMode = L"system";
    source.systemAccent = RGB(0, 120, 215);
    source.adaptive = true;
    const SettingsDialogPalette palette = BuildSettingsDialogPalette(source);
    check(Contrast(palette.text, palette.windowBg) >= 4.5, L"adaptive contrast");
    check(palette.dark, L"adaptive dark sample stays dark");
  }

  {
    MapLookup off({
        {L"Themes.AccentColorMode", L"auto"},
        {L"Themes.ContourBorderMode", L"borderless"},
        {L"Themes.ThemePreset", L"obsidian"},
        {L"Themes.AuraStyle", L"off"},
        {L"Appearance.BackdropMaterial", L"auto"},
        {L"Themes.BackdropMaterial", L"none"},
        {L"Appearance.SeparateExpandedOffsetY", L"0"},
        {L"Modules.Media", L"0"},
        {L"Modules.MediaPillClock", L"0"},
        {L"Modules.Lyrics", L"0"},
        {L"Modules.LyricsCache", L"0"},
        {L"Modules.KaraokeLyrics", L"0"},
        {L"Modules.GameOverlay", L"0"},
        {L"Modules.FileTrayModule", L"0"},
        {L"Indicators.PrivacyDots", L"0"},
        {L"Modules.QuickLookup", L"0"},
        {L"Shortcuts.HideShowHotkeyEnabled", L"0"},
        {L"Modules.Weather", L"0"},
        {L"Modules.BluetoothIndicator", L"0"},
        {L"Modules.DoNotDisturbIndicator", L"0"},
        {L"Modules.TimerModule", L"0"},
        {L"Modules.Progress", L"0"},
        {L"Modules.HardwareMonitorModule", L"0"},
        {L"Modules.CalendarModule", L"0"},
    });
    check(!SettingsControlIsEnabled(L"Themes.CustomAccentHex", off), L"accent hex off unless custom");
    check(!SettingsControlIsEnabled(L"Themes.ContourBorderHex", off), L"border hex off unless default");
    check(!SettingsControlIsEnabled(L"Themes.PillBgColor", off), L"pill color off unless custom preset");
    check(!SettingsControlIsEnabled(L"Themes.AuraIntensity", off), L"aura intensity off");
    check(!SettingsControlIsEnabled(L"Themes.BackdropTint", off), L"backdrop tint off");
    check(!SettingsControlIsEnabled(L"Appearance.OffsetYExpanded", off), L"expanded offset off");
    check(!SettingsControlIsEnabled(L"Modules.ClockFormat", off), L"clock format off");
    check(!SettingsControlIsEnabled(L"Modules.LyricsCacheMaxMB", off), L"lyrics cache size off");
    check(!SettingsControlIsEnabled(L"Modules.GameOverlayShowFps", off), L"overlay fps off");
    check(!SettingsControlIsEnabled(L"Modules.FileTrayMaxItems", off), L"file tray size off");
    check(!SettingsControlIsEnabled(L"Indicators.PrivacyDotsMic", off), L"mic dot off");
    check(!SettingsControlIsEnabled(L"Shortcuts.LookupKey", off), L"lookup key off");
    check(!SettingsControlIsEnabled(L"Shortcuts.HideShowKey", off), L"hide key off");
    check(!SettingsControlIsEnabled(L"Modules.WeatherCity", off), L"weather city off");
    check(!SettingsControlIsEnabled(L"Modules.ShowMetricText", off), L"metric text off");
    check(!SettingsControlIsEnabled(L"Modules.MediaLikeBridgePort", off), L"like bridge port off");
    check(!SettingsControlIsEnabled(L"Modules.MessengerApps", off), L"messenger apps off");
  }
  {
    MapLookup on({
        {L"Themes.AccentColorMode", L"custom"},
        {L"Themes.ContourBorderMode", L"default"},
        {L"Themes.ThemePreset", L"custom"},
        {L"Themes.AuraStyle", L"glow"},
        {L"Appearance.BackdropMaterial", L"acrylic"},
        {L"Themes.BackdropMaterial", L"none"},
        {L"Appearance.SeparateExpandedOffsetY", L"1"},
        {L"Modules.Media", L"1"},
        {L"Modules.MediaPillClock", L"1"},
        {L"Modules.MediaPillDate", L"1"},
        {L"Modules.Lyrics", L"1"},
        {L"Modules.LyricsCache", L"1"},
        {L"Modules.KaraokeLyrics", L"1"},
        {L"Modules.GameOverlay", L"1"},
        {L"Modules.FileTrayModule", L"1"},
        {L"Indicators.PrivacyDots", L"1"},
        {L"Modules.QuickLookup", L"1"},
        {L"Shortcuts.HideShowHotkeyEnabled", L"1"},
        {L"Modules.Weather", L"1"},
        {L"Modules.BluetoothIndicator", L"1"},
        {L"Modules.DoNotDisturbIndicator", L"1"},
        {L"Modules.TimerModule", L"1"},
        {L"Modules.Progress", L"1"},
        {L"Modules.HardwareMonitorModule", L"1"},
        {L"Modules.CalendarModule", L"1"},
    });
    check(SettingsControlIsEnabled(L"Themes.CustomAccentHex", on), L"accent hex on");
    check(SettingsControlIsEnabled(L"Themes.ContourBorderHex", on), L"border hex on for default");
    check(SettingsControlIsEnabled(L"Themes.TextSecondaryColor", on), L"custom text on");
    check(SettingsControlIsEnabled(L"Themes.BackdropTint", on), L"appearance acrylic keeps tint");
    check(SettingsControlIsEnabled(L"Modules.KaraokeLetterGlow", on), L"karaoke glow on");
    check(SettingsControlIsEnabled(L"Modules.DateFormat", on), L"date format on");
    check(SettingsControlIsEnabled(L"Shortcuts.HideShowModifiers", on), L"hide modifiers on");
    check(SettingsControlIsEnabled(L"Modules.FirstDayOfWeek", on), L"first day on");
    check(SettingsControlIsEnabled(L"Modules.AlwaysOnTop", on), L"unknown key stays enabled");
  }
  {
    MapLookup bridgeOn({{L"Modules.MediaLikeBridge", L"1"}, {L"Modules.Notifications", L"0"}});
    check(SettingsControlIsEnabled(L"Modules.MediaLikeBridgePort", bridgeOn), L"like bridge port follows bridge");
    check(!SettingsControlIsEnabled(L"Modules.MessengerApps", bridgeOn), L"messenger apps follow notifications");
  }
  {
    MapLookup notesOn({{L"Modules.MediaLikeBridge", L"0"}, {L"Modules.Notifications", L"1"}});
    check(!SettingsControlIsEnabled(L"Modules.MediaLikeBridgePort", notesOn), L"like bridge port off when bridge is off");
    check(SettingsControlIsEnabled(L"Modules.MessengerApps", notesOn), L"messenger apps on");
  }
  {
    MapLookup automatic({{L"Themes.ContourBorderMode", L"auto"}});
    check(!SettingsControlIsEnabled(L"Themes.ContourBorderHex", automatic), L"border hex off for auto");
  }

  return !failed;
}
