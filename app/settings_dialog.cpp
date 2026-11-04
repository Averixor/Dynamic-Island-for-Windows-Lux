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

// DWMWA_USE_IMMERSIVE_DARK_MODE (20) — Win10 1809+ / Win11.
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

const wchar_t kScrollClass[] = L"DynamicIslandLux.SettingsScroll";

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
  SetWindowTheme(hwnd, dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
}

void ThemeAllControls(DialogState& st) {
  ThemeControl(st.search, st.dark);
  ThemeControl(st.scroll, st.dark);
  ThemeControl(st.autostart, st.dark);
  ThemeControl(st.okBtn, st.dark);
  ThemeControl(st.cancelBtn, st.dark);
  for (RowRef& row : st.rows) {
    ThemeControl(row.groupHeader, st.dark);
    ThemeControl(row.label, st.dark);
    ThemeControl(row.control, st.dark);
  }
  if (st.scroll) {
    InvalidateRect(st.scroll, nullptr, TRUE);
  }
}

HBRUSH HandleCtlColor(DialogState& st, HDC hdc, int ctlType) {
  SetTextColor(hdc, st.text);
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

void ApplyScrollOffset(DialogState& st) {
  if (!st.scroll) {
    return;
  }
  int contentY = 8;
  SettingGroup lastGroup = SettingGroup::Runtime;
  bool first = true;

  for (RowRef& row : st.rows) {
    const SettingDef& def = kSettingSchema[row.schemaIndex];
    const bool match = RowMatchesFilter(row, st.filter);
    if (row.groupHeader) {
      ShowWindow(row.groupHeader, SW_HIDE);
    }
    ShowWindow(row.label, match ? SW_SHOW : SW_HIDE);
    ShowWindow(row.control, match ? SW_SHOW : SW_HIDE);
    if (!match) {
      continue;
    }
    if (first || def.group != lastGroup) {
      if (row.groupHeader) {
        SetWindowPos(row.groupHeader, nullptr, 8, contentY - st.scrollY, kContentW, 20,
                     SWP_NOZORDER | SWP_SHOWWINDOW);
        contentY += 24;
      }
      lastGroup = def.group;
      first = false;
    }
    SetWindowPos(row.label, nullptr, 16, contentY - st.scrollY + 3, 250, 18, SWP_NOZORDER);
    SetWindowPos(row.control, nullptr, 280, contentY - st.scrollY, 240, 24, SWP_NOZORDER);
    contentY += 30;
  }

  if (st.autostart) {
    SetWindowPos(st.autostart, nullptr, 16, contentY - st.scrollY + 10, 420, 22,
                 SWP_NOZORDER | SWP_SHOWWINDOW);
    contentY += 44;
  }

  st.contentHeight = contentY + 12;
  UpdateScrollBar(st);
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
  ApplyScrollOffset(st);
}

void ScrollBy(DialogState& st, int delta) {
  RECT rc = {};
  GetClientRect(st.scroll, &rc);
  const int page = (std::max)(1, static_cast<int>(rc.bottom - rc.top));
  const int maxScroll = (std::max)(0, st.contentHeight - page);
  const int next = (std::clamp)(st.scrollY + delta, 0, maxScroll);
  if (next == st.scrollY) {
    return;
  }
  st.scrollY = next;
  ApplyScrollOffset(st);
}

void RefreshSystemTheme(HWND hwnd, DialogState& st) {
  LoadThemeColors(st);
  ApplyDarkTitleBar(hwnd, st.dark);
  ThemeAllControls(st);
  InvalidateRect(hwnd, nullptr, TRUE);
  if (st.scroll) {
    InvalidateRect(st.scroll, nullptr, TRUE);
  }
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
      pos -= 30;
      break;
    case SB_LINEDOWN:
      pos += 30;
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
    ScrollBy(*st, pos - st->scrollY);
    return 0;
  }
  case WM_MOUSEWHEEL: {
    if (!st) {
      break;
    }
    const int delta = -GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA * 48;
    ScrollBy(*st, delta);
    return 0;
  }
  case WM_CTLCOLORSTATIC:
  case WM_CTLCOLOREDIT:
  case WM_CTLCOLORBTN:
  case WM_CTLCOLORLISTBOX:
    if (st) {
      const int type = (msg == WM_CTLCOLOREDIT)       ? CTLCOLOR_EDIT
                       : (msg == WM_CTLCOLORLISTBOX)  ? CTLCOLOR_LISTBOX
                       : (msg == WM_CTLCOLORBTN)      ? CTLCOLOR_BTN
                                                      : CTLCOLOR_STATIC;
      return reinterpret_cast<LRESULT>(HandleCtlColor(*st, reinterpret_cast<HDC>(wParam), type));
    }
    break;
  case WM_ERASEBKGND: {
    if (st && st->bgBrush) {
      RECT rc = {};
      GetClientRect(hwnd, &rc);
      FillRect(reinterpret_cast<HDC>(wParam), &rc, st->bgBrush);
      return 1;
    }
    break;
  }
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void EnsureScrollClass() {
  static bool registered = false;
  if (registered) {
    return;
  }
  WNDCLASSEXW wc = {};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = ScrollProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = nullptr;
  wc.lpszClassName = kScrollClass;
  RegisterClassExW(&wc);
  registered = true;
}

void BuildControls(HWND hwnd, DialogState& st) {
  st.lang = SettingsUiLoc::ResolveLang();
  EnsureScrollClass();
  LoadThemeColors(st);
  ApplyDarkTitleBar(hwnd, st.dark);

  st.search = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                              WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 16, kTopPad, 520, kSearchH, hwnd,
                              reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdSearch)),
                              GetModuleHandleW(nullptr), nullptr);
  SendMessageW(st.search, EM_SETCUEBANNER, TRUE,
               reinterpret_cast<LPARAM>(SettingsUiLoc::FilterCue(st.lang)));

  st.scroll = CreateWindowExW(WS_EX_CLIENTEDGE, kScrollClass, L"",
                              WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPCHILDREN, 8, kScrollTop,
                              kDlgClientW - 16, 400, hwnd, nullptr, GetModuleHandleW(nullptr),
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
          WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 0, 0, st.scroll, nullptr, GetModuleHandleW(nullptr),
          nullptr);
      prev = def.group;
    }
    row.label = CreateWindowExW(0, L"STATIC", row.labelText.c_str(), WS_CHILD | WS_VISIBLE, 0, 0, 0,
                                0, st.scroll, nullptr, GetModuleHandleW(nullptr), nullptr);
    const int id = kControlIdBase + static_cast<int>(i);
    if (def.type == SettingType::Bool) {
      const bool on = SettingsGetBool(def.key, def.defaultString && def.defaultString[0] == L'1');
      row.control = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0, 0,
                                    0, 0, st.scroll, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                    GetModuleHandleW(nullptr), nullptr);
      SendMessageW(row.control, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
    } else if (def.type == SettingType::Enum && def.enums && def.enumCount > 0) {
      row.control = CreateWindowExW(0, L"COMBOBOX", L"",
                                    WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 0, 0, 0, 0,
                                    st.scroll, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
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
      const DWORD editStyle = WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL |
                              (wcscmp(def.key, L"app.version") == 0 ? ES_READONLY : 0);
      row.control = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", value.c_str(), editStyle, 0, 0, 0, 0,
                                    st.scroll, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                    GetModuleHandleW(nullptr), nullptr);
    }
    st.rows.push_back(std::move(row));
  }

  st.autostart =
      CreateWindowExW(0, L"BUTTON", SettingsUiLoc::Autostart(st.lang),
                      WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0, 0, 0, 0, st.scroll,
                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdAutostart)),
                      GetModuleHandleW(nullptr), nullptr);
  SendMessageW(st.autostart, BM_SETCHECK, AutostartIsEnabled() ? BST_CHECKED : BST_UNCHECKED, 0);

  st.okBtn =
      CreateWindowExW(0, L"BUTTON", SettingsUiLoc::Ok(st.lang), WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                      0, 0, 90, 28, hwnd, reinterpret_cast<HMENU>(IDOK), GetModuleHandleW(nullptr),
                      nullptr);
  st.cancelBtn =
      CreateWindowExW(0, L"BUTTON", SettingsUiLoc::Cancel(st.lang), WS_CHILD | WS_VISIBLE, 0, 0, 90, 28,
                      hwnd, reinterpret_cast<HMENU>(IDCANCEL), GetModuleHandleW(nullptr), nullptr);

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
      const int type = (msg == WM_CTLCOLOREDIT)       ? CTLCOLOR_EDIT
                       : (msg == WM_CTLCOLORLISTBOX)  ? CTLCOLOR_LISTBOX
                       : (msg == WM_CTLCOLORBTN)      ? CTLCOLOR_BTN
                       : (msg == WM_CTLCOLORDLG)      ? CTLCOLOR_DLG
                                                      : CTLCOLOR_STATIC;
      return reinterpret_cast<INT_PTR>(HandleCtlColor(*st, reinterpret_cast<HDC>(wParam), type));
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
        ApplyScrollOffset(*st);
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
  tpl.dlg.style = DS_MODALFRAME | DS_CENTER | WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;
  tpl.dlg.cdit = 0;
  tpl.dlg.cx = 300;
  tpl.dlg.cy = 300;
  tpl.menu = 0;
  tpl.windowClass = 0;
  tpl.title[0] = 0;
  DialogBoxIndirectParamW(GetModuleHandleW(nullptr), &tpl.dlg, owner, SettingsProc, 0);
}
