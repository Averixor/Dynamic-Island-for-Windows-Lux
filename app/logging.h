#pragma once

#include <windows.h>

enum class LuxLogLevel {
  Error = 0,
  Warn = 1,
  Info = 2,
  Debug = 3,
};

void LuxLogInit(bool ephemeral);
void LuxLogShutdown();
void LuxLog(LuxLogLevel level, const wchar_t* fmt, ...);

#define LUX_LOGE(...) LuxLog(LuxLogLevel::Error, __VA_ARGS__)
#define LUX_LOGW(...) LuxLog(LuxLogLevel::Warn, __VA_ARGS__)
#define LUX_LOGI(...) LuxLog(LuxLogLevel::Info, __VA_ARGS__)
#define LUX_LOGD(...) LuxLog(LuxLogLevel::Debug, __VA_ARGS__)
