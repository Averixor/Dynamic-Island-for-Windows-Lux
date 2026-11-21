#include "settings_dialog.h"

#include "autostart.h"
#include "logging.h"
#include "lux_overlay.h"
#include "main.h"
#include "resource.h"
#include "settings_schema.h"
#include "settings_store.h"
#include "settings_theme.h"
#include "settings_ui_loc.h"

#include <commctrl.h>
#include <commdlg.h>
#include <uxtheme.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <cwctype>
#include <new>
#include <string>
#include <vector>

#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "uxtheme.lib")

namespace {
constexpr int kControlIdBase = 4000;
constexpr int kSwatchIdBase = 6000;
constexpr int kNavIdBase = 3600;
constexpr int kIdSearch = 3900;
constexpr int kIdOk = 3801;
constexpr int kIdCancelBtn = 3802;
constexpr int kIdApply = 3803;
constexpr int kIdResetDefaults = 3804;
constexpr UINT_PTR kFilterTimer = 1;
constexpr UINT_PTR kSubCombo = 1;
constexpr UINT_PTR kSubButton = 2;
constexpr UINT_PTR kSubEdit = 3;
constexpr UINT_PTR kSubHeader = 4;
constexpr UINT_PTR kSubLabel = 5;
constexpr UINT_PTR kSubComboList = 6;
constexpr UINT kMoveNoPaint = SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW;
constexpr UINT kSliderSetRange = WM_USER + 40;
constexpr UINT kSliderSetPos = WM_USER + 41;
constexpr UINT kSliderGetPos = WM_USER + 42;
constexpr WORD kSliderChanged = 0x0401;

constexpr int kDlgClientW = 760;
constexpr int kDlgClientH = 580;
constexpr int kMinClientW = 680;
constexpr int kMinClientH = 480;
constexpr int kSidebarW = 176;
constexpr int kSearchH = 28;
constexpr int kFooterH = 56;
constexpr int kRowH = 34;
constexpr int kHeaderH = 22;
constexpr int kControlH = 24;
constexpr int kPreviewH = 118;

constexpr SettingGroup kNavOrder[] = {
    SettingGroup::Themes,    SettingGroup::Appearance, SettingGroup::Modules,   SettingGroup::Behavior,
    SettingGroup::Animations, SettingGroup::Indicators, SettingGroup::Shortcuts, SettingGroup::General,
};

const wchar_t kScrollClass[] = L"DynamicIslandLux.SettingsScroll";
const wchar_t kContentClass[] = L"DynamicIslandLux.SettingsContent";
const wchar_t kSliderClass[] = L"DynamicIslandLux.SettingsSlider";
const wchar_t kPreviewClass[] = L"DynamicIslandLux.SettingsPreview";
const wchar_t kHotProp[] = L"LuxHot";
const wchar_t kNavProp[] = L"LuxNav";
const wchar_t kSwatchProp[] = L"LuxSwatch";
const wchar_t kMutedProp[] = L"LuxMuted";
const wchar_t kListHookProp[] = L"LuxListHook";
const wchar_t kDropMinProp[] = L"LuxDropMin";

struct RowRef {
  size_t schemaIndex = 0;
  SettingsUiLoc::SettingsCard card = SettingsUiLoc::SettingsCard::General;
  HWND label = nullptr;
  HWND control = nullptr;
  HWND swatch = nullptr;
  std::wstring labelText;
};

struct NavItem {
  SettingGroup group = SettingGroup::Themes;
  HWND button = nullptr;
};

struct CardHead {
  SettingsUiLoc::SettingsCard card = SettingsUiLoc::SettingsCard::General;
  HWND hwnd = nullptr;
};

struct DialogState {
  HWND search = nullptr;
  HWND scroll = nullptr;
  HWND content = nullptr;
  HWND preview = nullptr;
  HWND emptyLabel = nullptr;
  HWND banner = nullptr;
  HWND okBtn = nullptr;
  HWND cancelBtn = nullptr;
  HWND applyBtn = nullptr;
  HWND resetBtn = nullptr;
  std::vector<NavItem> nav;
  std::vector<CardHead> headers;
  std::vector<RowRef> rows;
  std::vector<std::wstring> baseline;
  std::vector<RECT> cardRects;
  wchar_t filter[128] = {};
  SettingsUiLoc::Lang lang = SettingsUiLoc::Lang::En;
  SettingGroup category = SettingGroup::Themes;
  int contentHeight = 0;
  int scrollY = 0;
  int dpi = 96;
  int wheelRemainder = 0;
  HFONT uiFont = nullptr;
  HFONT headerFont = nullptr;
  HFONT previewFont = nullptr;
  SettingsDialogPalette palette{};
  HBRUSH windowBrush = nullptr;
  HBRUSH navBrush = nullptr;
  HBRUSH navSelectedBrush = nullptr;
  HBRUSH elevatedBrush = nullptr;
  HBRUSH controlBrush = nullptr;
  HBRUSH bannerBrush = nullptr;
  ULONGLONG suppressCloseUntil = 0;
  bool ready = false;
  bool updating = false;
  bool layingOut = false;
  std::wstring bannerText;
  COLORREF customColors[16] = {};
};

struct SliderState {
  int minValue = 0;
  int maxValue = 100;
  int value = 0;
  int wheelRemainder = 0;
  bool dragging = false;
  bool hot = false;
};

struct UpdateGuard {
  bool& flag;
  explicit UpdateGuard(bool& value) : flag(value) { flag = true; }
  ~UpdateGuard() { flag = false; }
  UpdateGuard(const UpdateGuard&) = delete;
  UpdateGuard& operator=(const UpdateGuard&) = delete;
};

DialogState* State(HWND hwnd) {
  return reinterpret_cast<DialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

DialogState* StateFrom(HWND hwnd) {
  for (HWND cur = hwnd; cur; cur = GetParent(cur)) {
    wchar_t cls[32] = {};
    GetClassNameW(cur, cls, ARRAYSIZE(cls));
    if (wcscmp(cls, L"#32770") == 0) {
      return State(cur);
    }
  }
  return nullptr;
}

int Px(const DialogState& st, int dip) {
  return MulDiv(dip, st.dpi > 0 ? st.dpi : 96, 96);
}

COLORREF Blend(COLORREF from, COLORREF to, int percent) {
  percent = (std::clamp)(percent, 0, 100);
  auto channel = [&](int a, int b) { return (a * (100 - percent) + b * percent) / 100; };
  return RGB(channel(GetRValue(from), GetRValue(to)), channel(GetGValue(from), GetGValue(to)),
             channel(GetBValue(from), GetBValue(to)));
}

bool IsColorKey(const wchar_t* key) {
  return key && (wcscmp(key, L"Themes.CustomAccentHex") == 0 || wcscmp(key, L"Themes.ContourBorderHex") == 0 ||
                 wcscmp(key, L"Themes.PillBgColor") == 0 || wcscmp(key, L"Themes.TextPrimaryColor") == 0 ||
                 wcscmp(key, L"Themes.TextSecondaryColor") == 0);
}

bool UseSlider(const SettingDef& def) {
  return def.type == SettingType::Int && def.minValue < def.maxValue;
}

bool AffectsPalette(const wchar_t* key) {
  return key && (wcscmp(key, L"Themes.ThemePreset") == 0 || wcscmp(key, L"Themes.AdaptiveTheme") == 0 ||
                 wcscmp(key, L"Themes.AccentColorMode") == 0 || wcscmp(key, L"Themes.CustomAccentHex") == 0 ||
                 wcscmp(key, L"Themes.PillBgColor") == 0 || wcscmp(key, L"Themes.TextPrimaryColor") == 0 ||
                 wcscmp(key, L"Themes.TextSecondaryColor") == 0 || wcscmp(key, L"Themes.ContourBorderHex") == 0);
}

bool ShowFocusCue(HWND hwnd) {
  return hwnd && GetFocus() == hwnd;
}

HFONT CreateUiFont(int dpi) {
  NONCLIENTMETRICSW metrics = {};
  metrics.cbSize = sizeof(metrics);
  if (SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0,
                                 static_cast<UINT>(dpi > 0 ? dpi : 96))) {
    return CreateFontIndirectW(&metrics.lfMessageFont);
  }
  return CreateFontW(-MulDiv(12, dpi > 0 ? dpi : 96, 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                     DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

HFONT CreateWeightFont(HFONT base, int dpi, int weight, int dip) {
  LOGFONTW lf = {};
  if (base && GetObjectW(base, sizeof(lf), &lf)) {
    lf.lfWeight = weight;
    if (dip > 0) {
      lf.lfHeight = -MulDiv(dip, dpi > 0 ? dpi : 96, 96);
    }
    return CreateFontIndirectW(&lf);
  }
  return CreateFontW(-MulDiv(dip > 0 ? dip : 12, dpi > 0 ? dpi : 96, 96), 0, 0, 0, weight, FALSE, FALSE,
                     FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                     DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

void ReleaseBrush(HBRUSH& brush) {
  if (brush) {
    DeleteObject(brush);
    brush = nullptr;
  }
}

void FreeBrushes(DialogState& st) {
  ReleaseBrush(st.windowBrush);
  ReleaseBrush(st.navBrush);
  ReleaseBrush(st.navSelectedBrush);
  ReleaseBrush(st.elevatedBrush);
  ReleaseBrush(st.controlBrush);
  ReleaseBrush(st.bannerBrush);
}

void RebuildBrushes(DialogState& st) {
  FreeBrushes(st);
  st.windowBrush = CreateSolidBrush(st.palette.windowBg);
  st.navBrush = CreateSolidBrush(st.palette.navBg);
  st.navSelectedBrush = CreateSolidBrush(st.palette.navSelected);
  st.elevatedBrush = CreateSolidBrush(st.palette.elevated);
  st.controlBrush = CreateSolidBrush(st.palette.controlBg);
  st.bannerBrush = CreateSolidBrush(st.palette.bannerBg);
}

HBRUSH HandleCtlColor(DialogState& st, HDC hdc, HWND child) {
  COLORREF text = st.palette.text;
  COLORREF bg = st.palette.windowBg;
  HBRUSH brush = st.windowBrush;
  wchar_t cls[32] = {};
  if (child) {
    GetClassNameW(child, cls, ARRAYSIZE(cls));
  }
  const bool enabled = !child || IsWindowEnabled(child);
  if (!enabled) {
    text = st.palette.muted;
  }
  if (child && child == st.banner) {
    text = st.palette.bannerText;
    bg = st.palette.bannerBg;
    brush = st.bannerBrush ? st.bannerBrush : st.windowBrush;
  } else if (child && child == st.emptyLabel) {
    text = st.palette.muted;
    bg = st.palette.windowBg;
  } else if (_wcsicmp(cls, L"EDIT") == 0 || _wcsicmp(cls, L"LISTBOX") == 0 || _wcsicmp(cls, L"COMBOLBOX") == 0) {
    bg = st.palette.controlBg;
    brush = st.controlBrush ? st.controlBrush : st.windowBrush;
    if (!enabled) {
      text = st.palette.muted;
    }
  } else if (_wcsicmp(cls, L"STATIC") == 0) {
    bg = st.palette.elevated;
    brush = st.elevatedBrush ? st.elevatedBrush : st.windowBrush;
    for (const CardHead& head : st.headers) {
      if (head.hwnd == child) {
        text = enabled ? st.palette.headerText : st.palette.muted;
        break;
      }
    }
  }
  SetTextColor(hdc, text);
  SetBkColor(hdc, bg);
  SetBkMode(hdc, OPAQUE);
  return brush ? brush : reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
}

void ApplyControlTheme(HWND hwnd) {
  if (!hwnd) {
    return;
  }
  wchar_t cls[32] = {};
  GetClassNameW(hwnd, cls, ARRAYSIZE(cls));
  if (wcscmp(cls, kSliderClass) == 0 || wcscmp(cls, kPreviewClass) == 0 || wcscmp(cls, kScrollClass) == 0 ||
      wcscmp(cls, kContentClass) == 0) {
    return;
  }
    SetWindowTheme(hwnd, L"", L"");
}

void FrameControl(HWND hwnd, COLORREF color) {
  HDC hdc = GetWindowDC(hwnd);
  if (!hdc) {
    return;
  }
  RECT rc = {};
  GetWindowRect(hwnd, &rc);
  OffsetRect(&rc, -rc.left, -rc.top);
  HBRUSH brush = CreateSolidBrush(color);
  if (brush) {
    FrameRect(hdc, &rc, brush);
    InflateRect(&rc, -1, -1);
    FrameRect(hdc, &rc, brush);
    DeleteObject(brush);
  }
  ReleaseDC(hwnd, hdc);
}

// Off-screen buffer. Flush() copies pixels while `dest` is still valid.
// The destructor never blits: a paint DC is released by EndPaint, and a blit
// from ~BufferedDc after that call draws nothing.
struct BufferedDc {
  HDC dest = nullptr;
  HDC mem = nullptr;
  HBITMAP bmp = nullptr;
  HGDIOBJ old = nullptr;
  int width = 1;
  int height = 1;
  bool pending = false;

  BufferedDc() = default;
  BufferedDc(const BufferedDc&) = delete;
  BufferedDc& operator=(const BufferedDc&) = delete;

  void Attach(HDC target, int w, int h) {
    Release();
    dest = target;
    width = (std::max)(1, w);
    height = (std::max)(1, h);
    if (!dest) {
      return;
    }
    mem = CreateCompatibleDC(dest);
    bmp = mem ? CreateCompatibleBitmap(dest, width, height) : nullptr;
    if (mem && bmp) {
      old = SelectObject(mem, bmp);
      pending = true;
    }
  }

  void Flush() {
    if (pending && mem && bmp && dest) {
      BitBlt(dest, 0, 0, width, height, mem, 0, 0, SRCCOPY);
    }
    pending = false;
  }

  void Release() {
    pending = false;
    if (mem && old) {
      SelectObject(mem, old);
    }
    old = nullptr;
    if (bmp) {
      DeleteObject(bmp);
    }
    bmp = nullptr;
    if (mem) {
      DeleteDC(mem);
    }
    mem = nullptr;
    dest = nullptr;
  }

  ~BufferedDc() { Release(); }

  HDC dc() const { return mem ? mem : dest; }
};

// Owns BeginPaint / Flush / EndPaint in that order. Declaring a BufferedDc in
// the same block as EndPaint runs EndPaint first and the blit lands on a
// released DC.
struct PaintSession {
  HWND hwnd = nullptr;
  PAINTSTRUCT ps{};
  HDC paint = nullptr;
  BufferedDc buffer;

  explicit PaintSession(HWND window, int blitHeight = 0) : hwnd(window) {
    paint = hwnd ? BeginPaint(hwnd, &ps) : nullptr;
    RECT rc{};
    if (hwnd) {
      GetClientRect(hwnd, &rc);
    }
    int width = (std::max)(1, static_cast<int>(rc.right - rc.left));
    int height = (std::max)(1, static_cast<int>(rc.bottom - rc.top));
    if (blitHeight > 0 && blitHeight < height) {
      height = blitHeight;
    }
    buffer.Attach(paint, width, height);
  }

  PaintSession(const PaintSession&) = delete;
  PaintSession& operator=(const PaintSession&) = delete;

  ~PaintSession() {
    buffer.Flush();
    if (paint && hwnd) {
      EndPaint(hwnd, &ps);
    }
    paint = nullptr;
  }

  HDC dc() const { return buffer.dc(); }
};

void InvalidateWindowTree(HWND hwnd) {
  if (!hwnd || !IsWindow(hwnd)) {
    return;
  }
  // WS_CLIPCHILDREN excludes children from InvalidateRect. RDW_ALLCHILDREN
  // is what actually marks STATIC labels and custom controls dirty.
  RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
}

SliderState* SliderFrom(HWND hwnd) {
  return reinterpret_cast<SliderState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

void NotifySlider(HWND hwnd) {
  const HWND parent = GetParent(hwnd);
  if (parent) {
    SendMessageW(parent, WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(hwnd), kSliderChanged), reinterpret_cast<LPARAM>(hwnd));
  }
}

void SetSliderValue(HWND hwnd, SliderState& data, int value, bool notify) {
  const int clamped = (std::clamp)(value, data.minValue, data.maxValue);
  if (clamped == data.value) {
    return;
  }
  data.value = clamped;
  InvalidateRect(hwnd, nullptr, FALSE);
  if (notify) {
    NotifySlider(hwnd);
  }
}

int SliderValueFromX(const SliderState& data, const RECT& rc, int x, int dpi) {
  const int labelW = MulDiv(58, dpi > 0 ? dpi : 96, 96);
  const int pad = MulDiv(2, dpi > 0 ? dpi : 96, 96);
  const int left = rc.left + pad;
  const int right = rc.right - labelW - pad;
  if (right <= left || data.maxValue <= data.minValue) {
    return data.minValue;
  }
  const double t = (std::clamp)((x - left) / static_cast<double>(right - left), 0.0, 1.0);
  return data.minValue + static_cast<int>(std::lround(t * (data.maxValue - data.minValue)));
}

void PaintSlider(HWND hwnd, HDC hdc, const DialogState& st, const SliderState& data) {
  RECT rc = {};
  GetClientRect(hwnd, &rc);
  const int dpi = st.dpi > 0 ? st.dpi : 96;
  HBRUSH bg = st.elevatedBrush ? st.elevatedBrush : st.windowBrush;
  if (bg) {
    FillRect(hdc, &rc, bg);
  }
  const bool enabled = IsWindowEnabled(hwnd) != FALSE;
  const int labelW = MulDiv(58, dpi, 96);
  const int pad = MulDiv(2, dpi, 96);
  const int trackH = (std::max)(4, MulDiv(4, dpi, 96));
  RECT track = {rc.left + pad, (rc.top + rc.bottom - trackH) / 2, rc.right - labelW - pad,
                (rc.top + rc.bottom - trackH) / 2 + trackH};
  if (track.right < track.left) {
    track.right = track.left;
  }
  HBRUSH trackBrush = CreateSolidBrush(st.palette.divider);
  HBRUSH fillBrush = CreateSolidBrush(enabled ? st.palette.accent : st.palette.muted);
  if (trackBrush) {
    FillRect(hdc, &track, trackBrush);
    DeleteObject(trackBrush);
  }
  const double span = static_cast<double>(data.maxValue - data.minValue);
  const double t = span > 0.0 ? (data.value - data.minValue) / span : 0.0;
  RECT fill = track;
  fill.right = track.left + static_cast<int>(std::lround(t * (track.right - track.left)));
  if (fillBrush) {
    FillRect(hdc, &fill, fillBrush);
    DeleteObject(fillBrush);
  }
  const int radius = (std::max)(5, MulDiv(7, dpi, 96));
  const int cx = fill.right;
  const int cy = (rc.top + rc.bottom) / 2;
  HBRUSH thumb = CreateSolidBrush(enabled ? st.palette.accent : st.palette.muted);
  HPEN pen = CreatePen(PS_SOLID, 1, ShowFocusCue(hwnd) ? st.palette.focus : st.palette.controlBg);
  HGDIOBJ oldBrush = thumb ? SelectObject(hdc, thumb) : nullptr;
  HGDIOBJ oldPen = pen ? SelectObject(hdc, pen) : nullptr;
  Ellipse(hdc, cx - radius, cy - radius, cx + radius, cy + radius);
  if (oldPen) {
    SelectObject(hdc, oldPen);
  }
  if (oldBrush) {
    SelectObject(hdc, oldBrush);
  }
  if (pen) {
    DeleteObject(pen);
  }
  if (thumb) {
    DeleteObject(thumb);
  }
  wchar_t value[32] = {};
  swprintf_s(value, L"%d", data.value);
  RECT textRc = rc;
  textRc.left = (std::max)(rc.left, rc.right - labelW);
  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, enabled ? st.palette.text : st.palette.muted);
  HFONT font = reinterpret_cast<HFONT>(SendMessageW(hwnd, WM_GETFONT, 0, 0));
  HGDIOBJ oldFont = font ? SelectObject(hdc, font) : nullptr;
  DrawTextW(hdc, value, -1, &textRc, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  if (oldFont) {
    SelectObject(hdc, oldFont);
  }
}

LRESULT CALLBACK SliderProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  SliderState* data = SliderFrom(hwnd);
  DialogState* st = StateFrom(hwnd);
  switch (msg) {
  case WM_CREATE: {
    auto* created = new (std::nothrow) SliderState();
    if (!created) {
      return -1;
    }
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(created));
    return 0;
  }
  case WM_DESTROY:
    delete data;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
    return 0;
  case kSliderSetRange:
    if (data) {
      data->minValue = static_cast<int>(wParam);
      data->maxValue = static_cast<int>(lParam);
      if (data->maxValue < data->minValue) {
        data->maxValue = data->minValue;
      }
      data->value = (std::clamp)(data->value, data->minValue, data->maxValue);
      InvalidateRect(hwnd, nullptr, FALSE);
    }
    return 0;
  case kSliderSetPos:
    if (data) {
      const int previous = data->value;
      data->value = (std::clamp)(static_cast<int>(wParam), data->minValue, data->maxValue);
      if (data->value != previous) {
        InvalidateRect(hwnd, nullptr, FALSE);
      }
    }
    return 0;
  case kSliderGetPos:
    return data ? data->value : 0;
  case WM_GETDLGCODE:
    return DLGC_WANTARROWS;
  case WM_ERASEBKGND:
    return 1;
  case WM_PAINT: {
    PaintSession session(hwnd);
    if (st && data && session.dc()) {
      PaintSlider(hwnd, session.dc(), *st, *data);
    } else if (session.dc()) {
      RECT rc = {};
      GetClientRect(hwnd, &rc);
      FillRect(session.dc(), &rc, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    }
    return 0;
  }
  case WM_ENABLE:
  case WM_SETFOCUS:
  case WM_KILLFOCUS:
    InvalidateRect(hwnd, nullptr, FALSE);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
  case WM_LBUTTONDOWN:
    if (data && st) {
      SetFocus(hwnd);
      SetCapture(hwnd);
      data->dragging = true;
      RECT rc = {};
      GetClientRect(hwnd, &rc);
      const int x = static_cast<int>(static_cast<short>(LOWORD(lParam)));
      SetSliderValue(hwnd, *data, SliderValueFromX(*data, rc, x, st->dpi), true);
    }
    return 0;
  case WM_MOUSEMOVE:
    if (data && st && data->dragging) {
      RECT rc = {};
      GetClientRect(hwnd, &rc);
      const int x = static_cast<int>(static_cast<short>(LOWORD(lParam)));
      SetSliderValue(hwnd, *data, SliderValueFromX(*data, rc, x, st->dpi), true);
      return 0;
    }
    if (data && !data->hot) {
      data->hot = true;
      TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE, hwnd, 0};
      TrackMouseEvent(&track);
      InvalidateRect(hwnd, nullptr, FALSE);
    }
    return 0;
  case WM_MOUSELEAVE:
    if (data) {
      data->hot = false;
      InvalidateRect(hwnd, nullptr, FALSE);
    }
    return 0;
  case WM_LBUTTONUP:
  case WM_CAPTURECHANGED:
    if (data) {
      data->dragging = false;
    }
    if (msg == WM_LBUTTONUP && GetCapture() == hwnd) {
      ReleaseCapture();
    }
    return 0;
  case WM_KEYDOWN:
    if (data) {
      const int page = (std::max)(1, (data->maxValue - data->minValue) / 10);
      int next = data->value;
      switch (wParam) {
      case VK_LEFT:
      case VK_DOWN:
        next -= 1;
        break;
      case VK_RIGHT:
      case VK_UP:
        next += 1;
        break;
      case VK_PRIOR:
        next -= page;
        break;
      case VK_NEXT:
        next += page;
        break;
      case VK_HOME:
        next = data->minValue;
        break;
      case VK_END:
        next = data->maxValue;
        break;
      default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
      }
      SetSliderValue(hwnd, *data, next, true);
      return 0;
    }
    break;
  case WM_MOUSEWHEEL:
    if (data && GetFocus() == hwnd) {
      data->wheelRemainder += GET_WHEEL_DELTA_WPARAM(wParam);
      const int steps = data->wheelRemainder / WHEEL_DELTA;
      data->wheelRemainder -= steps * WHEEL_DELTA;
      if (steps != 0) {
        SetSliderValue(hwnd, *data, data->value + steps, true);
      }
      return 0;
    }
    if (HWND parent = GetParent(hwnd)) {
      return SendMessageW(parent, msg, wParam, lParam);
    }
    break;
  default:
    break;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void PaintFocusRing(HDC hdc, const RECT& rc, COLORREF color) {
  HBRUSH brush = CreateSolidBrush(color);
  if (!brush) {
    return;
  }
  FrameRect(hdc, &rc, brush);
  RECT inner = rc;
  InflateRect(&inner, -1, -1);
  FrameRect(hdc, &inner, brush);
  DeleteObject(brush);
}

void PaintPushButton(HWND hwnd, HDC hdc, const DialogState& st) {
  RECT rc = {};
  GetClientRect(hwnd, &rc);
  const bool enabled = IsWindowEnabled(hwnd) != FALSE;
  const bool primary = GetDlgCtrlID(hwnd) == kIdOk;
  const bool hot = GetPropW(hwnd, kHotProp) != nullptr;
  const bool pressed = (SendMessageW(hwnd, BM_GETSTATE, 0, 0) & BST_PUSHED) != 0;
  COLORREF fill = primary ? st.palette.accent : st.palette.controlBg;
  if (primary && enabled && pressed) {
    fill = st.palette.accentPressed;
  } else if (primary && enabled && hot) {
    fill = st.palette.accentHover;
  } else if (!primary && enabled && pressed) {
    fill = Blend(st.palette.controlBg, st.palette.accent, 28);
  } else if (!primary && enabled && hot) {
    fill = Blend(st.palette.controlBg, st.palette.accent, 14);
  }
  if (!enabled) {
    fill = st.palette.elevated;
  }
  HBRUSH brush = CreateSolidBrush(fill);
  if (brush) {
    FillRect(hdc, &rc, brush);
    DeleteObject(brush);
  }
  if (!primary) {
    HBRUSH border = CreateSolidBrush(st.palette.border);
    if (border) {
      FrameRect(hdc, &rc, border);
      DeleteObject(border);
    }
  }
  if (ShowFocusCue(hwnd)) {
    RECT ring = rc;
    InflateRect(&ring, -2, -2);
    PaintFocusRing(hdc, ring, primary ? st.palette.accentText : st.palette.focus);
  }
  wchar_t text[128] = {};
  GetWindowTextW(hwnd, text, ARRAYSIZE(text));
  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, !enabled ? st.palette.muted : (primary ? st.palette.accentText : st.palette.text));
  HFONT font = reinterpret_cast<HFONT>(SendMessageW(hwnd, WM_GETFONT, 0, 0));
  HGDIOBJ old = font ? SelectObject(hdc, font) : nullptr;
  DrawTextW(hdc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
  if (old) {
    SelectObject(hdc, old);
  }
}

void PaintCheckbox(HWND hwnd, HDC hdc, const DialogState& st) {
  RECT rc = {};
  GetClientRect(hwnd, &rc);
  const int dpi = st.dpi > 0 ? st.dpi : 96;
  const int box = (std::max)(14, MulDiv(16, dpi, 96));
  const int boxTop = (std::max)(0, static_cast<int>(rc.bottom - rc.top - box) / 2);
  RECT boxRc = {rc.left, rc.top + boxTop, rc.left + box, rc.top + boxTop + box};
  const bool enabled = IsWindowEnabled(hwnd) != FALSE;
  const bool checked = SendMessageW(hwnd, BM_GETCHECK, 0, 0) == BST_CHECKED;
  const bool hot = GetPropW(hwnd, kHotProp) != nullptr;
  HBRUSH bg = st.elevatedBrush ? st.elevatedBrush : st.windowBrush;
  if (bg) {
    FillRect(hdc, &rc, bg);
  }
  HBRUSH fill = CreateSolidBrush(checked && enabled ? st.palette.accent : st.palette.controlBg);
  HPEN pen = CreatePen(PS_SOLID, 1, !enabled ? st.palette.muted : (hot || checked ? st.palette.accent : st.palette.border));
  HGDIOBJ oldBrush = fill ? SelectObject(hdc, fill) : nullptr;
  HGDIOBJ oldPen = pen ? SelectObject(hdc, pen) : nullptr;
  RoundRect(hdc, boxRc.left, boxRc.top, boxRc.right, boxRc.bottom, MulDiv(4, dpi, 96), MulDiv(4, dpi, 96));
  if (oldPen) {
    SelectObject(hdc, oldPen);
  }
  if (oldBrush) {
    SelectObject(hdc, oldBrush);
  }
  if (pen) {
    DeleteObject(pen);
  }
  if (fill) {
    DeleteObject(fill);
  }
  if (checked) {
    HPEN mark = CreatePen(PS_SOLID, (std::max)(2, MulDiv(2, dpi, 96)),
                          enabled ? st.palette.accentText : st.palette.muted);
    HGDIOBJ oldMark = mark ? SelectObject(hdc, mark) : nullptr;
    const int x0 = boxRc.left + box / 5;
    const int y0 = boxRc.top + box / 2;
    const int x1 = boxRc.left + box * 2 / 5;
    const int y1 = boxRc.bottom - box / 4;
    const int x2 = boxRc.right - box / 5;
    const int y2 = boxRc.top + box / 4;
    MoveToEx(hdc, x0, y0, nullptr);
    LineTo(hdc, x1, y1);
    LineTo(hdc, x2, y2);
    if (oldMark) {
      SelectObject(hdc, oldMark);
    }
    if (mark) {
      DeleteObject(mark);
    }
  }
  if (ShowFocusCue(hwnd)) {
    RECT ring = boxRc;
    InflateRect(&ring, 2, 2);
    PaintFocusRing(hdc, ring, st.palette.focus);
  }
  wchar_t text[256] = {};
  GetWindowTextW(hwnd, text, ARRAYSIZE(text));
  RECT textRc = rc;
  textRc.left = boxRc.right + MulDiv(8, dpi, 96);
  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, enabled ? st.palette.text : st.palette.muted);
  HFONT font = reinterpret_cast<HFONT>(SendMessageW(hwnd, WM_GETFONT, 0, 0));
  HGDIOBJ oldFont = font ? SelectObject(hdc, font) : nullptr;
  DrawTextW(hdc, text, -1, &textRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
  if (oldFont) {
    SelectObject(hdc, oldFont);
  }
}

void PaintSwatch(HWND hwnd, HDC hdc, const DialogState& st) {
  RECT rc = {};
  GetClientRect(hwnd, &rc);
  HWND edit = reinterpret_cast<HWND>(GetPropW(hwnd, kSwatchProp));
  COLORREF color = st.palette.accent;
  if (edit) {
    wchar_t text[64] = {};
    GetWindowTextW(edit, text, ARRAYSIZE(text));
    COLORREF parsed = 0;
    if (SettingsThemeTryParseHex(text, &parsed)) {
      color = parsed;
    }
  }
  if (!IsWindowEnabled(hwnd)) {
    color = Blend(color, st.palette.elevated, 45);
  }
  HBRUSH fill = CreateSolidBrush(color);
  if (fill) {
    FillRect(hdc, &rc, fill);
    DeleteObject(fill);
  }
  HBRUSH border = CreateSolidBrush(ShowFocusCue(hwnd) ? st.palette.focus : st.palette.border);
  if (border) {
    FrameRect(hdc, &rc, border);
    if (ShowFocusCue(hwnd)) {
      RECT inner = rc;
      InflateRect(&inner, -1, -1);
      FrameRect(hdc, &inner, border);
    }
    DeleteObject(border);
  }
}

void PaintNavButton(HWND hwnd, HDC hdc, const DialogState& st) {
  RECT rc = {};
  GetClientRect(hwnd, &rc);
  const int index = GetDlgCtrlID(hwnd) - kNavIdBase;
  const bool selected =
      index >= 0 && index < static_cast<int>(ARRAYSIZE(kNavOrder)) && kNavOrder[index] == st.category;
  const bool hot = GetPropW(hwnd, kHotProp) != nullptr;
  HBRUSH fill = st.navBrush;
  if (selected && st.navSelectedBrush) {
    fill = st.navSelectedBrush;
  } else if (hot) {
    fill = st.elevatedBrush;
  }
  if (!fill) {
    fill = st.windowBrush;
  }
  if (fill) {
    FillRect(hdc, &rc, fill);
  }
  if (selected) {
    RECT bar = rc;
    bar.right = bar.left + (std::max)(3, Px(st, 3));
    HBRUSH accent = CreateSolidBrush(st.palette.accent);
    if (accent) {
      FillRect(hdc, &bar, accent);
      DeleteObject(accent);
    }
  }
  if (ShowFocusCue(hwnd)) {
    RECT ring = rc;
    InflateRect(&ring, -2, -2);
    PaintFocusRing(hdc, ring, st.palette.focus);
  }
  wchar_t text[128] = {};
  GetWindowTextW(hwnd, text, ARRAYSIZE(text));
  RECT textRc = rc;
  textRc.left += Px(st, 14);
  textRc.right -= Px(st, 8);
  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, selected ? st.palette.text : st.palette.muted);
  HFONT font = selected && st.headerFont ? st.headerFont
                                         : reinterpret_cast<HFONT>(SendMessageW(hwnd, WM_GETFONT, 0, 0));
  HGDIOBJ old = font ? SelectObject(hdc, font) : nullptr;
  DrawTextW(hdc, text, -1, &textRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
  if (old) {
    SelectObject(hdc, old);
  }
}

LRESULT CALLBACK ButtonThemeProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR) {
  DialogState* st = StateFrom(hwnd);
  switch (msg) {
  case WM_ERASEBKGND:
    return 1;
  case WM_PAINT: {
    PaintSession session(hwnd);
    if (st && session.dc()) {
      const LONG style = GetWindowLongW(hwnd, GWL_STYLE);
      if (GetPropW(hwnd, kNavProp)) {
        PaintNavButton(hwnd, session.dc(), *st);
      } else if (GetPropW(hwnd, kSwatchProp)) {
        PaintSwatch(hwnd, session.dc(), *st);
      } else if ((style & BS_TYPEMASK) == BS_AUTOCHECKBOX || (style & BS_TYPEMASK) == BS_CHECKBOX) {
        PaintCheckbox(hwnd, session.dc(), *st);
      } else {
        PaintPushButton(hwnd, session.dc(), *st);
      }
    }
    return 0;
  }
  case WM_MOUSEMOVE:
    if (!GetPropW(hwnd, kHotProp)) {
      SetPropW(hwnd, kHotProp, reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(1)));
      TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE, hwnd, 0};
      TrackMouseEvent(&track);
      InvalidateRect(hwnd, nullptr, FALSE);
    }
    break;
  case WM_MOUSELEAVE:
    RemovePropW(hwnd, kHotProp);
    InvalidateRect(hwnd, nullptr, FALSE);
    break;
  case WM_SETFOCUS:
  case WM_KILLFOCUS:
    InvalidateRect(hwnd, nullptr, FALSE);
    break;
  case WM_ENABLE:
    InvalidateRect(hwnd, nullptr, FALSE);
    return 0;
  case BM_SETCHECK: {
    const LRESULT hit = DefSubclassProc(hwnd, msg, wParam, lParam);
    InvalidateRect(hwnd, nullptr, FALSE);
    return hit;
  }
  case WM_NCDESTROY:
    RemovePropW(hwnd, kHotProp);
    RemovePropW(hwnd, kNavProp);
    RemovePropW(hwnd, kSwatchProp);
    break;
  case WM_MOUSEWHEEL:
    if (HWND parent = GetParent(hwnd)) {
      return SendMessageW(parent, msg, wParam, lParam);
    }
    break;
  default:
    break;
  }
  return DefSubclassProc(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK EditThemeProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR) {
  DialogState* st = StateFrom(hwnd);
  switch (msg) {
  case WM_NCPAINT: {
    const LRESULT hit = DefSubclassProc(hwnd, msg, wParam, lParam);
    if (st) {
      const COLORREF color = ShowFocusCue(hwnd) ? st->palette.focus : st->palette.border;
      FrameControl(hwnd, color);
    }
    return hit;
  }
  case WM_SETFOCUS:
  case WM_KILLFOCUS: {
    const LRESULT hit = DefSubclassProc(hwnd, msg, wParam, lParam);
    RedrawWindow(hwnd, nullptr, nullptr, RDW_FRAME | RDW_INVALIDATE);
    return hit;
  }
  case WM_MOUSEWHEEL:
    if (HWND parent = GetParent(hwnd)) {
      return SendMessageW(parent, msg, wParam, lParam);
    }
    break;
  default:
    break;
  }
  return DefSubclassProc(hwnd, msg, wParam, lParam);
}

int ComboPaintHeight(HWND hwnd) {
  RECT rc = {};
  GetClientRect(hwnd, &rc);
  const int full = (std::max)(1, static_cast<int>(rc.bottom - rc.top));
  COMBOBOXINFO info = {};
  info.cbSize = sizeof(info);
  if (!GetComboBoxInfo(hwnd, &info)) {
    return full;
  }
  const int band = (std::max)(info.rcItem.bottom, info.rcButton.bottom);
  if (band <= 0) {
    return full;
  }
  return (std::min)(full, band + 1);
}

void PaintCombo(HWND hwnd, HDC hdc, const DialogState& st) {
  RECT rc = {};
  GetClientRect(hwnd, &rc);
  const int band = ComboPaintHeight(hwnd);
  if (band > 0 && band < rc.bottom - rc.top) {
    rc.bottom = rc.top + band;
  }
  const bool enabled = IsWindowEnabled(hwnd) != FALSE;
  HBRUSH bg = st.controlBrush ? st.controlBrush : st.windowBrush;
  if (bg) {
    FillRect(hdc, &rc, bg);
  }
  HBRUSH border = CreateSolidBrush(ShowFocusCue(hwnd) ? st.palette.focus : st.palette.border);
  if (border) {
    FrameRect(hdc, &rc, border);
    DeleteObject(border);
  }
  std::wstring text;
  const int sel = static_cast<int>(SendMessageW(hwnd, CB_GETCURSEL, 0, 0));
  if (sel >= 0) {
    const int len = static_cast<int>(SendMessageW(hwnd, CB_GETLBTEXTLEN, static_cast<WPARAM>(sel), 0));
    if (len > 0 && len < 1024) {
      text.resize(static_cast<size_t>(len) + 1);
      SendMessageW(hwnd, CB_GETLBTEXT, static_cast<WPARAM>(sel), reinterpret_cast<LPARAM>(text.data()));
      text.resize(static_cast<size_t>(len));
    }
  }
  RECT textRc = rc;
  textRc.left += Px(st, 8);
  textRc.right -= Px(st, 22);
  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, enabled ? st.palette.text : st.palette.muted);
  HFONT font = reinterpret_cast<HFONT>(SendMessageW(hwnd, WM_GETFONT, 0, 0));
  HGDIOBJ old = font ? SelectObject(hdc, font) : nullptr;
  DrawTextW(hdc, text.c_str(), -1, &textRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
  if (old) {
    SelectObject(hdc, old);
  }
  const int mid = (rc.top + rc.bottom) / 2;
  const int x = rc.right - Px(st, 16);
  POINT chevron[3] = {{x, mid - Px(st, 2)}, {x + Px(st, 8), mid - Px(st, 2)}, {x + Px(st, 4), mid + Px(st, 3)}};
  HBRUSH chevronBrush = CreateSolidBrush(enabled ? st.palette.muted : st.palette.border);
  if (chevronBrush) {
    HGDIOBJ oldBrush = SelectObject(hdc, chevronBrush);
    HGDIOBJ oldPen = SelectObject(hdc, GetStockObject(NULL_PEN));
    Polygon(hdc, chevron, 3);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(chevronBrush);
  }
}

LRESULT CALLBACK ComboListProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR);

void EnsureComboListTheme(HWND combo, const DialogState& st) {
  if (!combo) {
    return;
  }
  COMBOBOXINFO info = {};
  info.cbSize = sizeof(info);
  if (!GetComboBoxInfo(combo, &info) || !info.hwndList) {
    return;
  }
  if (!GetPropW(info.hwndList, kListHookProp)) {
    SetWindowSubclass(info.hwndList, ComboListProc, kSubComboList, 0);
    SetPropW(info.hwndList, kListHookProp, reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(1)));
    SetWindowTheme(info.hwndList, L"", L"");
  }
  if (HFONT font = reinterpret_cast<HFONT>(SendMessageW(combo, WM_GETFONT, 0, 0))) {
    SendMessageW(info.hwndList, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
  }
  int itemH = static_cast<int>(SendMessageW(combo, CB_GETITEMHEIGHT, 0, 0));
  if (itemH < Px(st, 16)) {
    itemH = Px(st, kControlH);
  }
  const int count = static_cast<int>(SendMessageW(combo, CB_GETCOUNT, 0, 0));
  const int visible = (std::clamp)((std::max)(count, 1), 1, 8);
  SetPropW(info.hwndList, kDropMinProp,
           reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(itemH * visible + Px(st, 2))));
}

void RestoreClosedComboHeight(HWND combo, const DialogState& st) {
  if (!combo) {
    return;
  }
  RECT rc = {};
  GetWindowRect(combo, &rc);
  POINT origin = {rc.left, rc.top};
  if (HWND parent = GetParent(combo)) {
    ScreenToClient(parent, &origin);
  }
  const int width = (std::max)(1, static_cast<int>(rc.right - rc.left));
  const int height = (std::max)(1, Px(st, kControlH));
  if (rc.bottom - rc.top == height) {
    return;
  }
  SetWindowPos(combo, nullptr, origin.x, origin.y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
}

LRESULT CALLBACK ComboListProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR) {
  DialogState* st = StateFrom(hwnd);
  switch (msg) {
  case WM_ERASEBKGND:
    if (st && st->controlBrush) {
      RECT rc = {};
      GetClientRect(hwnd, &rc);
      FillRect(reinterpret_cast<HDC>(wParam), &rc, st->controlBrush);
      return 1;
    }
    break;
  case WM_NCPAINT: {
    const LRESULT hit = DefSubclassProc(hwnd, msg, wParam, lParam);
    if (st) {
      FrameControl(hwnd, st->palette.border);
    }
    return hit;
  }
  case WM_WINDOWPOSCHANGING: {
    auto* pos = reinterpret_cast<WINDOWPOS*>(lParam);
    const int minH = static_cast<int>(reinterpret_cast<ULONG_PTR>(GetPropW(hwnd, kDropMinProp)));
    if (pos && minH > 0 && !(pos->flags & SWP_NOSIZE) && !(pos->flags & SWP_HIDEWINDOW) && pos->cy > 0 &&
        pos->cy < minH) {
      pos->cy = minH;
    }
    break;
  }
  case WM_NCDESTROY:
    RemovePropW(hwnd, kListHookProp);
    RemovePropW(hwnd, kDropMinProp);
    break;
  default:
    break;
  }
  return DefSubclassProc(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK ComboThemeProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR) {
  DialogState* st = StateFrom(hwnd);
  switch (msg) {
  case WM_ERASEBKGND:
    return 1;
  case WM_PAINT: {
    if (st) {
      EnsureComboListTheme(hwnd, *st);
    }
    const int band = ComboPaintHeight(hwnd);
    PaintSession session(hwnd, band);
    if (st && session.dc()) {
      PaintCombo(hwnd, session.dc(), *st);
    }
    return 0;
  }
  case WM_CTLCOLORLISTBOX:
    if (st) {
      return reinterpret_cast<LRESULT>(
          HandleCtlColor(*st, reinterpret_cast<HDC>(wParam), reinterpret_cast<HWND>(lParam)));
    }
    break;
  case WM_SETFOCUS:
  case WM_KILLFOCUS:
  case WM_ENABLE:
    InvalidateRect(hwnd, nullptr, FALSE);
    if (msg == WM_ENABLE) {
      return 0;
    }
    break;
  case WM_NCDESTROY: {
    COMBOBOXINFO info = {};
    info.cbSize = sizeof(info);
    if (GetComboBoxInfo(hwnd, &info) && info.hwndList) {
      RemoveWindowSubclass(info.hwndList, ComboListProc, kSubComboList);
      RemovePropW(info.hwndList, kListHookProp);
      RemovePropW(info.hwndList, kDropMinProp);
    }
    break;
  }
  case WM_MOUSEWHEEL:
    if (SendMessageW(hwnd, CB_GETDROPPEDSTATE, 0, 0) == FALSE) {
      if (HWND parent = GetParent(hwnd)) {
        return SendMessageW(parent, msg, wParam, lParam);
      }
    }
    break;
  default:
    break;
  }
  return DefSubclassProc(hwnd, msg, wParam, lParam);
}

void PaintHeader(HWND hwnd, HDC hdc, const DialogState& st) {
  RECT rc = {};
  GetClientRect(hwnd, &rc);
  if (st.elevatedBrush) {
    FillRect(hdc, &rc, st.elevatedBrush);
  }
  wchar_t text[128] = {};
  GetWindowTextW(hwnd, text, ARRAYSIZE(text));
  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, st.palette.headerText);
  HFONT font = st.headerFont ? st.headerFont : reinterpret_cast<HFONT>(SendMessageW(hwnd, WM_GETFONT, 0, 0));
  HGDIOBJ old = font ? SelectObject(hdc, font) : nullptr;
  RECT textRc = rc;
  textRc.bottom -= 2;
  DrawTextW(hdc, text, -1, &textRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
  if (old) {
    SelectObject(hdc, old);
  }
  HPEN pen = CreatePen(PS_SOLID, 1, st.palette.divider);
  if (pen) {
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    MoveToEx(hdc, rc.left, rc.bottom - 1, nullptr);
    LineTo(hdc, rc.right, rc.bottom - 1);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
  }
}

LRESULT CALLBACK HeaderThemeProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR) {
  DialogState* st = StateFrom(hwnd);
  switch (msg) {
  case WM_ERASEBKGND:
    return 1;
  case WM_PAINT: {
    PaintSession session(hwnd);
    if (st && session.dc()) {
      PaintHeader(hwnd, session.dc(), *st);
    }
    return 0;
  }
  case WM_MOUSEWHEEL:
    if (HWND parent = GetParent(hwnd)) {
      return SendMessageW(parent, msg, wParam, lParam);
    }
    break;
  default:
    break;
  }
  return DefSubclassProc(hwnd, msg, wParam, lParam);
}

void PaintLabel(HWND hwnd, HDC hdc, const DialogState& st) {
  RECT rc = {};
  GetClientRect(hwnd, &rc);
  const bool banner = hwnd == st.banner;
  const bool empty = hwnd == st.emptyLabel;
  const bool muted = GetPropW(hwnd, kMutedProp) != nullptr || IsWindowEnabled(hwnd) == FALSE;
  HBRUSH bg = st.elevatedBrush ? st.elevatedBrush : st.windowBrush;
  COLORREF text = muted ? st.palette.muted : st.palette.text;
  if (banner) {
    bg = st.bannerBrush ? st.bannerBrush : st.windowBrush;
    text = st.palette.bannerText;
  } else if (empty) {
    bg = st.windowBrush ? st.windowBrush : st.elevatedBrush;
    text = st.palette.muted;
  }
  if (bg) {
    FillRect(hdc, &rc, bg);
  }
  wchar_t textBuf[512] = {};
  GetWindowTextW(hwnd, textBuf, ARRAYSIZE(textBuf));
  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, text);
  HFONT font = reinterpret_cast<HFONT>(SendMessageW(hwnd, WM_GETFONT, 0, 0));
  HGDIOBJ old = font ? SelectObject(hdc, font) : nullptr;
  DrawTextW(hdc, textBuf, -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
  if (old) {
    SelectObject(hdc, old);
  }
}

LRESULT CALLBACK LabelThemeProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR) {
  DialogState* st = StateFrom(hwnd);
  switch (msg) {
  case WM_ERASEBKGND:
    return 1;
  case WM_PAINT: {
    PaintSession session(hwnd);
    if (st && session.dc()) {
      PaintLabel(hwnd, session.dc(), *st);
    }
    return 0;
  }
  case WM_ENABLE:
    InvalidateRect(hwnd, nullptr, FALSE);
    return 0;
  case WM_SETTEXT: {
    const LRESULT hit = DefSubclassProc(hwnd, msg, wParam, lParam);
    InvalidateRect(hwnd, nullptr, FALSE);
    return hit;
  }
  case WM_NCDESTROY:
    RemovePropW(hwnd, kMutedProp);
    break;
  case WM_MOUSEWHEEL:
    if (HWND parent = GetParent(hwnd)) {
      return SendMessageW(parent, msg, wParam, lParam);
    }
    break;
  default:
    break;
  }
  return DefSubclassProc(hwnd, msg, wParam, lParam);
}

std::wstring ValueForKey(const DialogState& st, const wchar_t* key);

void PaintPreview(HWND hwnd, HDC hdc, const DialogState& st) {
  RECT rc = {};
  GetClientRect(hwnd, &rc);
  if (st.elevatedBrush) {
    FillRect(hdc, &rc, st.elevatedBrush);
  }
  HBRUSH border = CreateSolidBrush(st.palette.border);
  if (border) {
    FrameRect(hdc, &rc, border);
    DeleteObject(border);
  }
  RECT title = rc;
  title.left += Px(st, 14);
  title.top += Px(st, 8);
  title.bottom = title.top + Px(st, 18);
  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, st.palette.muted);
  HFONT ui = st.uiFont ? st.uiFont : reinterpret_cast<HFONT>(SendMessageW(hwnd, WM_GETFONT, 0, 0));
  HGDIOBJ old = ui ? SelectObject(hdc, ui) : nullptr;
  DrawTextW(hdc, SettingsUiLoc::PreviewTitle(st.lang), -1, &title,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

  const std::wstring shape = ValueForKey(st, L"Appearance.ShapeStyle");
  const std::wstring borderMode = ValueForKey(st, L"Themes.ContourBorderMode");
  int opacity = _wtoi(ValueForKey(st, L"Themes.PillOpacity").c_str());
  if (opacity < 35) {
    opacity = 35;
  }
  if (opacity > 100) {
    opacity = 100;
  }
  const bool notch = _wcsicmp(shape.c_str(), L"notch") == 0;
  const bool flyout = _wcsicmp(shape.c_str(), L"w11") == 0;
  const bool borderless = _wcsicmp(borderMode.c_str(), L"borderless") == 0;
  const bool autoBorder = _wcsicmp(borderMode.c_str(), L"auto") == 0;
  const COLORREF shown = Blend(st.palette.elevated, st.palette.pillBg, opacity);
  const int pillH = Px(st, notch ? 40 : 46);
  int pillW = (std::min)(Px(st, 420), (std::max)(Px(st, 160), static_cast<int>(rc.right - rc.left) - Px(st, 48)));
  RECT pill = {};
  pill.left = rc.left + ((rc.right - rc.left) - pillW) / 2;
  pill.right = pill.left + pillW;
  pill.top = notch ? title.bottom + Px(st, 4) : rc.bottom - Px(st, 18) - pillH;
  pill.bottom = pill.top + pillH;
  if (pill.top < title.bottom) {
    pill.top = title.bottom;
    pill.bottom = pill.top + pillH;
  }
  HBRUSH fill = CreateSolidBrush(shown);
  HPEN pen = borderless ? reinterpret_cast<HPEN>(GetStockObject(NULL_PEN))
                        : CreatePen(PS_SOLID, (std::max)(1, Px(st, 1)),
                                    autoBorder ? st.palette.accent : st.palette.pillBorder);
  const bool ownedPen = !borderless;
  HGDIOBJ oldBrush = fill ? SelectObject(hdc, fill) : nullptr;
  HGDIOBJ oldPen = pen ? SelectObject(hdc, pen) : nullptr;
  const int radius = notch ? Px(st, 14) : (flyout ? Px(st, 8) : pillH / 2);
  RoundRect(hdc, pill.left, pill.top, pill.right, pill.bottom, radius * 2, radius * 2);
  if (oldPen) {
    SelectObject(hdc, oldPen);
  }
  if (oldBrush) {
    SelectObject(hdc, oldBrush);
  }
  oldPen = nullptr;
  oldBrush = nullptr;
  if (notch && fill) {
    RECT cap = pill;
    cap.bottom = (std::min)(pill.bottom, pill.top + radius);
    FillRect(hdc, &cap, fill);
    if (!borderless && pen) {
      HGDIOBJ stroke = SelectObject(hdc, pen);
      MoveToEx(hdc, pill.left, pill.top, nullptr);
      LineTo(hdc, pill.right, pill.top);
      MoveToEx(hdc, pill.left, pill.top, nullptr);
      LineTo(hdc, pill.left, cap.bottom);
      MoveToEx(hdc, pill.right - 1, pill.top, nullptr);
      LineTo(hdc, pill.right - 1, cap.bottom);
      if (stroke) {
        SelectObject(hdc, stroke);
      }
    }
  }
  if (ownedPen && pen) {
    DeleteObject(pen);
  }
  if (fill) {
    DeleteObject(fill);
  }

  RECT textRc = pill;
  InflateRect(&textRc, -Px(st, 16), 0);
  textRc.right -= Px(st, 18);
  SetTextColor(hdc, st.palette.pillText);
  HFONT preview = st.previewFont ? st.previewFont : ui;
  if (preview) {
    SelectObject(hdc, preview);
  }
  DrawTextW(hdc, L"9:41", -1, &textRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  if (ui) {
    SelectObject(hdc, ui);
  }
  SetTextColor(hdc, st.palette.pillMuted);
  DrawTextW(hdc, L"Lux", -1, &textRc, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  const int dot = Px(st, 8);
  const int cy = (pill.top + pill.bottom) / 2;
  const int cx = pill.right - Px(st, 18);
  HBRUSH dotBrush = CreateSolidBrush(st.palette.accent);
  if (dotBrush) {
    HGDIOBJ prevBrush = SelectObject(hdc, dotBrush);
    HGDIOBJ prevPen = SelectObject(hdc, GetStockObject(NULL_PEN));
    Ellipse(hdc, cx, cy - dot / 2, cx + dot, cy + dot / 2);
    SelectObject(hdc, prevPen);
    SelectObject(hdc, prevBrush);
    DeleteObject(dotBrush);
  }
  if (old) {
    SelectObject(hdc, old);
  }
}

LRESULT CALLBACK PreviewProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  DialogState* st = StateFrom(hwnd);
  switch (msg) {
  case WM_ERASEBKGND:
    return 1;
  case WM_PAINT: {
    PaintSession session(hwnd);
    if (st && session.dc()) {
      PaintPreview(hwnd, session.dc(), *st);
    } else if (session.dc()) {
      RECT rc = {};
      GetClientRect(hwnd, &rc);
      FillRect(session.dc(), &rc, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    }
    return 0;
  }
  case WM_MOUSEWHEEL:
    if (HWND parent = GetParent(hwnd)) {
      return SendMessageW(parent, msg, wParam, lParam);
    }
    break;
  default:
    break;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool ContainsNoCase(const wchar_t* haystack, const wchar_t* needle) {
  if (!haystack || !needle || !needle[0]) {
    return true;
  }
  for (const wchar_t* h = haystack; *h; ++h) {
    const wchar_t* a = h;
    const wchar_t* b = needle;
    while (*a && *b && towlower(*a) == towlower(*b)) {
      ++a;
      ++b;
    }
    if (!*b) {
      return true;
    }
  }
  return false;
}

bool RowMatchesFilter(const RowRef& row, const wchar_t* filter) {
  const SettingDef& def = kSettingSchema[row.schemaIndex];
  return ContainsNoCase(def.key, filter) || ContainsNoCase(row.labelText.c_str(), filter);
}

bool RowVisible(const DialogState& st, const RowRef& row) {
  if (!RowMatchesFilter(row, st.filter)) {
    return false;
  }
  if (st.filter[0] != L'\0') {
    return true;
  }
  return kSettingSchema[row.schemaIndex].group == st.category;
}

bool ShowPreview(const DialogState& st) {
  if (st.filter[0] == L'\0') {
    return st.category == SettingGroup::Themes;
  }
  for (const RowRef& row : st.rows) {
    if (RowMatchesFilter(row, st.filter) && kSettingSchema[row.schemaIndex].group == SettingGroup::Themes) {
      return true;
    }
  }
  return false;
}

void UpdateScrollBar(DialogState& st) {
  if (!st.scroll) {
    return;
  }
  RECT rc = {};
  GetClientRect(st.scroll, &rc);
  const int page = (std::max)(1, static_cast<int>(rc.bottom - rc.top));
  const int maxScroll = (std::max)(0, st.contentHeight - page);
  st.scrollY = (std::clamp)(st.scrollY, 0, maxScroll);
  SCROLLINFO si = {};
  si.cbSize = sizeof(si);
  si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL;
  si.nMin = 0;
  si.nMax = (std::max)(st.contentHeight - 1, 0);
  si.nPage = static_cast<UINT>(page);
  si.nPos = st.scrollY;
  SetScrollInfo(st.scroll, SB_VERT, &si, TRUE);
}

void PlaceContent(DialogState& st) {
  if (!st.content || !st.scroll) {
    return;
  }
  RECT rc = {};
  GetClientRect(st.scroll, &rc);
  const int viewW = (std::max)(1, static_cast<int>(rc.right - rc.left));
  const int height = (std::max)(st.contentHeight, static_cast<int>(rc.bottom - rc.top));
  SetWindowPos(st.content, nullptr, 0, -st.scrollY, viewW, height, SWP_NOZORDER | SWP_NOACTIVATE);
}

void ScrollTo(DialogState& st, int pos) {
  if (!st.scroll) {
    return;
  }
  RECT rc = {};
  GetClientRect(st.scroll, &rc);
  const int page = (std::max)(1, static_cast<int>(rc.bottom - rc.top));
  const int maxScroll = (std::max)(0, st.contentHeight - page);
  const int next = (std::clamp)(pos, 0, maxScroll);
  if (next == st.scrollY) {
    return;
  }
  st.scrollY = next;
  UpdateScrollBar(st);
  PlaceContent(st);
}

LRESULT PaintContentBackground(HWND hwnd, HDC hdc, DialogState* st) {
  RECT rc = {};
  GetClientRect(hwnd, &rc);
  if (!st || !st->windowBrush) {
    FillRect(hdc, &rc, reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
    return 1;
  }
  FillRect(hdc, &rc, st->windowBrush);
  if (st->elevatedBrush) {
    for (const RECT& card : st->cardRects) {
      FillRect(hdc, &card, st->elevatedBrush);
      HBRUSH border = CreateSolidBrush(st->palette.border);
      if (border) {
        FrameRect(hdc, &card, border);
        DeleteObject(border);
      }
    }
  }
  return 1;
}

LRESULT CALLBACK ContentProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK ScrollProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

void EnsureWindowClasses() {
  static bool registered = false;
  if (registered) {
    return;
  }
  const HINSTANCE instance = GetModuleHandleW(nullptr);
  auto reg = [&](const wchar_t* name, WNDPROC proc) {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = proc;
    wc.hInstance = instance;
    // MAKEINTRESOURCEW: clangd without UNICODE treats IDC_ARROW as LPSTR.
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    wc.lpszClassName = name;
    wc.style = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
    RegisterClassExW(&wc);
  };
  reg(kScrollClass, ScrollProc);
  reg(kContentClass, ContentProc);
  reg(kSliderClass, SliderProc);
  reg(kPreviewClass, PreviewProc);
  registered = true;
}

std::wstring GetControlText(HWND control) {
  const int len = GetWindowTextLengthW(control);
  if (len <= 0) {
    return {};
  }
  std::wstring text(static_cast<size_t>(len) + 1, L'\0');
  const int copied = GetWindowTextW(control, &text[0], len + 1);
  if (copied <= 0) {
    return {};
  }
  text.resize(static_cast<size_t>(copied));
  return text;
}

bool IsSlider(HWND hwnd) {
  wchar_t cls[64] = {};
  GetClassNameW(hwnd, cls, ARRAYSIZE(cls));
  return wcscmp(cls, kSliderClass) == 0;
}

bool IsComboBox(HWND hwnd) {
  wchar_t cls[32] = {};
  if (!hwnd) {
    return false;
  }
  GetClassNameW(hwnd, cls, ARRAYSIZE(cls));
  return _wcsicmp(cls, L"COMBOBOX") == 0;
}

void SyncComboItemHeight(HWND combo, const DialogState& st) {
  if (!combo) {
    return;
  }
  HFONT font = reinterpret_cast<HFONT>(SendMessageW(combo, WM_GETFONT, 0, 0));
  if (!font) {
    return;
  }
  HDC hdc = GetDC(combo);
  if (!hdc) {
    return;
  }
  HGDIOBJ old = SelectObject(hdc, font);
  TEXTMETRICW metrics = {};
  const BOOL measured = GetTextMetricsW(hdc, &metrics);
  if (old) {
    SelectObject(hdc, old);
  }
  ReleaseDC(combo, hdc);
  if (!measured) {
    return;
  }
  const int itemH = (std::max)(Px(st, 16), static_cast<int>(metrics.tmHeight) + Px(st, 6));
  SendMessageW(combo, CB_SETITEMHEIGHT, 0, itemH);
  const int selection = (std::min)(itemH, (std::max)(Px(st, 16), Px(st, kControlH) - Px(st, 2)));
  SendMessageW(combo, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1), selection);
}

int EnumIndexForValue(const SettingDef& def, const std::wstring& value) {
  for (size_t e = 0; e < def.enumCount; ++e) {
    if (_wcsicmp(def.enums[e].value, value.c_str()) == 0) {
      return static_cast<int>(e);
    }
  }
  return -1;
}

int EnumDefaultIndex(const SettingDef& def) {
  for (size_t e = 0; e < def.enumCount; ++e) {
    if (def.defaultString && _wcsicmp(def.enums[e].value, def.defaultString) == 0) {
      return static_cast<int>(e);
    }
  }
  return 0;
}

std::wstring ComboEnumValue(const RowRef& row) {
  const SettingDef& def = kSettingSchema[row.schemaIndex];
  const int sel = static_cast<int>(SendMessageW(row.control, CB_GETCURSEL, 0, 0));
  if (sel < 0) {
    return def.defaultString ? def.defaultString : L"";
  }
  if (static_cast<size_t>(sel) < def.enumCount) {
    return def.enums[sel].value;
  }
  const int len = static_cast<int>(SendMessageW(row.control, CB_GETLBTEXTLEN, sel, 0));
  if (len <= 0) {
    return def.defaultString ? def.defaultString : L"";
  }
  std::wstring text(static_cast<size_t>(len) + 1, L'\0');
  SendMessageW(row.control, CB_GETLBTEXT, static_cast<WPARAM>(sel), reinterpret_cast<LPARAM>(text.data()));
  text.resize(static_cast<size_t>(len));
  return text;
}

void SelectEnumValue(HWND combo, const SettingDef& def, const std::wstring& value) {
  const int matched = EnumIndexForValue(def, value);
  if (matched >= 0) {
    SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(matched), 0);
    InvalidateRect(combo, nullptr, FALSE);
    return;
  }
  if (!value.empty()) {
    const int added =
        static_cast<int>(SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value.c_str())));
    if (added >= 0) {
      SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(added), 0);
      InvalidateRect(combo, nullptr, FALSE);
      return;
    }
  }
  SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(EnumDefaultIndex(def)), 0);
  InvalidateRect(combo, nullptr, FALSE);
}

