#include "settings_dialog.h"

#include "autostart.h"
#include "logging.h"
#include "lux_overlay.h"
#include "resource.h"
#include "settings_schema.h"
#include "settings_store.h"

#include <commctrl.h>

#include <cwctype>
#include <string>
#include <vector>

namespace {
constexpr int kControlIdBase = 4000;
constexpr int kIdSearch = 3900;
constexpr int kIdAutostart = 3901;
constexpr UINT_PTR kFilterTimer = 1;

const wchar_t* GroupTitle(SettingGroup g) {
  switch (g) {
  case SettingGroup::General:
    return L"General";
  case SettingGroup::Appearance:
    return L"Appearance";
  case SettingGroup::Themes:
    return L"Themes";
  case SettingGroup::Animations:
    return L"Animations";
  case SettingGroup::Behavior:
    return L"Behavior";
  case SettingGroup::Shortcuts:
    return L"Shortcuts";
  case SettingGroup::Modules:
    return L"Modules";
  case SettingGroup::Runtime:
    return L"Runtime";
  }
  return L"Other";
}

struct RowRef {
  size_t schemaIndex = 0;
  HWND label = nullptr;
  HWND control = nullptr;
  HWND groupHeader = nullptr;
};

struct DialogState {
  HWND search = nullptr;
  HWND autostart = nullptr;
  std::vector<RowRef> rows;
  wchar_t filter[128] = {};
};

DialogState* State(HWND hwnd) {
  return reinterpret_cast<DialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
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

void LayoutRows(HWND hwnd, DialogState& st) {
  int y = 48;
  SettingGroup lastGroup = SettingGroup::Runtime;
  bool first = true;

  for (RowRef& row : st.rows) {
    const SettingDef& def = kSettingSchema[row.schemaIndex];
    const bool match = ContainsNoCase(def.key, st.filter);
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
        SetWindowPos(row.groupHeader, nullptr, 16, y, 520, 20, SWP_NOZORDER | SWP_SHOWWINDOW);
        y += 24;
      }
      lastGroup = def.group;
      first = false;
    }
    SetWindowPos(row.label, nullptr, 24, y + 3, 260, 18, SWP_NOZORDER);
    SetWindowPos(row.control, nullptr, 300, y, 240, 24, SWP_NOZORDER);
    y += 30;
  }

  if (st.autostart) {
    SetWindowPos(st.autostart, nullptr, 24, y + 10, 420, 22, SWP_NOZORDER | SWP_SHOWWINDOW);
    y += 44;
  }
  SetWindowPos(GetDlgItem(hwnd, IDOK), nullptr, 360, y, 90, 28, SWP_NOZORDER);
  SetWindowPos(GetDlgItem(hwnd, IDCANCEL), nullptr, 460, y, 90, 28, SWP_NOZORDER);
  SetWindowPos(hwnd, nullptr, 0, 0, 580, y + 80, SWP_NOMOVE | SWP_NOZORDER);
}

