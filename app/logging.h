#pragma once

#include <windows.h>

// File sink: %LocalAppData%\DynamicIslandLux\logs\lux.log (rotated at 5 MB → .1 / .2).
// Release default min level = Info; Debug builds default to Debug.
// Override: --log-level=error|warn|info|debug
// Ephemeral (smoke / --test-json): no file I/O.

enum class LuxLogLevel {
  Error = 0,
  Warn = 1,
  Info = 2,
  Debug = 3,
};

void LuxLogInit(bool ephemeral);
void LuxLogShutdown();
void LuxLogSetMinLevel(LuxLogLevel level);
LuxLogLevel LuxLogGetMinLevel();
void LuxLog(LuxLogLevel level, const wchar_t* fmt, ...);

#define LUX_LOGE(...) LuxLog(LuxLogLevel::Error, __VA_ARGS__)
#define LUX_LOGW(...) LuxLog(LuxLogLevel::Warn, __VA_ARGS__)
#define LUX_LOGI(...) LuxLog(LuxLogLevel::Info, __VA_ARGS__)
#define LUX_LOGD(...) LuxLog(LuxLogLevel::Debug, __VA_ARGS__)