std::wstring ReadRowValue(const RowRef& row) {
  const SettingDef& def = kSettingSchema[row.schemaIndex];
  if (def.type == SettingType::Bool) {
    return SendMessageW(row.control, BM_GETCHECK, 0, 0) == BST_CHECKED ? L"1" : L"0";
  }
  if (def.type == SettingType::Enum && def.enums && def.enumCount > 0) {
    return ComboEnumValue(row);
  }
  if (UseSlider(def) && IsSlider(row.control)) {
    const int value = static_cast<int>(SendMessageW(row.control, kSliderGetPos, 0, 0));
    return std::to_wstring(value);
  }
  const std::wstring text = GetControlText(row.control);
  if (def.type == SettingType::Int) {
    wchar_t* end = nullptr;
    long value = wcstol(text.c_str(), &end, 10);
    while (end && iswspace(*end)) {
      ++end;
    }
    if (end == text.c_str() || (end && *end != L'\0')) {
      value = wcstol(def.defaultString ? def.defaultString : L"0", nullptr, 10);
    }
    if (def.minValue < def.maxValue) {
      value = (std::max)(static_cast<long>(def.minValue), value);
      value = (std::min)(static_cast<long>(def.maxValue), value);
    }
    return std::to_wstring(value);
  }
  return text;
}

