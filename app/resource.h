#pragma once

// Host-process resource / command IDs (tray + message-only HWND).
// Island render thread uses its own WM_APP + 0x442… range in the island.cpp body body —
// keep these below WM_APP + 0x100 to avoid collisions.

#define IDI_APP 101

// STRINGTABLE fallbacks (English). Live tray/UI strings come from SettingsUiLoc.
#define IDS_APP_TITLE 201
#define IDS_TRAY_TIP 202
#define IDS_MENU_SETTINGS 203
#define IDS_MENU_SHOW 204
#define IDS_MENU_EXIT 205
#define IDS_MENU_RELOAD 206

#define IDM_TRAY_SETTINGS 1001
#define IDM_TRAY_SHOW 1002
#define IDM_TRAY_EXIT 1003
#define IDM_TRAY_RELOAD 1004

#define WM_APP_TRAY (WM_APP + 1)
#define WM_APP_OPEN_SETTINGS (WM_APP + 2)
#define WM_APP_SHOW_ISLAND (WM_APP + 3)
#define WM_APP_TOGGLE_ISLAND (WM_APP + 4)
#define WM_APP_RELOAD (WM_APP + 5)