void BuildControls(HWND hwnd, DialogState& st) {
  st.search = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                              WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 16, 12, 520, 24, hwnd,
                              reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdSearch)),
                              GetModuleHandleW(nullptr), nullptr);
  SendMessageW(st.search, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Filter settings…"));

  SettingGroup prev = SettingGroup::Runtime;
  for (size_t i = 0; i < kSettingSchemaCount; ++i) {
    const SettingDef& def = kSettingSchema[i];
    if (def.group == SettingGroup::Runtime) {
      continue;
    }
    RowRef row;
    row.schemaIndex = i;
    if (def.group != prev) {
      row.groupHeader = CreateWindowExW(
          0, L"STATIC", GroupTitle(def.group), WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 0, 0, hwnd,
          nullptr, GetModuleHandleW(nullptr), nullptr);
      prev = def.group;
    }
    row.label = CreateWindowExW(0, L"STATIC", def.key, WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd,
                                nullptr, GetModuleHandleW(nullptr), nullptr);
    const int id = kControlIdBase + static_cast<int>(i);
    if (def.type == SettingType::Bool) {
      const bool on = SettingsGetBool(def.key, def.defaultString && def.defaultString[0] == L'1');
      row.control = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0, 0,
                                    0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                    GetModuleHandleW(nullptr), nullptr);
      CheckDlgButton(hwnd, id, on ? BST_CHECKED : BST_UNCHECKED);
    } else if (def.type == SettingType::Enum && def.enums && def.enumCount > 0) {
      row.control = CreateWindowExW(0, L"COMBOBOX", L"",
                                    WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 0, 0, 0, 0,
                                    hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                    GetModuleHandleW(nullptr), nullptr);
      const std::wstring cur = SettingsGetString(def.key, def.defaultString);
      int select = 0;
      for (size_t e = 0; e < def.enumCount; ++e) {
        SendMessageW(row.control, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(def.enums[e].label));
        if (_wcsicmp(def.enums[e].value, cur.c_str()) == 0) {
          select = static_cast<int>(e);
        }
      }
      SendMessageW(row.control, CB_SETCURSEL, static_cast<WPARAM>(select), 0);
    } else {
      const std::wstring value = SettingsGetString(def.key, def.defaultString);
      row.control = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", value.c_str(),
                                    WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd,
                                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                    GetModuleHandleW(nullptr), nullptr);
    }
    st.rows.push_back(row);
  }

  st.autostart =
      CreateWindowExW(0, L"BUTTON", L"Autostart with Windows (HKCU Run + StartupApproved)",
                      WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0, 0, 0, 0, hwnd,
                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdAutostart)),
                      GetModuleHandleW(nullptr), nullptr);
  CheckDlgButton(hwnd, kIdAutostart, AutostartIsEnabled() ? BST_CHECKED : BST_UNCHECKED);

  CreateWindowExW(0, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 0, 0, 90, 28, hwnd,
                  reinterpret_cast<HMENU>(IDOK), GetModuleHandleW(nullptr), nullptr);
  CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE, 0, 0, 90, 28, hwnd,
                  reinterpret_cast<HMENU>(IDCANCEL), GetModuleHandleW(nullptr), nullptr);

  LayoutRows(hwnd, st);
}

bool SaveFromControls(HWND hwnd, DialogState& st) {
  for (const RowRef& row : st.rows) {
    const SettingDef& def = kSettingSchema[row.schemaIndex];
    const int id = kControlIdBase + static_cast<int>(row.schemaIndex);
    if (def.type == SettingType::Bool) {
      SettingsSetBool(def.key, IsDlgButtonChecked(hwnd, id) == BST_CHECKED);
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
  AutostartSetEnabled(IsDlgButtonChecked(hwnd, kIdAutostart) == BST_CHECKED);
  if (!SettingsStoreSaveUser()) {
    LUX_LOGE(L"Settings dialog: SettingsStoreSaveUser failed");
    return false;
  }
  LuxOverlay_OnSettingsChanged();
  return true;
}

INT_PTR CALLBACK SettingsProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  (void)lParam;
  switch (msg) {
  case WM_INITDIALOG: {
    auto* st = new DialogState();
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));
    SetWindowTextW(hwnd, L"Dynamic Island Lux — Settings");
    BuildControls(hwnd, *st);
    return TRUE;
  }
  case WM_TIMER:
    if (wParam == kFilterTimer) {
      KillTimer(hwnd, kFilterTimer);
      if (DialogState* st = State(hwnd)) {
        GetWindowTextW(st->search, st->filter, ARRAYSIZE(st->filter));
        LayoutRows(hwnd, *st);
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
  tpl.dlg.cx = 290;
  tpl.dlg.cy = 280;
  tpl.menu = 0;
  tpl.windowClass = 0;
  tpl.title[0] = 0;
  DialogBoxIndirectParamW(GetModuleHandleW(nullptr), &tpl.dlg, owner, SettingsProc, 0);
}