std::wstring ValueForKey(const DialogState& st, const wchar_t* key) {
  if (!key) {
    return {};
  }
  for (const RowRef& row : st.rows) {
    if (wcscmp(kSettingSchema[row.schemaIndex].key, key) == 0) {
      return ReadRowValue(row);
    }
  }
  return {};
}

struct DialogLookup final : SettingsValueLookup {
  const DialogState* state = nullptr;
  explicit DialogLookup(const DialogState* value) : state(value) {}
  std::wstring Value(const wchar_t* key) const override { return state ? ValueForKey(*state, key) : std::wstring(); }
};

void WriteRowValue(const RowRef& row, const std::wstring& value) {
    const SettingDef& def = kSettingSchema[row.schemaIndex];
  if (def.type == SettingType::Bool) {
    SendMessageW(row.control, BM_SETCHECK, value == L"1" ? BST_CHECKED : BST_UNCHECKED, 0);
    InvalidateRect(row.control, nullptr, FALSE);
    return;
  }
  if (def.type == SettingType::Enum && def.enums && def.enumCount > 0) {
    SelectEnumValue(row.control, def, value);
    return;
  }
  if (UseSlider(def) && IsSlider(row.control)) {
    SendMessageW(row.control, kSliderSetPos, static_cast<WPARAM>(wcstol(value.c_str(), nullptr, 10)), 0);
    return;
  }
  SetWindowTextW(row.control, value.c_str());
}

