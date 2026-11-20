#pragma once

#include <windows.h>

// Schema-driven settings dialog. A category sidebar plus a global search box
// replaces one long list. Colors, caption chrome, and controls follow the
// island theme (presets, custom hex, accent mode, adaptive). Changing theme
// controls repaints the dialog immediately; Apply/OK persist, Cancel restores
// the last baseline and does not write those edits.
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
