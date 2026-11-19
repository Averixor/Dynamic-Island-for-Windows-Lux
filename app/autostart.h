#pragma once

// HKCU\...\Run value "DynamicIslandLux" + StartupApproved\Run sync (Task Manager toggle).
// Command line: "exe" --minimized (tray-only until LMB).

enum class AutostartState {
  Off = 0,             // no Run value
  On = 1,              // Run value points at this exe and StartupApproved is enabled
  DisabledByUser = 2,  // Run value exists but Task Manager disabled it
  StalePath = 3,       // Run value exists but does not point at this exe
};

AutostartState AutostartQuery();
bool AutostartIsEnabled();
bool AutostartSetEnabled(bool enabled);