void CaptureBaseline(DialogState& st) {
  st.baseline.clear();
  st.baseline.reserve(st.rows.size());
  for (const RowRef& row : st.rows) {
    st.baseline.push_back(ReadRowValue(row));
  }
}

void RestoreBaseline(DialogState& st) {
  UpdateGuard guard(st.updating);
  const size_t n = (std::min)(st.rows.size(), st.baseline.size());
  for (size_t i = 0; i < n; ++i) {
    WriteRowValue(st.rows[i], st.baseline[i]);
  }
}

SettingsThemeSource ThemeSourceFrom(const DialogState& st) {
  SettingsThemeSource source;
  source.preset = ValueForKey(st, L"Themes.ThemePreset");
  source.pillBg = ValueForKey(st, L"Themes.PillBgColor");
  source.textPrimary = ValueForKey(st, L"Themes.TextPrimaryColor");
  source.textSecondary = ValueForKey(st, L"Themes.TextSecondaryColor");
  source.borderHex = ValueForKey(st, L"Themes.ContourBorderHex");
  source.accentMode = ValueForKey(st, L"Themes.AccentColorMode");
  source.customAccent = ValueForKey(st, L"Themes.CustomAccentHex");
  source.adaptive = ValueForKey(st, L"Themes.AdaptiveTheme") == L"1";
  source.systemAccent = SettingsThemeSystemAccent();
  return source;
}

void ApplyFonts(DialogState& st) {
  const auto assign = [&](HWND hwnd, HFONT font) {
    if (hwnd && font) {
      SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
  };
  assign(st.search, st.uiFont);
  assign(st.okBtn, st.uiFont);
  assign(st.cancelBtn, st.uiFont);
  assign(st.applyBtn, st.uiFont);
  assign(st.resetBtn, st.uiFont);
  assign(st.emptyLabel, st.uiFont);
  assign(st.banner, st.uiFont);
  assign(st.preview, st.uiFont);
  for (NavItem& item : st.nav) {
    assign(item.button, st.uiFont);
  }
  for (CardHead& head : st.headers) {
    assign(head.hwnd, st.headerFont ? st.headerFont : st.uiFont);
  }
  for (RowRef& row : st.rows) {
    assign(row.label, st.uiFont);
    assign(row.control, st.uiFont);
    assign(row.swatch, st.uiFont);
    if (IsComboBox(row.control)) {
      SyncComboItemHeight(row.control, st);
      EnsureComboListTheme(row.control, st);
    }
  }
}

void RebuildFonts(DialogState& st) {
  HFONT next = CreateUiFont(st.dpi);
  HFONT header = CreateWeightFont(next, st.dpi, FW_SEMIBOLD, 0);
  HFONT preview = CreateWeightFont(next, st.dpi, FW_SEMIBOLD, 20);
  HFONT previous = st.uiFont;
  HFONT previousHeader = st.headerFont;
  HFONT previousPreview = st.previewFont;
  st.uiFont = next;
  st.headerFont = header;
  st.previewFont = preview;
  ApplyFonts(st);
  if (previous) {
    DeleteObject(previous);
  }
  if (previousHeader) {
    DeleteObject(previousHeader);
  }
  if (previousPreview) {
    DeleteObject(previousPreview);
  }
}

void ThemeAllControls(DialogState& st) {
  ApplyControlTheme(st.search);
  ApplyControlTheme(st.okBtn);
  ApplyControlTheme(st.cancelBtn);
  ApplyControlTheme(st.applyBtn);
  ApplyControlTheme(st.resetBtn);
  ApplyControlTheme(st.emptyLabel);
  ApplyControlTheme(st.banner);
  for (NavItem& item : st.nav) {
    ApplyControlTheme(item.button);
  }
  for (CardHead& head : st.headers) {
    ApplyControlTheme(head.hwnd);
  }
  for (RowRef& row : st.rows) {
    ApplyControlTheme(row.label);
    ApplyControlTheme(row.control);
    ApplyControlTheme(row.swatch);
  }
}

void RefreshTheme(HWND hwnd, DialogState& st);
void UpdateEnabled(DialogState& st);
void LayoutShell(HWND hwnd, DialogState& st, bool placeOuter);

void InvalidateChrome(HWND hwnd, DialogState& st) {
  RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME | RDW_UPDATENOW);
  if (st.preview) {
    InvalidateRect(st.preview, nullptr, TRUE);
  }
}

void SetBanner(HWND hwnd, DialogState& st, const wchar_t* text) {
  st.bannerText = text ? text : L"";
  if (st.banner) {
    SetWindowTextW(st.banner, st.bannerText.c_str());
  }
  LayoutShell(hwnd, st, false);
}

void ClearBanner(HWND hwnd, DialogState& st) {
  if (st.bannerText.empty()) {
    return;
  }
  SetBanner(hwnd, st, L"");
}

