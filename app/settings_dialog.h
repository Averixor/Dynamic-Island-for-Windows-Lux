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
// Apply writes settings, reloads the island, and refreshes the baseline.
// Cancel restores the UI to the last baseline and closes (keeps applied disk).
// OK writes settings, updates the island, and closes.
// Reset to defaults asks for confirmation, writes schema defaults (+ clears drag
// offsets), refreshes the controls, and updates the island without closing.
// Enter in a field does not press OK. Escape closes without writing new edits.
void OpenSettingsDialog(HWND owner);
