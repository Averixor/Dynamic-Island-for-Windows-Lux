#pragma once

#include <atomic>
#include <cstdint>

// Process-wide flags shared by main / lux_overlay / island.cpp.
// Set from wWinMain before workers start; workers only write StuckOnExit.

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
inline std::uint32_t& LuxSmokeTimeoutMs() {
  static std::uint32_t value = 0;
  return value;
}

// Written from StopThreads wait path; read on main after Uninit.
inline std::atomic<bool>& LuxWorkersStuckOnExit() {
  static std::atomic<bool> value{false};
  return value;
}