int CardRank(SettingsUiLoc::SettingsCard card) {
  switch (card) {
  case SettingsUiLoc::SettingsCard::ThemeLook:
    return 0;
  case SettingsUiLoc::SettingsCard::ThemeAccent:
    return 1;
  case SettingsUiLoc::SettingsCard::ThemeSurface:
    return 2;
  case SettingsUiLoc::SettingsCard::ThemeBackdrop:
    return 3;
  case SettingsUiLoc::SettingsCard::ThemeAura:
    return 4;
  case SettingsUiLoc::SettingsCard::Appearance:
    return 5;
  case SettingsUiLoc::SettingsCard::Media:
    return 10;
  case SettingsUiLoc::SettingsCard::MediaClock:
    return 11;
  case SettingsUiLoc::SettingsCard::Lyrics:
    return 12;
  case SettingsUiLoc::SettingsCard::Volume:
    return 13;
  case SettingsUiLoc::SettingsCard::Clipboard:
    return 14;
  case SettingsUiLoc::SettingsCard::Battery:
    return 15;
  case SettingsUiLoc::SettingsCard::Progress:
    return 16;
  case SettingsUiLoc::SettingsCard::CapsLock:
    return 17;
  case SettingsUiLoc::SettingsCard::Weather:
    return 18;
  case SettingsUiLoc::SettingsCard::Calendar:
    return 19;
  case SettingsUiLoc::SettingsCard::Monitor:
    return 20;
  case SettingsUiLoc::SettingsCard::Bluetooth:
    return 21;
  case SettingsUiLoc::SettingsCard::DoNotDisturb:
    return 22;
  case SettingsUiLoc::SettingsCard::FocusTimer:
    return 23;
  case SettingsUiLoc::SettingsCard::QuickLookup:
    return 24;
  case SettingsUiLoc::SettingsCard::FileTray:
    return 25;
  case SettingsUiLoc::SettingsCard::GameOverlay:
    return 26;
  case SettingsUiLoc::SettingsCard::Behavior:
    return 27;
  case SettingsUiLoc::SettingsCard::Animations:
    return 28;
  case SettingsUiLoc::SettingsCard::Indicators:
    return 29;
  case SettingsUiLoc::SettingsCard::Shortcuts:
    return 30;
  case SettingsUiLoc::SettingsCard::General:
    return 31;
  default: {
    const SettingsUiLoc::SettingsCard unhandled = card;
    (void)unhandled;
    return 100;
  }
  }
}

bool KeyEquals(const wchar_t* key, const wchar_t* expected) {
  return key && expected && wcscmp(key, expected) == 0;
}

bool KeyPrefix(const wchar_t* key, const wchar_t* prefix) {
  return key && prefix && wcsncmp(key, prefix, wcslen(prefix)) == 0;
}

SettingsUiLoc::SettingsCard CardForKey(const wchar_t* key, SettingGroup group) {
  if (KeyEquals(key, L"Themes.ThemePreset") || KeyEquals(key, L"Themes.AdaptiveTheme") ||
      KeyEquals(key, L"Themes.FontFamily")) {
    return SettingsUiLoc::SettingsCard::ThemeLook;
  }
  if (KeyEquals(key, L"Themes.AccentColorMode") || KeyEquals(key, L"Themes.CustomAccentHex") ||
      KeyEquals(key, L"Themes.AccentBloom") || KeyEquals(key, L"Themes.CalendarAccent") ||
      KeyEquals(key, L"Themes.ClockAccentGlow")) {
    return SettingsUiLoc::SettingsCard::ThemeAccent;
  }
  if (KeyEquals(key, L"Themes.PillOpacity") || KeyEquals(key, L"Themes.TextScale") ||
      KeyEquals(key, L"Themes.TintIntensity") || KeyEquals(key, L"Themes.MaterialDepth") ||
      KeyEquals(key, L"Themes.DropShadow") || KeyEquals(key, L"Themes.ContourBorderMode") ||
      KeyEquals(key, L"Themes.ContourBorderHex") || KeyEquals(key, L"Themes.PillBgColor") ||
      KeyEquals(key, L"Themes.TextPrimaryColor") || KeyEquals(key, L"Themes.TextSecondaryColor")) {
    return SettingsUiLoc::SettingsCard::ThemeSurface;
  }
  if (KeyEquals(key, L"Themes.BackdropMaterial") || KeyEquals(key, L"Themes.BackdropTint") ||
      KeyEquals(key, L"Themes.BackdropFillOpacity")) {
    return SettingsUiLoc::SettingsCard::ThemeBackdrop;
  }
  if (KeyEquals(key, L"Themes.AuraStyle") || KeyEquals(key, L"Themes.AuraIntensity")) {
    return SettingsUiLoc::SettingsCard::ThemeAura;
  }
  if (KeyEquals(key, L"Modules.Media") || KeyEquals(key, L"Modules.MediaAutoExpand") ||
      KeyEquals(key, L"Modules.MediaExpandBlocklist") || KeyEquals(key, L"Modules.MediaLikeBridge") ||
      KeyEquals(key, L"Modules.MediaLikeBridgePort")) {
    return SettingsUiLoc::SettingsCard::Media;
  }
  if (KeyEquals(key, L"Modules.MediaPillClock") || KeyEquals(key, L"Modules.MediaPillDate") ||
      KeyEquals(key, L"Modules.ClockFormat") || KeyEquals(key, L"Modules.DateFormat") ||
      KeyEquals(key, L"Modules.ShowSeconds") || KeyEquals(key, L"Modules.DateFirst")) {
    return SettingsUiLoc::SettingsCard::MediaClock;
  }
  if (KeyPrefix(key, L"Modules.Lyrics") || KeyPrefix(key, L"Modules.Karaoke") ||
      KeyEquals(key, L"Modules.CollapsedLyrics") || KeyEquals(key, L"Modules.MediaTabLyrics")) {
    return SettingsUiLoc::SettingsCard::Lyrics;
  }
  if (KeyEquals(key, L"Modules.Volume")) {
    return SettingsUiLoc::SettingsCard::Volume;
  }
  if (KeyEquals(key, L"Modules.Clipboard")) {
    return SettingsUiLoc::SettingsCard::Clipboard;
  }
  if (KeyEquals(key, L"Modules.Battery")) {
    return SettingsUiLoc::SettingsCard::Battery;
  }
  if (KeyEquals(key, L"Modules.Progress") || KeyEquals(key, L"Modules.StatusCountdownProgress")) {
    return SettingsUiLoc::SettingsCard::Progress;
  }
  if (KeyEquals(key, L"Modules.CapsLock")) {
    return SettingsUiLoc::SettingsCard::CapsLock;
  }
  if (KeyPrefix(key, L"Modules.Weather")) {
    return SettingsUiLoc::SettingsCard::Weather;
  }
  if (KeyEquals(key, L"Modules.CalendarModule") || KeyEquals(key, L"Modules.FirstDayOfWeek")) {
    return SettingsUiLoc::SettingsCard::Calendar;
  }
  if (KeyEquals(key, L"Modules.HardwareMonitorModule") || KeyEquals(key, L"Modules.ShowMetricText")) {
    return SettingsUiLoc::SettingsCard::Monitor;
  }
  if (KeyPrefix(key, L"Modules.Bluetooth")) {
    return SettingsUiLoc::SettingsCard::Bluetooth;
  }
  if (KeyEquals(key, L"Modules.DoNotDisturbIndicator") || KeyEquals(key, L"Modules.NotificationRespectDnD") ||
      KeyEquals(key, L"Modules.Notifications") || KeyEquals(key, L"Modules.MessengerApps")) {
    return SettingsUiLoc::SettingsCard::DoNotDisturb;
  }
  if (KeyPrefix(key, L"Modules.Timer")) {
    return SettingsUiLoc::SettingsCard::FocusTimer;
  }
  if (KeyEquals(key, L"Modules.QuickLookup")) {
    return SettingsUiLoc::SettingsCard::QuickLookup;
  }
  if (KeyPrefix(key, L"Modules.FileTray")) {
    return SettingsUiLoc::SettingsCard::FileTray;
  }
  if (KeyPrefix(key, L"Modules.GameOverlay")) {
    return SettingsUiLoc::SettingsCard::GameOverlay;
  }
  switch (group) {
  case SettingGroup::Appearance:
    return SettingsUiLoc::SettingsCard::Appearance;
  case SettingGroup::Behavior:
    return SettingsUiLoc::SettingsCard::Behavior;
  case SettingGroup::Animations:
    return SettingsUiLoc::SettingsCard::Animations;
  case SettingGroup::Indicators:
    return SettingsUiLoc::SettingsCard::Indicators;
  case SettingGroup::Shortcuts:
    return SettingsUiLoc::SettingsCard::Shortcuts;
  case SettingGroup::General:
    return SettingsUiLoc::SettingsCard::General;
  case SettingGroup::Themes:
    return SettingsUiLoc::SettingsCard::ThemeLook;
  case SettingGroup::Modules:
    return SettingsUiLoc::SettingsCard::Media;
  case SettingGroup::Runtime:
    return SettingsUiLoc::SettingsCard::General;
  default: {
    const SettingGroup unhandled = group;
    (void)unhandled;
    return SettingsUiLoc::SettingsCard::General;
  }
  }
}

HWND HeaderFor(const DialogState& st, SettingsUiLoc::SettingsCard card) {
  for (const CardHead& head : st.headers) {
    if (head.card == card) {
      return head.hwnd;
    }
  }
  return nullptr;
}

struct RowPlacement {
  RECT label{};
  RECT control{};
  RECT swatch{};
  bool hasLabel = false;
  bool hasSwatch = false;
  int rowAdvance = 0;
};

int Dip(int dpi, int dip) {
  return MulDiv(dip, dpi > 0 ? dpi : 96, 96);
}

struct RowBand {
  int left = 0;
  int width = 0;
};

RowBand RowBandForView(int dpi, int viewW) {
  const int margin = Dip(dpi, 8);
  const int pad = Dip(dpi, 12);
  RowBand band;
  band.left = margin + pad;
  band.width = (std::max)(Dip(dpi, 80), viewW - margin - pad - band.left);
  return band;
}

void ClampBottom(RECT& rc, int limit) {
  if (rc.bottom > limit) {
    rc.bottom = (std::max)(rc.top, static_cast<LONG>(limit));
  }
}

RowPlacement MeasureRow(int dpi, SettingType type, bool colorSwatch, int left, int top, int width) {
  RowPlacement out;
  out.rowAdvance = (std::max)(1, Dip(dpi, kRowH));
  width = (std::max)(1, width);
  const int controlH = (std::max)(1, Dip(dpi, kControlH));
  const int bottomLimit = top + out.rowAdvance;
  if (type == SettingType::Bool) {
    out.control = {left, top, left + width, top + (std::min)(controlH, out.rowAdvance)};
    return out;
  }
  const int gap = Dip(dpi, 8);
  const int minControl = Dip(dpi, colorSwatch ? 40 : 48);
  int labelW = (std::clamp)(width * 42 / 100, Dip(dpi, 120), Dip(dpi, 260));
  int maxLabel = width - gap - minControl;
  if (maxLabel < Dip(dpi, 48)) {
    maxLabel = (std::max)(0, width / 2);
  }
  if (labelW > maxLabel) {
    labelW = maxLabel;
  }
  if (labelW < 0) {
    labelW = 0;
  }
  const int labelGap = labelW > 0 ? gap : 0;
  int controlW = width - labelW - labelGap;
  if (controlW < 1) {
    labelW = (std::max)(0, width / 2);
    controlW = (std::max)(1, width - labelW - (labelW > 0 ? gap : 0));
  }
  const int controlX = left + labelW + (labelW > 0 ? gap : 0);
  if (labelW > 0) {
    const int labelY = top + Dip(dpi, 3);
    out.hasLabel = true;
    out.label = {left, labelY, left + labelW, labelY + Dip(dpi, 18)};
    ClampBottom(out.label, bottomLimit);
  }
  if (colorSwatch) {
    const int swatch = (std::min)(controlH, (std::max)(1, controlW));
    const int swatchGap = (std::min)(Dip(dpi, 6), (std::max)(0, controlW - swatch));
    int editW = controlW - swatch - swatchGap;
    if (editW < 1) {
      editW = (std::max)(1, controlW / 2);
    }
    out.control = {controlX, top, controlX + editW, top + (std::min)(controlH, out.rowAdvance)};
    out.hasSwatch = true;
    const int swatchX = controlX + controlW - swatch;
    out.swatch = {swatchX, top, swatchX + swatch, top + (std::min)(controlH, out.rowAdvance)};
    if (out.control.right > out.swatch.left) {
      out.control.right = out.swatch.left;
    }
    if (out.control.right < out.control.left) {
      out.control.right = out.control.left;
    }
    ClampBottom(out.control, bottomLimit);
    ClampBottom(out.swatch, bottomLimit);
    return out;
  }
  // Closed combo, edit, and slider occupy one row. The dropdown is a popup
  // (CB_SETMINVISIBLE), not an 8-row-tall HWND that clips the rows below it.
  out.control = {controlX, top, controlX + (std::max)(1, controlW), top + (std::min)(controlH, out.rowAdvance)};
  ClampBottom(out.control, bottomLimit);
  return out;
}

void PlaceControl(HWND hwnd, const RECT& rc, UINT flags) {
  if (!hwnd) {
    return;
  }
  const int width = (std::max)(1, static_cast<int>(rc.right - rc.left));
  const int height = (std::max)(1, static_cast<int>(rc.bottom - rc.top));
  SetWindowPos(hwnd, nullptr, rc.left, rc.top, width, height, flags);
}

void PlaceRow(DialogState& st, RowRef& row, int left, int top, int width) {
    const SettingDef& def = kSettingSchema[row.schemaIndex];
  const UINT flags = kMoveNoPaint | SWP_SHOWWINDOW;
  const RowPlacement place = MeasureRow(st.dpi, def.type, row.swatch != nullptr, left, top, width);
  if (place.hasLabel) {
    PlaceControl(row.label, place.label, flags);
  }
  PlaceControl(row.control, place.control, flags);
  if (place.hasSwatch) {
    PlaceControl(row.swatch, place.swatch, flags);
  }
}

void LayoutContent(DialogState& st) {
  if (!st.content) {
    return;
  }
  RECT rc = {};
  GetClientRect(st.scroll ? st.scroll : st.content, &rc);
  int width = static_cast<int>(rc.right - rc.left);
  if (width < Px(st, 120)) {
    width = Px(st, 420);
  }
  std::vector<size_t> order;
  order.reserve(st.rows.size());
  for (size_t i = 0; i < st.rows.size(); ++i) {
    if (RowVisible(st, st.rows[i])) {
      order.push_back(i);
    }
  }
  std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
    const int rankA = CardRank(st.rows[a].card);
    const int rankB = CardRank(st.rows[b].card);
    if (rankA != rankB) {
      return rankA < rankB;
    }
    return a < b;
  });

  const UINT hideFlags = SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_HIDEWINDOW | SWP_NOREDRAW;
  for (RowRef& row : st.rows) {
    if (row.label) {
      SetWindowPos(row.label, nullptr, 0, 0, 0, 0, hideFlags);
    }
    if (row.control) {
      SetWindowPos(row.control, nullptr, 0, 0, 0, 0, hideFlags);
    }
    if (row.swatch) {
      SetWindowPos(row.swatch, nullptr, 0, 0, 0, 0, hideFlags);
    }
  }
  for (CardHead& head : st.headers) {
    if (head.hwnd) {
      SetWindowPos(head.hwnd, nullptr, 0, 0, 0, 0, hideFlags);
    }
  }

  const int margin = Px(st, 8);
  const int pad = Px(st, 12);
  int y = margin;
  st.cardRects.clear();
  const UINT showFlags = SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW | SWP_SHOWWINDOW;
  if (st.preview) {
    if (ShowPreview(st)) {
      SetWindowPos(st.preview, nullptr, margin, y, (std::max)(1, width - margin * 2), Px(st, kPreviewH), showFlags);
      y += Px(st, kPreviewH) + margin;
    } else {
      SetWindowPos(st.preview, nullptr, 0, 0, 0, 0, hideFlags);
    }
  }

  if (order.empty()) {
    if (st.emptyLabel) {
      SetWindowPos(st.emptyLabel, nullptr, margin, y, (std::max)(1, width - margin * 2), Px(st, 28), showFlags);
      y += Px(st, 40);
    }
  } else if (st.emptyLabel) {
    SetWindowPos(st.emptyLabel, nullptr, 0, 0, 0, 0, hideFlags);
  }

  bool open = false;
  int cardTop = 0;
  SettingsUiLoc::SettingsCard current = SettingsUiLoc::SettingsCard::General;
  auto closeCard = [&](int bottom) {
    if (!open) {
      return;
    }
    RECT card = {margin, cardTop, width - margin, bottom};
    if (card.right > card.left) {
      st.cardRects.push_back(card);
    }
  };

  for (size_t index : order) {
    RowRef& row = st.rows[index];
    if (!open || row.card != current) {
      if (open) {
        closeCard(y + Px(st, 6));
        y += Px(st, 6) + margin;
      }
      open = true;
      current = row.card;
      cardTop = y;
      if (HWND header = HeaderFor(st, row.card)) {
        SetWindowPos(header, nullptr, margin + pad, y + Px(st, 6),
                     (std::max)(1, width - (margin + pad) * 2), Px(st, kHeaderH), showFlags);
      }
      y += Px(st, 8) + Px(st, kHeaderH);
    }
    const RowBand band = RowBandForView(st.dpi, width);
    PlaceRow(st, row, band.left, y, band.width);
    y += Px(st, kRowH);
  }
  if (open) {
    closeCard(y + Px(st, 8));
    y += Px(st, 8);
  }
  st.contentHeight = y + margin;
  UpdateScrollBar(st);
  PlaceContent(st);
  InvalidateWindowTree(st.content);
  if (st.scroll) {
    InvalidateWindowTree(st.scroll);
  }
}

