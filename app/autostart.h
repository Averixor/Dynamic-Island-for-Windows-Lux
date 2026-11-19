#pragma once

// HKCU\...\Run value "DynamicIslandLux" + StartupApproved\Run sync (Task Manager toggle).
// Command line: "exe" --minimized (tray-only until LMB).

bool AutostartIsEnabled();
bool AutostartSetEnabled(bool enabled);
