#include "settings_dialog.h"

#include "autostart.h"
#include "logging.h"
#include "lux_overlay.h"
#include "resource.h"
#include "settings_schema.h"
#include "settings_store.h"

#include <string>

namespace {
constexpr int kControlIdBase = 4000;

INT_PTR CALLBACK SettingsProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
  case WM_INITDIALOG: {
    SetWindowTextW(hwnd, L"Dynamic Island Lux — Settings");
    int y = 16;
    for (size_t i = 0; i < kSettingSchemaCount; ++i) {
      const SettingDef& def = kSettingSchema[i];
      if (def.group == SettingGroup::Runtime) {
        continue;
      }
      CreateWindowExW(0, L"STATIC", def.key, WS_CHILD | WS_VISIBLE, 16, y, 280, 18, hwnd,
                      nullptr, GetModuleHandleW(nullptr), nullptr);
      const int id = kControlIdBase + static_cast<int>(i);
      if (def.type == SettingType::Bool) {
        const bool on = SettingsGetBool(def.key, def.defaultString && def.defaultString[0] == L'1');
        CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 300, y, 24, 20,
                        hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                        GetModuleHandleW(nullptr), nullptr);
        CheckDlgButton(hwnd, id, on ? BST_CHECKED : BST_UNCHECKED);
      } else {
        const std::wstring value = SettingsGetString(def.key, def.defaultString);
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", value.c_str(),
                        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 300, y - 2, 220, 22, hwnd,
                        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                        GetModuleHandleW(nullptr), nullptr);
      }
      y += 28;
    }
    CreateWindowExW(0, L"BUTTON", L"Autostart with Windows",
                    WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 16, y + 8, 260, 22, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(kControlIdBase + 900)),
                    GetModuleHandleW(nullptr), nullptr);
    CheckDlgButton(hwnd, kControlIdBase + 900, AutostartIsEnabled() ? BST_CHECKED : BST_UNCHECKED);
    CreateWindowExW(0, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 320, y + 40, 90,
                    28, hwnd, reinterpret_cast<HMENU>(IDOK), GetModuleHandleW(nullptr), nullptr);
    CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE, 420, y + 40, 90, 28, hwnd,
                    reinterpret_cast<HMENU>(IDCANCEL), GetModuleHandleW(nullptr), nullptr);
    SetWindowPos(hwnd, nullptr, 0, 0, 560, y + 120, SWP_NOMOVE | SWP_NOZORDER);
    return TRUE;
  }
  case WM_COMMAND:
    if (LOWORD(wParam) == IDOK) {
      for (size_t i = 0; i < kSettingSchemaCount; ++i) {
        const SettingDef& def = kSettingSchema[i];
        if (def.group == SettingGroup::Runtime) {
          continue;
        }
        const int id = kControlIdBase + static_cast<int>(i);
        if (def.type == SettingType::Bool) {
          SettingsSetBool(def.key, IsDlgButtonChecked(hwnd, id) == BST_CHECKED);
        } else {
          wchar_t buf[512] = {};
          GetDlgItemTextW(hwnd, id, buf, ARRAYSIZE(buf));
          SettingsSetString(def.key, buf);
        }
      }
      AutostartSetEnabled(IsDlgButtonChecked(hwnd, kControlIdBase + 900) == BST_CHECKED);
      SettingsStoreSaveUser();
      LuxOverlay_OnSettingsChanged();
      EndDialog(hwnd, IDOK);
      return TRUE;
    }
    if (LOWORD(wParam) == IDCANCEL) {
      EndDialog(hwnd, IDCANCEL);
      return TRUE;
    }
    break;
  }
  return FALSE;
}
}  // namespace

void OpenSettingsDialog(HWND owner) {
  // Minimal dynamic dialog template (empty shell; controls created in WM_INITDIALOG).
#pragma pack(push, 2)
  struct {
    DLGTEMPLATE dlg;
    WORD menu;
    WORD windowClass;
    wchar_t title[1];
  } tpl = {};
#pragma pack(pop)
  tpl.dlg.style = DS_MODALFRAME | DS_CENTER | WS_POPUP | WS_CAPTION | WS_SYSMENU;
  tpl.dlg.cdit = 0;
  tpl.dlg.cx = 280;
  tpl.dlg.cy = 200;
  tpl.menu = 0;
  tpl.windowClass = 0;
  tpl.title[0] = 0;
  DialogBoxIndirectParamW(GetModuleHandleW(nullptr), &tpl.dlg, owner, SettingsProc, 0);
}