void LayoutShell(HWND hwnd, DialogState& st, bool placeOuter) {
  if (st.layingOut) {
    return;
  }
  st.layingOut = true;
  if (placeOuter) {
    RECT outer = {0, 0, Px(st, kDlgClientW), Px(st, kDlgClientH)};
    const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE));
    const DWORD exStyle = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_EXSTYLE));
    if (!AdjustWindowRectExForDpi(&outer, style, FALSE, exStyle, static_cast<UINT>(st.dpi))) {
      outer.right = Px(st, kDlgClientW) + Px(st, 16);
      outer.bottom = Px(st, kDlgClientH) + Px(st, 48);
    }
    const int width = outer.right - outer.left;
    const int height = outer.bottom - outer.top;
    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info = {sizeof(info)};
    int x = 0;
    int y = 0;
    if (GetMonitorInfoW(monitor, &info)) {
      x = info.rcWork.left + ((info.rcWork.right - info.rcWork.left) - width) / 2;
      y = info.rcWork.top + ((info.rcWork.bottom - info.rcWork.top) - height) / 2;
    }
    SetWindowPos(hwnd, nullptr, x, y, width, height, SWP_NOZORDER);
  }

  RECT client = {};
  GetClientRect(hwnd, &client);
  const int clientW = client.right - client.left;
  const int clientH = client.bottom - client.top;
  const int pad = Px(st, 12);
  const int bannerH = st.bannerText.empty() ? 0 : Px(st, 28);
  const int footerTop = clientH - Px(st, 40);
  if (st.search) {
    SetWindowPos(st.search, nullptr, pad, pad, (std::max)(1, clientW - pad * 2), Px(st, kSearchH), kMoveNoPaint);
  }
  const int navTop = pad + Px(st, kSearchH) + Px(st, 8);
  const int navW = Px(st, kSidebarW);
  const int navBtnH = Px(st, 34);
  const int navGap = Px(st, 2);
  for (size_t i = 0; i < st.nav.size(); ++i) {
    SetWindowPos(st.nav[i].button, nullptr, Px(st, 8), navTop + static_cast<int>(i) * (navBtnH + navGap),
                 (std::max)(1, navW - Px(st, 16)), navBtnH, kMoveNoPaint);
  }
  const int contentX = navW + Px(st, 4);
  const int contentW = (std::max)(1, clientW - contentX - pad);
  const int contentBottom = footerTop - bannerH - Px(st, 8);
  const int contentH = (std::max)(Px(st, 80), contentBottom - navTop);
  if (st.scroll) {
    SetWindowPos(st.scroll, nullptr, contentX, navTop, contentW, contentH, kMoveNoPaint);
  }
  if (st.banner) {
    if (bannerH > 0) {
      SetWindowPos(st.banner, nullptr, contentX, navTop + contentH + Px(st, 4), contentW, bannerH,
                   kMoveNoPaint | SWP_SHOWWINDOW);
    } else {
      SetWindowPos(st.banner, nullptr, 0, 0, 0, 0, kMoveNoPaint | SWP_HIDEWINDOW | SWP_NOMOVE | SWP_NOSIZE);
    }
  }
  const int btnY = footerTop;
  const int btnH = Px(st, 28);
  const int gap = Px(st, 8);
  int x = clientW - pad;
  if (st.okBtn) {
    x -= Px(st, 80);
    SetWindowPos(st.okBtn, nullptr, x, btnY, Px(st, 80), btnH, kMoveNoPaint);
  }
  if (st.applyBtn) {
    x -= gap + Px(st, 128);
    SetWindowPos(st.applyBtn, nullptr, x, btnY, Px(st, 128), btnH, kMoveNoPaint);
  }
  if (st.cancelBtn) {
    x -= gap + Px(st, 118);
    SetWindowPos(st.cancelBtn, nullptr, x, btnY, Px(st, 118), btnH, kMoveNoPaint);
  }
  if (st.resetBtn) {
    SetWindowPos(st.resetBtn, nullptr, pad, btnY, Px(st, 188), btnH, kMoveNoPaint);
  }
  LayoutContent(st);
  InvalidateWindowTree(hwnd);
  st.layingOut = false;
}

void UpdateEnabled(DialogState& st) {
  DialogLookup lookup(&st);
  for (RowRef& row : st.rows) {
    const bool enabled = SettingsControlIsEnabled(kSettingSchema[row.schemaIndex].key, lookup);
    auto apply = [&](HWND hwnd) {
      if (!hwnd) {
    return;
  }
      const bool current = IsWindowEnabled(hwnd) != FALSE;
      if (current != enabled) {
        EnableWindow(hwnd, enabled ? TRUE : FALSE);
      }
    };
    apply(row.control);
    apply(row.swatch);
    // Leave the STATIC label enabled. With visual styles stripped, a disabled
    // static ignores WM_CTLCOLORSTATIC and draws embossed gray text.
    if (row.label) {
      const bool wasMuted = GetPropW(row.label, kMutedProp) != nullptr;
      const bool shouldMute = !enabled;
      if (wasMuted != shouldMute) {
        if (shouldMute) {
          SetPropW(row.label, kMutedProp, reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(1)));
        } else {
          RemovePropW(row.label, kMutedProp);
        }
        InvalidateRect(row.label, nullptr, FALSE);
      }
      if (IsWindowEnabled(row.label) == FALSE) {
        EnableWindow(row.label, TRUE);
      }
    }
  }
}

void RefreshTheme(HWND hwnd, DialogState& st) {
  SettingsThemeSource source = ThemeSourceFrom(st);
  HIGHCONTRASTW contrast = {sizeof(contrast)};
  if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) &&
      (contrast.dwFlags & HCF_HIGHCONTRASTON)) {
    source.highContrast = true;
  }
  st.palette = BuildSettingsDialogPalette(source);
  RebuildBrushes(st);
  ApplySettingsWindowChrome(hwnd, st.palette);
  if (st.scroll) {
    AllowSettingsControlDarkMode(st.scroll, st.palette.dark);
  }
  ThemeAllControls(st);
  if (st.ready) {
    InvalidateChrome(hwnd, st);
  }
}

void OnSettingChanged(HWND hwnd, DialogState& st, const wchar_t* key) {
  if (!st.ready || st.updating) {
    return;
  }
  ClearBanner(hwnd, st);
  UpdateEnabled(st);
  if (AffectsPalette(key)) {
    RefreshTheme(hwnd, st);
  } else if (st.preview) {
    InvalidateRect(st.preview, nullptr, TRUE);
  }
  for (NavItem& item : st.nav) {
    InvalidateRect(item.button, nullptr, FALSE);
  }
}

const wchar_t* KeyForControl(const DialogState& st, int id) {
  if (id >= kSwatchIdBase && id < kSwatchIdBase + static_cast<int>(kSettingSchemaCount)) {
    return kSettingSchema[id - kSwatchIdBase].key;
  }
  if (id >= kControlIdBase && id < kControlIdBase + static_cast<int>(kSettingSchemaCount)) {
    return kSettingSchema[id - kControlIdBase].key;
  }
  (void)st;
  return nullptr;
}

RowRef* RowForControlId(DialogState& st, int id) {
  int index = -1;
  if (id >= kSwatchIdBase && id < kSwatchIdBase + static_cast<int>(kSettingSchemaCount)) {
    index = id - kSwatchIdBase;
  } else if (id >= kControlIdBase && id < kControlIdBase + static_cast<int>(kSettingSchemaCount)) {
    index = id - kControlIdBase;
  }
  if (index < 0) {
    return nullptr;
  }
  for (RowRef& row : st.rows) {
    if (static_cast<int>(row.schemaIndex) == index) {
      return &row;
    }
  }
  return nullptr;
}

void PickColor(HWND hwnd, DialogState& st, RowRef& row) {
  COLORREF parsed = st.palette.accent;
  const std::wstring text = GetControlText(row.control);
  COLORREF fromEdit = 0;
  if (SettingsThemeTryParseHex(text.c_str(), &fromEdit)) {
    parsed = fromEdit;
  }
  CHOOSECOLORW choose = {};
  choose.lStructSize = sizeof(choose);
  choose.hwndOwner = hwnd;
  choose.rgbResult = parsed;
  choose.lpCustColors = st.customColors;
  choose.Flags = CC_FULLOPEN | CC_RGBINIT | CC_ANYCOLOR;
  if (!ChooseColorW(&choose)) {
    return;
  }
  wchar_t hex[16] = {};
  swprintf_s(hex, L"#%02X%02X%02X", GetRValue(choose.rgbResult), GetGValue(choose.rgbResult),
             GetBValue(choose.rgbResult));
  SetWindowTextW(row.control, hex);
  if (row.swatch) {
    InvalidateRect(row.swatch, nullptr, FALSE);
  }
}

bool IsValidHotkeyKey(const wchar_t* keyName, const std::wstring& value) {
  std::wstring trimmed = value;
  const size_t first = trimmed.find_first_not_of(L" \t\r\n\"'");
  if (first == std::wstring::npos) {
    return false;
  }
  trimmed = trimmed.substr(first, trimmed.find_last_not_of(L" \t\r\n\"'") - first + 1);
  if (wcscmp(keyName, L"Shortcuts.LookupKey") == 0 && _wcsicmp(trimmed.c_str(), L"Space") == 0) {
    return true;
  }
  if (trimmed.size() != 1) {
    return false;
  }
  const wchar_t ch = towupper(trimmed[0]);
  return (ch >= L'A' && ch <= L'Z') || (ch >= L'0' && ch <= L'9');
}

std::wstring NormalizeHotkeyKey(const wchar_t* keyName, const std::wstring& value,
                                const std::wstring& previous) {
  if (IsValidHotkeyKey(keyName, value)) {
    std::wstring trimmed = value;
    const size_t first = trimmed.find_first_not_of(L" \t\r\n\"'");
    trimmed = trimmed.substr(first, trimmed.find_last_not_of(L" \t\r\n\"'") - first + 1);
    if (_wcsicmp(trimmed.c_str(), L"Space") == 0) {
      return L"Space";
    }
    trimmed[0] = towupper(trimmed[0]);
    return trimmed;
  }
  if (IsValidHotkeyKey(keyName, previous)) {
    return NormalizeHotkeyKey(keyName, previous, previous);
  }
  return (keyName && wcscmp(keyName, L"Shortcuts.LookupKey") == 0) ? L"Space" : L"D";
}

void FillControlsFromSchemaDefaults(DialogState& st) {
  UpdateGuard guard(st.updating);
  for (RowRef& row : st.rows) {
    const SettingDef& def = kSettingSchema[row.schemaIndex];
    const wchar_t* defValue = def.defaultString ? def.defaultString : L"";
    if (wcscmp(def.key, L"General.LaunchAtStartup") == 0) {
      WriteRowValue(row, defValue);
      continue;
    }
    if (wcscmp(def.key, L"app.version") == 0) {
      WriteRowValue(row, LUX_APP_VERSION_STRING);
      continue;
    }
    WriteRowValue(row, defValue);
  }
}

bool HotkeysCollide(const DialogState& st) {
  const std::wstring lookupKey =
      NormalizeHotkeyKey(L"Shortcuts.LookupKey", ValueForKey(st, L"Shortcuts.LookupKey"), L"Space");
  const std::wstring hideKey =
      NormalizeHotkeyKey(L"Shortcuts.HideShowKey", ValueForKey(st, L"Shortcuts.HideShowKey"), L"D");
  const bool lookupOn = ValueForKey(st, L"Modules.QuickLookup") == L"1";
  const bool hideOn = ValueForKey(st, L"Shortcuts.HideShowHotkeyEnabled") == L"1";
  return lookupOn && hideOn && !lookupKey.empty() && _wcsicmp(lookupKey.c_str(), hideKey.c_str()) == 0 &&
         _wcsicmp(ValueForKey(st, L"Shortcuts.LookupModifiers").c_str(),
                  ValueForKey(st, L"Shortcuts.HideShowModifiers").c_str()) == 0;
}

