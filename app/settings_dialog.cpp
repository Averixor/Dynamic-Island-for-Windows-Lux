#include "settings_dialog.h"

#include "autostart.h"
#include "logging.h"
#include "lux_overlay.h"
#include "resource.h"
#include "settings_schema.h"
#include "settings_store.h"
#include "settings_ui_loc.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>

#include <algorithm>
#include <cwctype>
#include <string>
#include <vector>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")

namespace {
constexpr int kControlIdBase = 4000;
constexpr int kIdSearch = 3900;
constexpr int kIdAutostart = 3901;
constexpr UINT_PTR kFilterTimer = 1;

constexpr int kDlgClientW = 580;
constexpr int kDlgClientH = 560;
constexpr int kSearchH = 24;
constexpr int kTopPad = 12;
constexpr int kFooterH = 52;
constexpr int kScrollTop = kTopPad + kSearchH + 12;
constexpr int kContentW = 540;
constexpr int kRowH = 30;
constexpr int kGroupH = 24;
constexpr int kControlH = 24;
constexpr int kLabelH = 18;

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

const wchar_t kScrollClass[] = L"DynamicIslandLux.SettingsScroll";
const wchar_t kContentClass[] = L"DynamicIslandLux.SettingsContent";

struct RowRef {
  size_t schemaIndex = 0;
  HWND label = nullptr;
  HWND control = nullptr;
  HWND groupHeader = nullptr;
  std::wstring labelText;
};

struct DialogState {
  HWND search = nullptr;
  HWND scroll = nullptr;
  HWND content = nullptr;
  HWND autostart = nullptr;
  HWND okBtn = nullptr;
  HWND cancelBtn = nullptr;
  std::vector<RowRef> rows;
  wchar_t filter[128] = {};
  SettingsUiLoc::Lang lang = SettingsUiLoc::Lang::En;
  int contentHeight = 0;
  int scrollY = 0;
  bool dark = false;
  COLORREF bg = RGB(255, 255, 255);
  COLORREF text = RGB(0, 0, 0);
  COLORREF editBg = RGB(255, 255, 255);
  HBRUSH bgBrush = nullptr;
  HBRUSH editBrush = nullptr;
};

DialogState* State(HWND hwnd) {
  return reinterpret_cast<DialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

bool SystemPreferLightApps() {
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

void FreeThemeBrushes(DialogState& st) {
  if (st.bgBrush) {
    DeleteObject(st.bgBrush);
    st.bgBrush = nullptr;
  }
  if (st.editBrush) {
    DeleteObject(st.editBrush);
    st.editBrush = nullptr;
  }
}

void LoadThemeColors(DialogState& st) {
  FreeThemeBrushes(st);
  st.dark = !SystemPreferLightApps();
  if (st.dark) {
    st.bg = RGB(32, 32, 32);
    st.text = RGB(240, 240, 240);
    st.editBg = RGB(45, 45, 45);
  } else {
    st.bg = RGB(255, 255, 255);
    st.text = RGB(20, 20, 20);
    st.editBg = RGB(255, 255, 255);
  }
  st.bgBrush = CreateSolidBrush(st.bg);
  st.editBrush = CreateSolidBrush(st.editBg);
}

void ApplyDarkTitleBar(HWND hwnd, bool dark) {
  BOOL useDark = dark ? TRUE : FALSE;
  DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDark, sizeof(useDark));
}

void ThemeControl(HWND hwnd, bool dark) {
  if (!hwnd) {
    return;
  }
  // Explorer dark theme on STATIC fights WM_CTLCOLOR and leaves paint tears.
  wchar_t className[64] = {};
  GetClassNameW(hwnd, className, ARRAYSIZE(className));
  if (_wcsicmp(className, L"STATIC") == 0) {
    SetWindowTheme(hwnd, L"", L"");
    return;
  }
  SetWindowTheme(hwnd, dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
}

void ThemeAllControls(DialogState& st) {
  ThemeControl(st.search, st.dark);
  ThemeControl(st.autostart, st.dark);
  ThemeControl(st.okBtn, st.dark);
  ThemeControl(st.cancelBtn, st.dark);
  for (RowRef& row : st.rows) {
    ThemeControl(row.groupHeader, st.dark);
    ThemeControl(row.label, st.dark);
    ThemeControl(row.control, st.dark);
  }
}

HBRUSH HandleCtlColor(DialogState& st, HDC hdc, int ctlType) {
  SetTextColor(hdc, st.text);
  SetBkMode(hdc, OPAQUE);
  if (ctlType == CTLCOLOR_EDIT || ctlType == CTLCOLOR_LISTBOX) {
    SetBkColor(hdc, st.editBg);
    return st.editBrush ? st.editBrush : st.bgBrush;
  }
  SetBkColor(hdc, st.bg);
  return st.bgBrush;
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

void LayoutContentChildren(DialogState& st) {
  if (!st.content) {
    return;
  }

  int y = 8;
  SettingGroup lastGroup = SettingGroup::Runtime;
  bool first = true;
  int deferCount = 2;  // autostart may move; count visible rows below
  for (const RowRef& row : st.rows) {
    if (!RowMatchesFilter(row, st.filter)) {
      continue;
    }
    const SettingDef& def = kSettingSchema[row.schemaIndex];
    if (first || def.group != lastGroup) {
      if (row.groupHeader) {
        ++deferCount;
      }
      lastGroup = def.group;
      first = false;
    }
    deferCount += 2;
  }

  HDWP hdwp = BeginDeferWindowPos((std::max)(deferCount, 1));
  y = 8;
  lastGroup = SettingGroup::Runtime;
  first = true;

  for (RowRef& row : st.rows) {
    const SettingDef& def = kSettingSchema[row.schemaIndex];
    const bool match = RowMatchesFilter(row, st.filter);
    if (!match) {
      if (row.groupHeader) {
        ShowWindow(row.groupHeader, SW_HIDE);
      }
      ShowWindow(row.label, SW_HIDE);
      ShowWindow(row.control, SW_HIDE);
      continue;
    }

    if (first || def.group != lastGroup) {
      if (row.groupHeader) {
        hdwp = DeferWindowPos(hdwp, row.groupHeader, nullptr, 8, y, kContentW, 20,
                              SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        y += kGroupH;
      }
      lastGroup = def.group;
      first = false;
    } else if (row.groupHeader) {
      ShowWindow(row.groupHeader, SW_HIDE);
    }

    hdwp = DeferWindowPos(hdwp, row.label, nullptr, 16, y + 3, 250, kLabelH,
                          SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    hdwp = DeferWindowPos(hdwp, row.control, nullptr, 280, y, 240, kControlH,
                          SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    y += kRowH;
  }

  if (st.autostart) {
    hdwp = DeferWindowPos(hdwp, st.autostart, nullptr, 16, y + 10, 420, 22,
                          SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    y += 44;
  }

  if (hdwp) {
    EndDeferWindowPos(hdwp);
  }

  st.contentHeight = y + 12;
  UpdateScrollBar(st);
  PlaceContent(st);
  RedrawWindow(st.scroll, nullptr, nullptr,
               RDW_ERASE | RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

void LayoutRows(HWND hwnd, DialogState& st) {
  SetWindowPos(hwnd, nullptr, 0, 0, kDlgClientW + 16, kDlgClientH + 40, SWP_NOMOVE | SWP_NOZORDER);

  RECT client = {};
  GetClientRect(hwnd, &client);
  const int clientW = client.right - client.left;
  const int clientH = client.bottom - client.top;

  if (st.search) {
    SetWindowPos(st.search, nullptr, 16, kTopPad, clientW - 32, kSearchH, SWP_NOZORDER);
  }

  const int scrollH = (std::max)(80, clientH - kScrollTop - kFooterH);
  if (st.scroll) {
    SetWindowPos(st.scroll, nullptr, 8, kScrollTop, clientW - 16, scrollH, SWP_NOZORDER);
  }

  if (st.okBtn) {
    SetWindowPos(st.okBtn, nullptr, clientW - 200, clientH - 40, 90, 28, SWP_NOZORDER);
  }
  if (st.cancelBtn) {
    SetWindowPos(st.cancelBtn, nullptr, clientW - 100, clientH - 40, 90, 28, SWP_NOZORDER);
  }

  st.scrollY = 0;
  LayoutContentChildren(st);
}

void ScrollTo(DialogState& st, int pos) {
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

void RefreshSystemTheme(HWND hwnd, DialogState& st) {
  LoadThemeColors(st);
  ApplyDarkTitleBar(hwnd, st.dark);
  ThemeAllControls(st);
  InvalidateRect(hwnd, nullptr, TRUE);
  if (st.content) {
    InvalidateRect(st.content, nullptr, TRUE);
  }
  if (st.scroll) {
    InvalidateRect(st.scroll, nullptr, TRUE);
  }
}

LRESULT PaintSolidBackground(HWND hwnd, WPARAM wParam, DialogState* st) {
  if (!st || !st->bgBrush) {
    return DefWindowProcW(hwnd, WM_ERASEBKGND, wParam, 0);
  }
  RECT rc = {};
  GetClientRect(hwnd, &rc);
  FillRect(reinterpret_cast<HDC>(wParam), &rc, st->bgBrush);
  return 1;
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
      const int type = (msg == WM_CTLCOLOREDIT)      ? CTLCOLOR_EDIT
                       : (msg == WM_CTLCOLORLISTBOX) ? CTLCOLOR_LISTBOX
                       : (msg == WM_CTLCOLORBTN)     ? CTLCOLOR_BTN
                                                     : CTLCOLOR_STATIC;
      return reinterpret_cast<LRESULT>(HandleCtlColor(*st, reinterpret_cast<HDC>(wParam), type));
    }
    break;
  case WM_ERASEBKGND:
    return PaintSolidBackground(hwnd, wParam, st);
  case WM_MOUSEWHEEL:
    if (scroll) {
      return SendMessageW(scroll, msg, wParam, lParam);
    }
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
      pos -= kRowH;
      break;
    case SB_LINEDOWN:
      pos += kRowH;
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
    const int delta = -GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA * 48;
    ScrollTo(*st, st->scrollY + delta);
    return 0;
  }
  case WM_SIZE:
    if (st) {
      UpdateScrollBar(*st);
      PlaceContent(*st);
    }
    return 0;
  case WM_ERASEBKGND:
    return PaintSolidBackground(hwnd, wParam, st);
  case WM_CTLCOLORSTATIC:
  case WM_CTLCOLOREDIT:
  case WM_CTLCOLORBTN:
  case WM_CTLCOLORLISTBOX:
    if (st) {
      const int type = (msg == WM_CTLCOLOREDIT)      ? CTLCOLOR_EDIT
                       : (msg == WM_CTLCOLORLISTBOX) ? CTLCOLOR_LISTBOX
                       : (msg == WM_CTLCOLORBTN)     ? CTLCOLOR_BTN
                                                     : CTLCOLOR_STATIC;
      return reinterpret_cast<LRESULT>(HandleCtlColor(*st, reinterpret_cast<HDC>(wParam), type));
    }
    break;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void EnsureWindowClasses() {
  static bool registered = false;
  if (registered) {
    return;
  }
  WNDCLASSEXW scrollWc = {};
  scrollWc.cbSize = sizeof(scrollWc);
  scrollWc.lpfnWndProc = ScrollProc;
  scrollWc.hInstance = GetModuleHandleW(nullptr);
  scrollWc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  scrollWc.hbrBackground = nullptr;
  scrollWc.lpszClassName = kScrollClass;
  scrollWc.style = CS_DBLCLKS;
  RegisterClassExW(&scrollWc);

  WNDCLASSEXW contentWc = {};
  contentWc.cbSize = sizeof(contentWc);
  contentWc.lpfnWndProc = ContentProc;
  contentWc.hInstance = GetModuleHandleW(nullptr);
  contentWc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  contentWc.hbrBackground = nullptr;
  contentWc.lpszClassName = kContentClass;
  contentWc.style = CS_DBLCLKS;
  RegisterClassExW(&contentWc);
  registered = true;
}

void BuildControls(HWND hwnd, DialogState& st) {
  st.lang = SettingsUiLoc::ResolveLang();
  EnsureWindowClasses();
  LoadThemeColors(st);
  ApplyDarkTitleBar(hwnd, st.dark);

  st.search = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                              WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | ES_AUTOHSCROLL, 16, kTopPad,
                              520, kSearchH, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdSearch)),
                              GetModuleHandleW(nullptr), nullptr);
  SendMessageW(st.search, EM_SETCUEBANNER, TRUE,
               reinterpret_cast<LPARAM>(SettingsUiLoc::FilterCue(st.lang)));

  st.scroll =
      CreateWindowExW(WS_EX_CLIENTEDGE, kScrollClass, L"",
                      WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 8,
                      kScrollTop, kDlgClientW - 16, 400, hwnd, nullptr, GetModuleHandleW(nullptr),
                      nullptr);

  st.content = CreateWindowExW(0, kContentClass, L"",
                               WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 0,
                               kDlgClientW - 16, 400, st.scroll, nullptr, GetModuleHandleW(nullptr),
                               nullptr);

  SettingGroup prev = SettingGroup::Runtime;
  for (size_t i = 0; i < kSettingSchemaCount; ++i) {
    const SettingDef& def = kSettingSchema[i];
    if (def.group == SettingGroup::Runtime) {
      continue;
    }
    RowRef row;
    row.schemaIndex = i;
    row.labelText = SettingsUiLoc::SettingLabel(st.lang, def.key);
    if (def.group != prev) {
      row.groupHeader = CreateWindowExW(
          0, L"STATIC", SettingsUiLoc::GroupTitle(st.lang, def.group),
          WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | SS_LEFT, 0, 0, 0, 0, st.content, nullptr,
          GetModuleHandleW(nullptr), nullptr);
      HFONT font = reinterpret_cast<HFONT>(SendMessageW(hwnd, WM_GETFONT, 0, 0));
      if (!font) {
        font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
      }
      SendMessageW(row.groupHeader, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
      prev = def.group;
    }
    row.label = CreateWindowExW(0, L"STATIC", row.labelText.c_str(),
                                WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | SS_LEFT | SS_NOPREFIX, 0, 0,
                                0, 0, st.content, nullptr, GetModuleHandleW(nullptr), nullptr);
    const int id = kControlIdBase + static_cast<int>(i);
    if (def.type == SettingType::Bool) {
      const bool on = SettingsGetBool(def.key, def.defaultString && def.defaultString[0] == L'1');
      row.control = CreateWindowExW(0, L"BUTTON", L"",
                                    WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | BS_AUTOCHECKBOX, 0, 0, 0,
                                    0, st.content, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                    GetModuleHandleW(nullptr), nullptr);
      SendMessageW(row.control, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
    } else if (def.type == SettingType::Enum && def.enums && def.enumCount > 0) {
      row.control =
          CreateWindowExW(0, L"COMBOBOX", L"",
                          WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | CBS_DROPDOWNLIST | WS_VSCROLL, 0,
                          0, 0, 0, st.content, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                          GetModuleHandleW(nullptr), nullptr);
      const std::wstring cur = SettingsGetString(def.key, def.defaultString);
      int select = 0;
      for (size_t e = 0; e < def.enumCount; ++e) {
        const wchar_t* label = SettingsUiLoc::EnumLabel(st.lang, def, e);
        SendMessageW(row.control, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
        if (_wcsicmp(def.enums[e].value, cur.c_str()) == 0) {
          select = static_cast<int>(e);
        }
      }
      SendMessageW(row.control, CB_SETCURSEL, static_cast<WPARAM>(select), 0);
    } else {
      const std::wstring value = SettingsGetString(def.key, def.defaultString);
      const DWORD editStyle = WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | ES_AUTOHSCROLL |
                              (wcscmp(def.key, L"app.version") == 0 ? ES_READONLY : 0);
      row.control = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", value.c_str(), editStyle, 0, 0, 0, 0,
                                    st.content, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                    GetModuleHandleW(nullptr), nullptr);
    }
    st.rows.push_back(std::move(row));
  }

  st.autostart = CreateWindowExW(
      0, L"BUTTON", SettingsUiLoc::Autostart(st.lang),
      WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | BS_AUTOCHECKBOX, 0, 0, 0, 0, st.content,
      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdAutostart)), GetModuleHandleW(nullptr),
      nullptr);
  SendMessageW(st.autostart, BM_SETCHECK, AutostartIsEnabled() ? BST_CHECKED : BST_UNCHECKED, 0);

  st.okBtn =
      CreateWindowExW(0, L"BUTTON", SettingsUiLoc::Ok(st.lang),
                      WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | BS_DEFPUSHBUTTON, 0, 0, 90, 28, hwnd,
                      reinterpret_cast<HMENU>(IDOK), GetModuleHandleW(nullptr), nullptr);
  st.cancelBtn =
      CreateWindowExW(0, L"BUTTON", SettingsUiLoc::Cancel(st.lang),
                      WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, 90, 28, hwnd,
                      reinterpret_cast<HMENU>(IDCANCEL), GetModuleHandleW(nullptr), nullptr);

  ThemeAllControls(st);
  LayoutRows(hwnd, st);
}

bool SaveFromControls(HWND hwnd, DialogState& st) {
  (void)hwnd;
  for (const RowRef& row : st.rows) {
    const SettingDef& def = kSettingSchema[row.schemaIndex];
    if (def.type == SettingType::Bool) {
      SettingsSetBool(def.key, SendMessageW(row.control, BM_GETCHECK, 0, 0) == BST_CHECKED);
    } else if (def.type == SettingType::Enum && def.enums && def.enumCount > 0) {
      const int sel = static_cast<int>(SendMessageW(row.control, CB_GETCURSEL, 0, 0));
      if (sel >= 0 && static_cast<size_t>(sel) < def.enumCount) {
        SettingsSetString(def.key, def.enums[sel].value);
      }
    } else {
      wchar_t buf[512] = {};
      GetWindowTextW(row.control, buf, ARRAYSIZE(buf));
      SettingsSetString(def.key, buf);
    }
  }
  AutostartSetEnabled(SendMessageW(st.autostart, BM_GETCHECK, 0, 0) == BST_CHECKED);
  if (!SettingsStoreSaveUser()) {
    LUX_LOGE(L"Settings dialog: SettingsStoreSaveUser failed");
    return false;
  }
  LuxOverlay_OnSettingsChanged();
  return true;
}

INT_PTR CALLBACK SettingsProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
  case WM_INITDIALOG: {
    auto* st = new DialogState();
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));
    st->lang = SettingsUiLoc::ResolveLang();
    SetWindowTextW(hwnd, SettingsUiLoc::WindowTitle(st->lang));
    BuildControls(hwnd, *st);
    return TRUE;
  }
  case WM_CTLCOLORDLG:
  case WM_CTLCOLORSTATIC:
  case WM_CTLCOLOREDIT:
  case WM_CTLCOLORBTN:
  case WM_CTLCOLORLISTBOX:
    if (DialogState* st = State(hwnd)) {
      const int type = (msg == WM_CTLCOLOREDIT)      ? CTLCOLOR_EDIT
                       : (msg == WM_CTLCOLORLISTBOX) ? CTLCOLOR_LISTBOX
                       : (msg == WM_CTLCOLORBTN)     ? CTLCOLOR_BTN
                       : (msg == WM_CTLCOLORDLG)     ? CTLCOLOR_DLG
                                                     : CTLCOLOR_STATIC;
      return reinterpret_cast<INT_PTR>(HandleCtlColor(*st, reinterpret_cast<HDC>(wParam), type));
    }
    break;
  case WM_ERASEBKGND:
    if (DialogState* st = State(hwnd)) {
      return PaintSolidBackground(hwnd, wParam, st);
    }
    break;
  case WM_SETTINGCHANGE:
    if (lParam && _wcsicmp(reinterpret_cast<LPCWSTR>(lParam), L"ImmersiveColorSet") == 0) {
      if (DialogState* st = State(hwnd)) {
        RefreshSystemTheme(hwnd, *st);
      }
      return TRUE;
    }
    break;
  case WM_SIZE:
    if (DialogState* st = State(hwnd)) {
      LayoutRows(hwnd, *st);
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
        LayoutContentChildren(*st);
      }
    }
    return TRUE;
  case WM_COMMAND:
    if (HIWORD(wParam) == EN_CHANGE && LOWORD(wParam) == kIdSearch) {
      SetTimer(hwnd, kFilterTimer, 120, nullptr);
      return TRUE;
    }
    if (LOWORD(wParam) == IDOK) {
      if (DialogState* st = State(hwnd)) {
        if (SaveFromControls(hwnd, *st)) {
          EndDialog(hwnd, IDOK);
        }
      }
      return TRUE;
    }
    if (LOWORD(wParam) == IDCANCEL) {
      EndDialog(hwnd, IDCANCEL);
      return TRUE;
    }
    break;
  case WM_DESTROY:
    if (DialogState* st = State(hwnd)) {
      FreeThemeBrushes(*st);
      delete st;
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
    }
    return TRUE;
  }
  return FALSE;
}
}  // namespace

void OpenSettingsDialog(HWND owner) {
#pragma pack(push, 2)
  struct {
    DLGTEMPLATE dlg;
    WORD menu;
    WORD windowClass;
    wchar_t title[1];
  } tpl = {};
#pragma pack(pop)
  tpl.dlg.style =
      DS_MODALFRAME | DS_CENTER | WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;
  tpl.dlg.cdit = 0;
  tpl.dlg.cx = 300;
  tpl.dlg.cy = 300;
  tpl.menu = 0;
  tpl.windowClass = 0;
  tpl.title[0] = 0;
  DialogBoxIndirectParamW(GetModuleHandleW(nullptr), &tpl.dlg, owner, SettingsProc, 0);
}
