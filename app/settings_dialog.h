#pragma once

#include <windows.h>

// Architecture (Step D — locked):
// Custom single-dialog UI driven by kSettingSchema — NOT Win32 PROPSHEETHEADER.
// Reasons: seed keys will grow past ~100; we need a search box, group headers,
// and non-standard controls later. Property sheets have no search and fight
// custom DPI/layout. Keep one schema-driven dialog + filter.
//
// Modal DialogBox on the *main* (host) thread — OK because the island renders on
// RenderThreadProc. DialogBox runs its own IsDialogMessage loop (Tab/Enter work).
// No Apply button: OK saves+applies; Cancel discards without calling OnSettingsChanged.
void OpenSettingsDialog(HWND owner);