bool ResetToFactoryDefaults(HWND hwnd, DialogState& st) {
  const int answer = MessageBoxW(hwnd, SettingsUiLoc::ResetDefaultsConfirm(st.lang),
                                 SettingsUiLoc::ResetDefaultsTitle(st.lang),
                                 MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
  if (answer != IDYES) {
    return false;
  }
  if (!SettingsStoreResetToDefaults()) {
    LUX_LOGE(L"Settings dialog: SettingsStoreResetToDefaults failed");
    return false;
  }
  AutostartSetEnabled(false);
  FillControlsFromSchemaDefaults(st);
  CaptureBaseline(st);
  ClearBanner(hwnd, st);
  UpdateEnabled(st);
  RefreshTheme(hwnd, st);
  LayoutContent(st);
  LuxOverlay_OnSettingsChanged();
  return true;
}

bool SaveFromControls(HWND hwnd, DialogState& st) {
  if (HotkeysCollide(st)) {
    SetBanner(hwnd, st, SettingsUiLoc::HotkeyCollision(st.lang));
    MessageBoxW(hwnd, SettingsUiLoc::HotkeyCollision(st.lang), SettingsUiLoc::WindowTitle(st.lang),
                MB_OK | MB_ICONWARNING);
    return false;
  }
  for (const RowRef& row : st.rows) {
    const SettingDef& def = kSettingSchema[row.schemaIndex];
    if (def.type == SettingType::Bool) {
      const bool checked = SendMessageW(row.control, BM_GETCHECK, 0, 0) == BST_CHECKED;
      SettingsSetBool(def.key, checked);
      if (wcscmp(def.key, L"General.LaunchAtStartup") == 0) {
        if (!AutostartSetEnabled(checked)) {
          SetBanner(hwnd, st, SettingsUiLoc::AutostartWriteFailed(st.lang));
          MessageBoxW(hwnd, SettingsUiLoc::AutostartWriteFailed(st.lang), SettingsUiLoc::WindowTitle(st.lang),
                      MB_OK | MB_ICONWARNING);
          const bool live = AutostartIsEnabled();
          SettingsSetBool(def.key, live);
          SendMessageW(row.control, BM_SETCHECK, live ? BST_CHECKED : BST_UNCHECKED, 0);
        }
      }
    } else if (def.type == SettingType::Enum && def.enums && def.enumCount > 0) {
      SettingsSetString(def.key, ComboEnumValue(row).c_str());
    } else if (def.type == SettingType::Int) {
      const std::wstring normalized = ReadRowValue(row);
      SettingsSetString(def.key, normalized.c_str());
    } else {
      std::wstring text = GetControlText(row.control);
      if (wcscmp(def.key, L"Shortcuts.LookupKey") == 0 || wcscmp(def.key, L"Shortcuts.HideShowKey") == 0) {
        const std::wstring previous = SettingsGetString(def.key, def.defaultString);
        text = NormalizeHotkeyKey(def.key, text, previous);
        WriteRowValue(row, text);
      }
      SettingsSetString(def.key, text.c_str());
    }
  }
  if (!SettingsStoreSaveUser()) {
    LUX_LOGE(L"Settings dialog: SettingsStoreSaveUser failed");
    return false;
  }
  LuxOverlay_OnSettingsChanged();
  return true;
}

void SubclassButton(HWND hwnd) {
  if (hwnd) {
    SetWindowSubclass(hwnd, ButtonThemeProc, kSubButton, 0);
  }
}

void SubclassEdit(HWND hwnd) {
  if (hwnd) {
    SetWindowSubclass(hwnd, EditThemeProc, kSubEdit, 0);
  }
}

void SubclassCombo(HWND hwnd) {
  if (hwnd) {
    SetWindowSubclass(hwnd, ComboThemeProc, kSubCombo, 0);
  }
}

constexpr SettingsUiLoc::SettingsCard kAllCards[] = {
    SettingsUiLoc::SettingsCard::ThemeLook,     SettingsUiLoc::SettingsCard::ThemeAccent,
    SettingsUiLoc::SettingsCard::ThemeSurface,  SettingsUiLoc::SettingsCard::ThemeBackdrop,
    SettingsUiLoc::SettingsCard::ThemeAura,     SettingsUiLoc::SettingsCard::Media,
    SettingsUiLoc::SettingsCard::MediaClock,    SettingsUiLoc::SettingsCard::Volume,
    SettingsUiLoc::SettingsCard::Clipboard,     SettingsUiLoc::SettingsCard::Battery,
    SettingsUiLoc::SettingsCard::Progress,      SettingsUiLoc::SettingsCard::CapsLock,
    SettingsUiLoc::SettingsCard::Weather,       SettingsUiLoc::SettingsCard::Calendar,
    SettingsUiLoc::SettingsCard::Monitor,       SettingsUiLoc::SettingsCard::Bluetooth,
    SettingsUiLoc::SettingsCard::DoNotDisturb,  SettingsUiLoc::SettingsCard::FocusTimer,
    SettingsUiLoc::SettingsCard::Lyrics,        SettingsUiLoc::SettingsCard::QuickLookup,
    SettingsUiLoc::SettingsCard::FileTray,      SettingsUiLoc::SettingsCard::GameOverlay,
    SettingsUiLoc::SettingsCard::Appearance,    SettingsUiLoc::SettingsCard::Behavior,
    SettingsUiLoc::SettingsCard::Animations,    SettingsUiLoc::SettingsCard::Indicators,
    SettingsUiLoc::SettingsCard::Shortcuts,     SettingsUiLoc::SettingsCard::General,
};

void BuildControls(HWND hwnd, DialogState& st) {
  st.lang = SettingsUiLoc::ResolveLang();
  EnsureWindowClasses();
  const COLORREF seeds[] = {RGB(0x4C, 0xC9, 0xF0), RGB(0x08, 0x08, 0x0A), RGB(0x1C, 0x1C, 0x1E),
                            RGB(0x11, 0x17, 0x21), RGB(0x2E, 0x34, 0x40), RGB(0x0C, 0x15, 0x12),
                            RGB(0x1A, 0x15, 0x12), RGB(0x16, 0x11, 0x1C), RGB(0xF5, 0xF6, 0xF8),
                            RGB(0xFF, 0xFF, 0xFF), RGB(0x9B, 0x9B, 0xA5), RGB(0xE8, 0xA8, 0x38),
                            RGB(0x51, 0xCF, 0x66), RGB(0xFF, 0x6B, 0x6B), RGB(0x84, 0x5E, 0xF7),
                            RGB(0x33, 0x33, 0x38)};
  for (int i = 0; i < 16; ++i) {
    st.customColors[i] = seeds[i];
  }

  const HINSTANCE instance = GetModuleHandleW(nullptr);
  st.search = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                              WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS | ES_AUTOHSCROLL, 0, 0, 0,
                              0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdSearch)), instance,
                              nullptr);
  SendMessageW(st.search, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(SettingsUiLoc::FilterCue(st.lang)));
  SubclassEdit(st.search);

  for (int i = 0; i < static_cast<int>(ARRAYSIZE(kNavOrder)); ++i) {
    NavItem item;
    item.group = kNavOrder[i];
    item.button = CreateWindowExW(
        0, L"BUTTON", SettingsUiLoc::GroupTitle(st.lang, item.group),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0, hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kNavIdBase + i)), instance, nullptr);
    SetPropW(item.button, kNavProp, reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(1)));
    SubclassButton(item.button);
    st.nav.push_back(item);
  }

  st.scroll = CreateWindowExW(WS_EX_CONTROLPARENT, kScrollClass, L"",
                              WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | WS_TABSTOP,
                              0, 0, 0, 0, hwnd, nullptr, instance, nullptr);
  st.content = CreateWindowExW(WS_EX_CONTROLPARENT, kContentClass, L"",
                               WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 0, 0, 0, st.scroll,
                               nullptr, instance, nullptr);
  st.preview = CreateWindowExW(0, kPreviewClass, L"", WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 0, 0, st.content, nullptr,
                               instance, nullptr);
  st.emptyLabel = CreateWindowExW(0, L"STATIC", SettingsUiLoc::NoMatches(st.lang),
                                  WS_CHILD | SS_LEFT | SS_NOPREFIX | SS_CENTERIMAGE, 0, 0, 0, 0, st.content,
                                  nullptr, instance, nullptr);
  if (st.emptyLabel) {
    SetWindowSubclass(st.emptyLabel, LabelThemeProc, kSubLabel, 0);
  }

  for (SettingsUiLoc::SettingsCard card : kAllCards) {
    CardHead head;
    head.card = card;
    head.hwnd = CreateWindowExW(0, L"STATIC", SettingsUiLoc::CardTitle(st.lang, card),
                                WS_CHILD | SS_LEFT | SS_NOPREFIX, 0, 0, 0, 0, st.content, nullptr, instance,
                                nullptr);
    SetWindowSubclass(head.hwnd, HeaderThemeProc, kSubHeader, 0);
    st.headers.push_back(head);
  }

  for (size_t i = 0; i < kSettingSchemaCount; ++i) {
    const SettingDef& def = kSettingSchema[i];
    if (def.group == SettingGroup::Runtime) {
      continue;
    }
    RowRef row;
    row.schemaIndex = i;
    row.card = CardForKey(def.key, def.group);
    row.labelText = SettingsUiLoc::SettingLabel(st.lang, def.key);
    const int id = kControlIdBase + static_cast<int>(i);
    if (def.type == SettingType::Bool) {
      bool on = SettingsGetBool(def.key, def.defaultString && def.defaultString[0] == L'1');
      if (wcscmp(def.key, L"General.LaunchAtStartup") == 0) {
        on = AutostartIsEnabled();
      }
      row.control = CreateWindowExW(0, L"BUTTON", row.labelText.c_str(),
                                    WS_CHILD | WS_TABSTOP | WS_CLIPSIBLINGS | BS_AUTOCHECKBOX, 0, 0, 0, 0,
                                    st.content, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance,
                                    nullptr);
      SendMessageW(row.control, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
      SubclassButton(row.control);
    } else if (def.type == SettingType::Enum && def.enums && def.enumCount > 0) {
      row.label = CreateWindowExW(0, L"STATIC", row.labelText.c_str(),
                                  WS_CHILD | SS_LEFT | SS_NOPREFIX | SS_ENDELLIPSIS, 0, 0, 0, 0, st.content,
                                  nullptr, instance, nullptr);
      if (row.label) {
        SetWindowSubclass(row.label, LabelThemeProc, kSubLabel, 0);
      }
      row.control = CreateWindowExW(0, L"COMBOBOX", L"",
                                    WS_CHILD | WS_TABSTOP | WS_CLIPSIBLINGS | CBS_DROPDOWNLIST | WS_VSCROLL, 0, 0,
                                    0, 0, st.content, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance,
                                    nullptr);
      for (size_t e = 0; e < def.enumCount; ++e) {
        SendMessageW(row.control, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(SettingsUiLoc::EnumLabel(st.lang, def, e)));
      }
      SelectEnumValue(row.control, def, SettingsGetString(def.key, def.defaultString));
      SendMessageW(row.control, CB_SETMINVISIBLE, 8, 0);
      SubclassCombo(row.control);
    } else if (UseSlider(def)) {
      row.label = CreateWindowExW(0, L"STATIC", row.labelText.c_str(),
                                  WS_CHILD | SS_LEFT | SS_NOPREFIX | SS_ENDELLIPSIS, 0, 0, 0, 0, st.content,
                                  nullptr, instance, nullptr);
      if (row.label) {
        SetWindowSubclass(row.label, LabelThemeProc, kSubLabel, 0);
      }
      row.control = CreateWindowExW(0, kSliderClass, row.labelText.c_str(), WS_CHILD | WS_TABSTOP | WS_CLIPSIBLINGS,
                                    0, 0, 0, 0, st.content, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                    instance, nullptr);
      SendMessageW(row.control, kSliderSetRange, static_cast<WPARAM>(def.minValue),
                   static_cast<LPARAM>(def.maxValue));
      int value = _wtoi(SettingsGetString(def.key, def.defaultString).c_str());
      value = (std::clamp)(value, def.minValue, def.maxValue);
      SendMessageW(row.control, kSliderSetPos, static_cast<WPARAM>(value), 0);
    } else {
      row.label = CreateWindowExW(0, L"STATIC", row.labelText.c_str(),
                                  WS_CHILD | SS_LEFT | SS_NOPREFIX | SS_ENDELLIPSIS, 0, 0, 0, 0, st.content,
                                  nullptr, instance, nullptr);
      if (row.label) {
        SetWindowSubclass(row.label, LabelThemeProc, kSubLabel, 0);
      }
      const std::wstring value = SettingsGetString(def.key, def.defaultString);
      const DWORD editStyle = WS_CHILD | WS_TABSTOP | WS_CLIPSIBLINGS | ES_AUTOHSCROLL |
                              (wcscmp(def.key, L"app.version") == 0 ? ES_READONLY : 0);
      row.control = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", value.c_str(), editStyle, 0, 0, 0, 0, st.content,
                                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
      SubclassEdit(row.control);
      if (IsColorKey(def.key)) {
        row.swatch = CreateWindowExW(
            0, L"BUTTON", SettingsUiLoc::ColorPickerLabel(st.lang), WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0,
            st.content, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSwatchIdBase + static_cast<int>(i))),
            instance, nullptr);
        SetPropW(row.swatch, kSwatchProp, row.control);
        SubclassButton(row.swatch);
      }
    }
    st.rows.push_back(std::move(row));
  }

  st.banner = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | SS_LEFT | SS_NOPREFIX | SS_CENTERIMAGE | SS_ENDELLIPSIS,
                              0, 0, 0, 0, hwnd, nullptr, instance, nullptr);
  if (st.banner) {
    SetWindowSubclass(st.banner, LabelThemeProc, kSubLabel, 0);
  }
  st.resetBtn = CreateWindowExW(0, L"BUTTON", SettingsUiLoc::ResetDefaults(st.lang),
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0, hwnd,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdResetDefaults)), instance, nullptr);
  st.cancelBtn = CreateWindowExW(0, L"BUTTON", SettingsUiLoc::Cancel(st.lang),
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0, hwnd,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdCancelBtn)), instance, nullptr);
  st.applyBtn = CreateWindowExW(0, L"BUTTON", SettingsUiLoc::Apply(st.lang),
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0, hwnd,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdApply)), instance, nullptr);
  st.okBtn = CreateWindowExW(0, L"BUTTON", SettingsUiLoc::Ok(st.lang),
                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 0, 0, 0, 0, hwnd,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdOk)), instance, nullptr);
  SubclassButton(st.resetBtn);
  SubclassButton(st.cancelBtn);
  SubclassButton(st.applyBtn);
  SubclassButton(st.okBtn);

  RebuildFonts(st);
  UpdateEnabled(st);
  RefreshTheme(hwnd, st);
  LayoutShell(hwnd, st, true);
  CaptureBaseline(st);
  st.ready = true;
}

void RemoveSubclasses(DialogState& st) {
  auto drop = [](HWND hwnd, SUBCLASSPROC proc, UINT_PTR id) {
    if (hwnd) {
      RemoveWindowSubclass(hwnd, proc, id);
    }
  };
  drop(st.search, EditThemeProc, kSubEdit);
  drop(st.emptyLabel, LabelThemeProc, kSubLabel);
  drop(st.banner, LabelThemeProc, kSubLabel);
  drop(st.okBtn, ButtonThemeProc, kSubButton);
  drop(st.cancelBtn, ButtonThemeProc, kSubButton);
  drop(st.applyBtn, ButtonThemeProc, kSubButton);
  drop(st.resetBtn, ButtonThemeProc, kSubButton);
  for (NavItem& item : st.nav) {
    drop(item.button, ButtonThemeProc, kSubButton);
  }
  for (CardHead& head : st.headers) {
    drop(head.hwnd, HeaderThemeProc, kSubHeader);
  }
  for (RowRef& row : st.rows) {
    drop(row.label, LabelThemeProc, kSubLabel);
    if (row.label) {
      RemovePropW(row.label, kMutedProp);
    }
    if (IsComboBox(row.control)) {
      COMBOBOXINFO info = {};
      info.cbSize = sizeof(info);
      if (GetComboBoxInfo(row.control, &info) && info.hwndList) {
        drop(info.hwndList, ComboListProc, kSubComboList);
        RemovePropW(info.hwndList, kListHookProp);
        RemovePropW(info.hwndList, kDropMinProp);
      }
    }
    drop(row.control, ButtonThemeProc, kSubButton);
    drop(row.control, EditThemeProc, kSubEdit);
    drop(row.control, ComboThemeProc, kSubCombo);
    drop(row.swatch, ButtonThemeProc, kSubButton);
  }
}

LRESULT CALLBACK ContentProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  HWND scroll = GetParent(hwnd);
  HWND parent = scroll ? GetParent(scroll) : nullptr;
  DialogState* st = parent ? State(parent) : nullptr;
  switch (msg) {
  case WM_CTLCOLORSTATIC:
  case WM_CTLCOLOREDIT:
  case WM_CTLCOLORBTN:
  case WM_CTLCOLORLISTBOX:
    if (st) {
      return reinterpret_cast<LRESULT>(HandleCtlColor(*st, reinterpret_cast<HDC>(wParam), reinterpret_cast<HWND>(lParam)));
    }
    break;
  case WM_ERASEBKGND:
    return PaintContentBackground(hwnd, reinterpret_cast<HDC>(wParam), st);
  case WM_MOUSEWHEEL:
    if (scroll) {
      return SendMessageW(scroll, msg, wParam, lParam);
    }
    break;
  case WM_COMMAND: {
    const WORD code = HIWORD(wParam);
    const int id = LOWORD(wParam);
    if (st && code == CBN_DROPDOWN) {
      if (RowRef* row = RowForControlId(*st, id)) {
        EnsureComboListTheme(row->control, *st);
      }
    }
    if (st && code == CBN_CLOSEUP) {
      st->suppressCloseUntil = GetTickCount64() + 50;
      if (RowRef* row = RowForControlId(*st, id)) {
        // Opening the list can stretch the combo HWND over the rows under it.
        // Put the closed control back to one row and repaint what it covered.
        RestoreClosedComboHeight(row->control, *st);
        InvalidateWindowTree(hwnd);
      }
    }
    if (st && parent &&
        (code == CBN_SELCHANGE || code == BN_CLICKED || code == EN_CHANGE || code == kSliderChanged)) {
      if (code == BN_CLICKED && id >= kSwatchIdBase) {
        if (RowRef* row = RowForControlId(*st, id)) {
          PickColor(parent, *st, *row);
        }
      }
      OnSettingChanged(parent, *st, KeyForControl(*st, id));
      if (code == CBN_SELCHANGE || code == EN_CHANGE) {
        if (RowRef* row = RowForControlId(*st, id)) {
          if (row->swatch) {
            InvalidateRect(row->swatch, nullptr, FALSE);
          }
          if (row->control) {
            InvalidateRect(row->control, nullptr, FALSE);
          }
        }
      }
    }
    break;
  }
  default:
    break;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK ScrollProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  HWND parent = GetParent(hwnd);
  DialogState* st = parent ? State(parent) : nullptr;
  switch (msg) {
  case WM_VSCROLL: {
    if (!st) {
      break;
    }
    SCROLLINFO si = {};
    si.cbSize = sizeof(si);
    si.fMask = SIF_ALL;
    GetScrollInfo(hwnd, SB_VERT, &si);
    int pos = si.nPos;
    switch (LOWORD(wParam)) {
    case SB_LINEUP:
      pos -= Px(*st, kRowH);
      break;
    case SB_LINEDOWN:
      pos += Px(*st, kRowH);
      break;
    case SB_PAGEUP:
      pos -= static_cast<int>(si.nPage);
      break;
    case SB_PAGEDOWN:
      pos += static_cast<int>(si.nPage);
      break;
    case SB_THUMBTRACK:
    case SB_THUMBPOSITION:
      pos = si.nTrackPos;
      break;
    default:
      break;
    }
    ScrollTo(*st, pos);
    return 0;
  }
  case WM_MOUSEWHEEL: {
    if (!st) {
      break;
    }
    st->wheelRemainder += GET_WHEEL_DELTA_WPARAM(wParam);
    const int steps = st->wheelRemainder / WHEEL_DELTA;
    st->wheelRemainder -= steps * WHEEL_DELTA;
    if (steps != 0) {
      ScrollTo(*st, st->scrollY - steps * Px(*st, 48));
    }
    return 0;
  }
  case WM_SIZE:
    if (st && !st->layingOut) {
      UpdateScrollBar(*st);
      PlaceContent(*st);
    }
    return 0;
  case WM_ERASEBKGND:
    if (st && st->windowBrush) {
      RECT rc = {};
      GetClientRect(hwnd, &rc);
      FillRect(reinterpret_cast<HDC>(wParam), &rc, st->windowBrush);
      return 1;
    }
    break;
  case WM_CTLCOLORSTATIC:
  case WM_CTLCOLOREDIT:
  case WM_CTLCOLORBTN:
  case WM_CTLCOLORLISTBOX:
    if (st) {
      return reinterpret_cast<LRESULT>(HandleCtlColor(*st, reinterpret_cast<HDC>(wParam), reinterpret_cast<HWND>(lParam)));
    }
    break;
  default:
    break;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void SelectCategory(HWND hwnd, DialogState& st, SettingGroup group) {
  st.category = group;
  st.filter[0] = L'\0';
  KillTimer(hwnd, kFilterTimer);
  if (st.search) {
    UpdateGuard guard(st.updating);
    SetWindowTextW(st.search, L"");
  }
  st.scrollY = 0;
  LayoutContent(st);
  for (NavItem& item : st.nav) {
    InvalidateRect(item.button, nullptr, FALSE);
  }
}

INT_PTR CALLBACK SettingsProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
  case WM_INITDIALOG: {
    GetApp().settingsHwnd = hwnd;
    auto* st = new DialogState();
    st->dpi = static_cast<int>(GetDpiForWindow(hwnd));
    if (st->dpi < 96) {
      st->dpi = 96;
    }
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));
    st->lang = SettingsUiLoc::ResolveLang();
    SetWindowTextW(hwnd, SettingsUiLoc::WindowTitle(st->lang));
    if (HICON icon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APP))) {
      SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon));
      SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon));
    }
    BuildControls(hwnd, *st);
    return TRUE;
  }
  case WM_CTLCOLORDLG:
  case WM_CTLCOLORSTATIC:
  case WM_CTLCOLOREDIT:
  case WM_CTLCOLORBTN:
  case WM_CTLCOLORLISTBOX:
    if (DialogState* st = State(hwnd)) {
      return reinterpret_cast<INT_PTR>(
          HandleCtlColor(*st, reinterpret_cast<HDC>(wParam), reinterpret_cast<HWND>(lParam)));
    }
    break;
  case WM_ERASEBKGND:
    if (DialogState* st = State(hwnd)) {
      RECT rc = {};
      GetClientRect(hwnd, &rc);
      HDC hdc = reinterpret_cast<HDC>(wParam);
      if (st->windowBrush) {
        FillRect(hdc, &rc, st->windowBrush);
      }
      RECT nav = {0, 0, Px(*st, kSidebarW), rc.bottom};
      if (st->navBrush) {
        FillRect(hdc, &nav, st->navBrush);
      }
      HPEN pen = CreatePen(PS_SOLID, 1, st->palette.divider);
      if (pen) {
        HGDIOBJ old = SelectObject(hdc, pen);
        const int split = Px(*st, kSidebarW);
        MoveToEx(hdc, split, 0, nullptr);
        LineTo(hdc, split, rc.bottom);
        const int footer = rc.bottom - Px(*st, kFooterH);
        MoveToEx(hdc, 0, footer, nullptr);
        LineTo(hdc, rc.right, footer);
        SelectObject(hdc, old);
        DeleteObject(pen);
      }
      return TRUE;
    }
    break;
  case WM_GETMINMAXINFO:
      if (DialogState* st = State(hwnd)) {
      RECT minRc = {0, 0, Px(*st, kMinClientW), Px(*st, kMinClientH)};
      const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE));
      const DWORD exStyle = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_EXSTYLE));
      if (!AdjustWindowRectExForDpi(&minRc, style, FALSE, exStyle, static_cast<UINT>(st->dpi))) {
        minRc.right = Px(*st, kMinClientW) + 16;
        minRc.bottom = Px(*st, kMinClientH) + 48;
      }
      MINMAXINFO* info = reinterpret_cast<MINMAXINFO*>(lParam);
      info->ptMinTrackSize.x = minRc.right - minRc.left;
      info->ptMinTrackSize.y = minRc.bottom - minRc.top;
      return TRUE;
    }
    break;
  case WM_SETTINGCHANGE:
  case WM_THEMECHANGED:
    if (DialogState* st = State(hwnd)) {
      const bool themeChange =
          msg == WM_THEMECHANGED || !lParam ||
          (lParam && _wcsicmp(reinterpret_cast<LPCWSTR>(lParam), L"ImmersiveColorSet") == 0) ||
          (lParam && _wcsicmp(reinterpret_cast<LPCWSTR>(lParam), L"WindowsThemeElement") == 0);
      if (themeChange) {
        RefreshTheme(hwnd, *st);
        return TRUE;
      }
    }
    break;
  case WM_DPICHANGED: {
    DialogState* st = State(hwnd);
    if (!st) {
      break;
    }
    st->dpi = HIWORD(wParam);
    if (st->dpi < 96) {
      st->dpi = 96;
    }
    RebuildFonts(*st);
    const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
    if (suggested) {
      SetWindowPos(hwnd, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                   suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    LayoutShell(hwnd, *st, false);
    return TRUE;
  }
  case WM_SIZE:
    if (DialogState* st = State(hwnd)) {
      LayoutShell(hwnd, *st, false);
    }
    return TRUE;
  case WM_MOUSEWHEEL:
    if (DialogState* st = State(hwnd); st && st->scroll) {
      SendMessageW(st->scroll, WM_MOUSEWHEEL, wParam, lParam);
      return TRUE;
    }
    break;
  case WM_TIMER:
    if (wParam == kFilterTimer) {
      KillTimer(hwnd, kFilterTimer);
      if (DialogState* st = State(hwnd)) {
        GetWindowTextW(st->search, st->filter, ARRAYSIZE(st->filter));
        st->scrollY = 0;
        LayoutContent(*st);
      }
    }
    return TRUE;
  case WM_COMMAND: {
    const WORD id = LOWORD(wParam);
    const WORD code = HIWORD(wParam);
    if (code == EN_CHANGE && id == kIdSearch) {
      if (DialogState* st = State(hwnd); !st || !st->updating) {
      SetTimer(hwnd, kFilterTimer, 120, nullptr);
      }
      return TRUE;
    }
    if (id == IDOK) {
      return TRUE;
    }
    DialogState* st = State(hwnd);
    const bool suppressCancel = st && GetTickCount64() < st->suppressCloseUntil;
    if (st && code == BN_CLICKED && id >= kNavIdBase && id < kNavIdBase + static_cast<int>(ARRAYSIZE(kNavOrder))) {
      SelectCategory(hwnd, *st, kNavOrder[id - kNavIdBase]);
      return TRUE;
    }
    if (id == kIdResetDefaults && code == BN_CLICKED) {
      if (st) {
        ResetToFactoryDefaults(hwnd, *st);
      }
      return TRUE;
    }
    if (id == kIdCancelBtn && code == BN_CLICKED) {
      if (!st || suppressCancel) {
        return TRUE;
      }
      RestoreBaseline(*st);
      UpdateEnabled(*st);
      RefreshTheme(hwnd, *st);
      EndDialog(hwnd, IDCANCEL);
      return TRUE;
    }
    if (id == kIdApply && code == BN_CLICKED) {
      if (!st) {
        return TRUE;
      }
        if (SaveFromControls(hwnd, *st)) {
        CaptureBaseline(*st);
        ClearBanner(hwnd, *st);
      }
      return TRUE;
    }
    if (id == kIdOk && code == BN_CLICKED) {
      if (!st) {
        return TRUE;
      }
      if (SaveFromControls(hwnd, *st)) {
        EndDialog(hwnd, IDOK);
      }
      return TRUE;
    }
    if (id == IDCANCEL) {
      EndDialog(hwnd, IDCANCEL);
      return TRUE;
    }
    break;
  }
  case WM_DESTROY:
    if (GetApp().settingsHwnd == hwnd) {
      GetApp().settingsHwnd = nullptr;
    }
    if (DialogState* st = State(hwnd)) {
      RemoveSubclasses(*st);
      FreeBrushes(*st);
      if (st->uiFont) {
        DeleteObject(st->uiFont);
      }
      if (st->headerFont) {
        DeleteObject(st->headerFont);
      }
      if (st->previewFont) {
        DeleteObject(st->previewFont);
      }
      delete st;
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
    }
    return TRUE;
  default:
    break;
  }
  return FALSE;
}

