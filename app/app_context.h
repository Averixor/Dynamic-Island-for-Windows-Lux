#pragma once

#include <cstdint>

// Shared process flags for lux_overlay / legacy .wh.cpp body.
// Prefer SettingsStore "__smoke" only if many sites need it later; keep one header for now.

inline bool& LuxSmokeTestFlag() {
  static bool value = false;
  return value;
}

inline bool& LuxNoWorkersFlag() {
  static bool value = false;
  return value;
}

// Only --no-workers suppresses threads/hooks.
// --smoke-test alone MAY start workers (frame proof still uses scaffold first).
inline bool LuxShouldStartWorkers() {
  return !LuxNoWorkersFlag();
}

// 0 = wait forever (normal run). Smoke default is set from main when --smoke-test is parsed.
inline DWORD& LuxSmokeTimeoutMs() {
  static DWORD value = 0;
  return value;
}

inline bool& LuxWorkersStuckOnExit() {
  static bool value = false;
  return value;
}