bool RectsOverlap(const RECT& a, const RECT& b) {
  if (a.right <= a.left || a.bottom <= a.top || b.right <= b.left || b.bottom <= b.top) {
    return false;
  }
  return a.left < b.right && b.left < a.right && a.top < b.bottom && b.top < a.bottom;
}

bool RectInside(const RECT& rc, int left, int top, int right, int bottom) {
  return rc.left >= left && rc.top >= top && rc.right <= right && rc.bottom <= bottom && rc.right > rc.left &&
         rc.bottom > rc.top;
}

int ScrollClientForDialog(int dpi, int clientDip) {
  const int clientW = Dip(dpi, clientDip);
  const int outerPad = Dip(dpi, 12);
  const int contentX = Dip(dpi, kSidebarW) + Dip(dpi, 4);
  const int contentW = (std::max)(1, clientW - contentX - outerPad);
  const int scrollbar = Dip(dpi, 17);
  return (std::max)(Dip(dpi, 120), contentW - scrollbar);
}

bool RowFits(const RowPlacement& place, int left, int top, int width) {
  const int right = left + width;
  const int bottom = top + place.rowAdvance;
  if (!RectInside(place.control, left, top, right, bottom)) {
    return false;
  }
  if (place.control.bottom - place.control.top > place.rowAdvance) {
    return false;
  }
  if (place.hasLabel) {
    if (!RectInside(place.label, left, top, right, bottom) || RectsOverlap(place.label, place.control)) {
      return false;
    }
  }
  if (place.hasSwatch) {
    if (!RectInside(place.swatch, left, top, right, bottom) || RectsOverlap(place.control, place.swatch)) {
      return false;
    }
    if (place.swatch.right > right || place.control.right > place.swatch.left) {
      return false;
    }
  }
  if (place.control.right > right || place.control.left < left) {
    return false;
  }
  return true;
}

bool LayoutRowsFit(int dpi, int viewW) {
  const RowBand band = RowBandForView(dpi, viewW);
  const struct Sample {
    SettingType type;
    bool swatch;
  } samples[] = {
      {SettingType::Bool, false}, {SettingType::Enum, false}, {SettingType::Int, false},
      {SettingType::String, false}, {SettingType::String, true},
  };
  int top = Dip(dpi, 40);
  int previousBottom = top;
  for (const Sample& sample : samples) {
    const RowPlacement place = MeasureRow(dpi, sample.type, sample.swatch, band.left, top, band.width);
    if (place.rowAdvance <= 0 || top < previousBottom || !RowFits(place, band.left, top, band.width)) {
      return false;
    }
    if (sample.type == SettingType::Enum &&
        place.control.bottom - place.control.top > Dip(dpi, kControlH)) {
      return false;
    }
    previousBottom = place.control.bottom;
    top += place.rowAdvance;
  }
  return true;
}

COLORREF g_probePixel = CLR_INVALID;

LRESULT CALLBACK PaintProbeProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
  case WM_ERASEBKGND:
    return 1;
  case WM_PAINT: {
    {
      PaintSession session(hwnd);
      if (HDC hdc = session.dc()) {
        RECT rc = {};
        GetClientRect(hwnd, &rc);
        HBRUSH brush = CreateSolidBrush(RGB(18, 52, 86));
        if (brush) {
          FillRect(hdc, &rc, brush);
          DeleteObject(brush);
        }
      }
    }
    HDC hdc = GetDC(hwnd);
    if (hdc) {
      g_probePixel = GetPixel(hdc, 4, 4);
      ReleaseDC(hwnd, hdc);
    }
    return 0;
  }
  default:
    return DefWindowProcW(hwnd, msg, wParam, lParam);
  }
}

bool PaintSessionSelfTest() {
  g_probePixel = CLR_INVALID;
  const wchar_t* className = L"DynamicIslandLux.PaintProbe";
  WNDCLASSEXW wc = {};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = PaintProbeProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
  wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
  wc.lpszClassName = className;
  if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    LUX_LOGE(L"Settings dialog self-test failed: paint probe class");
    return false;
  }
  // On a real monitor: off-screen (−3200,−3200) GetPixel often returns CLR_INVALID under DWM.
  POINT probeOrigin = {0, 0};
  if (HMONITOR monitor = MonitorFromPoint(probeOrigin, MONITOR_DEFAULTTOPRIMARY)) {
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(monitor, &mi)) {
      probeOrigin.x = mi.rcMonitor.left;
      probeOrigin.y = mi.rcMonitor.top;
    }
  }
  HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, className, L"", WS_POPUP,
                              probeOrigin.x, probeOrigin.y, 80, 48, nullptr, nullptr, wc.hInstance,
                              nullptr);
  if (!hwnd) {
    UnregisterClassW(className, wc.hInstance);
    LUX_LOGE(L"Settings dialog self-test failed: paint probe window");
    return false;
  }
  SetWindowPos(hwnd, HWND_BOTTOM, probeOrigin.x, probeOrigin.y, 80, 48,
               SWP_NOACTIVATE | SWP_SHOWWINDOW);
  UpdateWindow(hwnd);
  const COLORREF pixel = g_probePixel;
  DestroyWindow(hwnd);
  UnregisterClassW(className, wc.hInstance);
  if (pixel != RGB(18, 52, 86)) {
    LUX_LOGE(L"Settings dialog self-test failed: PaintSession blit before EndPaint (pixel=0x%08lX)",
             static_cast<unsigned long>(pixel));
    return false;
  }
  return true;
}

bool BufferedDcFlushSelfTest() {
  bool createdScreen = false;
  HDC screen = GetDC(nullptr);
  if (!screen) {
    screen = CreateDCW(L"DISPLAY", nullptr, nullptr, nullptr);
    createdScreen = screen != nullptr;
  }
  if (!screen) {
    LUX_LOGE(L"Settings dialog self-test failed: screen DC");
    return false;
  }
  HDC mem = CreateCompatibleDC(screen);
  HBITMAP bmp = mem ? CreateCompatibleBitmap(screen, 24, 24) : nullptr;
  HGDIOBJ old = (mem && bmp) ? SelectObject(mem, bmp) : nullptr;
  bool ok = mem && bmp && old;
  if (ok) {
    RECT rc = {0, 0, 24, 24};
    HBRUSH white = CreateSolidBrush(RGB(255, 255, 255));
    if (white) {
      FillRect(mem, &rc, white);
      DeleteObject(white);
    }
    {
      BufferedDc buffer;
      buffer.Attach(mem, 24, 24);
      HBRUSH ink = CreateSolidBrush(RGB(200, 16, 32));
      if (buffer.dc() && ink) {
        FillRect(buffer.dc(), &rc, ink);
      }
      if (ink) {
        DeleteObject(ink);
      }
      if (GetPixel(mem, 4, 4) != RGB(255, 255, 255)) {
        ok = false;
      }
      buffer.Flush();
      if (GetPixel(mem, 4, 4) != RGB(200, 16, 32)) {
        ok = false;
      }
    }
    if (GetPixel(mem, 4, 4) != RGB(200, 16, 32)) {
      ok = false;
    }
  }
  if (old) {
    SelectObject(mem, old);
  }
  if (bmp) {
    DeleteObject(bmp);
  }
  if (mem) {
    DeleteDC(mem);
  }
  if (createdScreen) {
    DeleteDC(screen);
  } else {
    ReleaseDC(nullptr, screen);
  }
  if (!ok) {
    LUX_LOGE(L"Settings dialog self-test failed: BufferedDc flush");
  }
  return ok;
}

void ReportSettingsSelfTest(const wchar_t* name) {
  fwprintf(stderr, L"Settings dialog self-test failed: %s\n", name ? name : L"");
  LUX_LOGE(L"Settings dialog self-test failed: %s", name);
}

bool SettingsDialogSelfTestImpl() {
  bool failed = false;
  auto check = [&](bool condition, const wchar_t* name) {
    if (!condition) {
      ReportSettingsSelfTest(name);
      failed = true;
    }
  };

  check(CardForKey(L"Modules.MediaLikeBridge", SettingGroup::Modules) == SettingsUiLoc::SettingsCard::Media,
        L"like bridge card");
  check(CardForKey(L"Modules.MediaLikeBridgePort", SettingGroup::Modules) == SettingsUiLoc::SettingsCard::Media,
        L"like bridge port card");
  check(CardForKey(L"Modules.Notifications", SettingGroup::Modules) == SettingsUiLoc::SettingsCard::DoNotDisturb,
        L"notifications card");
  check(CardForKey(L"Modules.MessengerApps", SettingGroup::Modules) == SettingsUiLoc::SettingsCard::DoNotDisturb,
        L"messenger apps card");
  check(CardForKey(L"Themes.ThemePreset", SettingGroup::Themes) == SettingsUiLoc::SettingsCard::ThemeLook,
        L"preset card");

  const SettingsUiLoc::Lang langs[] = {SettingsUiLoc::Lang::En, SettingsUiLoc::Lang::Uk, SettingsUiLoc::Lang::Ru};
  for (const SettingDef& def : kSettingSchema) {
    if (def.group == SettingGroup::Runtime) {
      continue;
    }
    check(CardRank(CardForKey(def.key, def.group)) < 100, def.key);
    for (SettingsUiLoc::Lang lang : langs) {
      const wchar_t* label = SettingsUiLoc::SettingLabel(lang, def.key);
      if (!label || !label[0] || wcscmp(label, def.key) == 0) {
        wchar_t detail[192] = {};
        swprintf_s(detail, L"missing label %s", def.key);
        ReportSettingsSelfTest(detail);
        failed = true;
      }
    }
  }
  check(wcscmp(SettingsUiLoc::SettingLabel(SettingsUiLoc::Lang::En, L"Modules.Notifications"),
               L"Windows notifications") == 0,
        L"notifications en");
  check(wcscmp(SettingsUiLoc::SettingLabel(SettingsUiLoc::Lang::Uk, L"Modules.Notifications"),
               L"Сповіщення Windows") == 0,
        L"notifications uk");
  check(wcscmp(SettingsUiLoc::SettingLabel(SettingsUiLoc::Lang::En, L"Modules.MessengerApps"),
               L"Messenger apps (comma-separated)") == 0,
        L"messenger en");
  check(wcscmp(SettingsUiLoc::SettingLabel(SettingsUiLoc::Lang::Uk, L"Modules.MessengerApps"),
               L"Месенджери (через кому)") == 0,
        L"messenger uk");
  check(wcscmp(SettingsUiLoc::SettingLabel(SettingsUiLoc::Lang::En, L"Modules.MediaLikeBridge"),
               L"Like / Dislike for players (YTMDesktop: Companion Server)") == 0,
        L"like bridge en");
  check(wcscmp(SettingsUiLoc::SettingLabel(SettingsUiLoc::Lang::Uk, L"Modules.MediaLikeBridgePort"),
               L"Порт браузерного моста (127.0.0.1)") == 0,
        L"like bridge port uk");

  const int dpis[] = {96, 120, 144, 192};
  const int clients[] = {kMinClientW, kDlgClientW, 1280, 1920, 2560};
  for (int dpi : dpis) {
    check(LayoutRowsFit(dpi, Dip(dpi, 160)), L"narrow row fit");
    for (int client : clients) {
      if (!LayoutRowsFit(dpi, ScrollClientForDialog(dpi, client))) {
        wchar_t detail[96] = {};
        swprintf_s(detail, L"row fit dpi %d client %d", dpi, client);
        ReportSettingsSelfTest(detail);
        failed = true;
      }
    }
  }

  check(BufferedDcFlushSelfTest(), L"buffer flush");
  check(PaintSessionSelfTest(), L"paint session");
  return !failed;
}

}  // namespace

bool SettingsDialogSelfTest() {
  return SettingsDialogSelfTestImpl();
}

void OpenSettingsDialog(HWND owner) {
  if (IsWindow(GetApp().settingsHwnd)) {
    SetForegroundWindow(GetApp().settingsHwnd);
    return;
  }
#pragma pack(push, 2)
  struct {
    DLGTEMPLATE dlg;
    WORD menu;
    WORD windowClass;
    wchar_t title[1];
  } tpl = {};
#pragma pack(pop)
  tpl.dlg.style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MAXIMIZEBOX | WS_CLIPCHILDREN |
                  WS_CLIPSIBLINGS;
  tpl.dlg.cdit = 0;
  tpl.dlg.cx = 320;
  tpl.dlg.cy = 240;
  tpl.menu = 0;
  tpl.windowClass = 0;
  tpl.title[0] = 0;
  DialogBoxIndirectParamW(GetModuleHandleW(nullptr), &tpl.dlg, owner, SettingsProc, 0);
}
