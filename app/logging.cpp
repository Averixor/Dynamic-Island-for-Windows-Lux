#include "logging.h"

#include <shlobj.h>

#include <cstdarg>
#include <cstdio>
#include <cwchar>
#include <mutex>
#include <string>

namespace {
std::mutex g_logMutex;
FILE* g_logFile = nullptr;
bool g_ephemeral = false;
LuxLogLevel g_minLevel = LuxLogLevel::Info;

std::wstring LogsDirectory() {
  wchar_t base[MAX_PATH] = {};
  if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, base))) {
    return {};
  }
  std::wstring dir = base;
  dir += L"\\DynamicIslandLux\\logs";
  return dir;
}

void EnsureDirectory(const std::wstring& dir) {
  SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
}

void RotateIfNeeded(const std::wstring& path) {
  WIN32_FILE_ATTRIBUTE_DATA fad = {};
  if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad)) {
    return;
  }
  ULARGE_INTEGER size = {};
  size.HighPart = fad.nFileSizeHigh;
  size.LowPart = fad.nFileSizeLow;
  constexpr ULONGLONG kMaxBytes = 5ull * 1024ull * 1024ull;
  if (size.QuadPart < kMaxBytes) {
    return;
  }
  const std::wstring bak2 = path + L".2";
  const std::wstring bak1 = path + L".1";
  DeleteFileW(bak2.c_str());
  MoveFileExW(bak1.c_str(), bak2.c_str(), MOVEFILE_REPLACE_EXISTING);
  if (g_logFile) {
    fclose(g_logFile);
    g_logFile = nullptr;
  }
  MoveFileExW(path.c_str(), bak1.c_str(), MOVEFILE_REPLACE_EXISTING);
}

const wchar_t* LevelName(LuxLogLevel level) {
  switch (level) {
  case LuxLogLevel::Error:
    return L"ERR";
  case LuxLogLevel::Warn:
    return L"WRN";
  case LuxLogLevel::Info:
    return L"INF";
  case LuxLogLevel::Debug:
    return L"DBG";
  }
  return L"INF";
}
}  // namespace

void LuxLogInit(bool ephemeral) {
  std::lock_guard lock(g_logMutex);
  g_ephemeral = ephemeral;
  // Release default = Info (no Debug spam). Override with --log-level=debug.
#ifdef _DEBUG
  g_minLevel = LuxLogLevel::Debug;
#else
  g_minLevel = LuxLogLevel::Info;
#endif
  if (g_ephemeral) {
    return;
  }
  const std::wstring dir = LogsDirectory();
  if (dir.empty()) {
    return;
  }
  EnsureDirectory(dir);
  const std::wstring path = dir + L"\\lux.log";
  RotateIfNeeded(path);
  g_logFile = _wfsopen(path.c_str(), L"a+, ccs=UTF-8", _SH_DENYWR);
}

void LuxLogShutdown() {
  std::lock_guard lock(g_logMutex);
  if (g_logFile) {
    fclose(g_logFile);
    g_logFile = nullptr;
  }
}

void LuxLogSetMinLevel(LuxLogLevel level) {
  std::lock_guard lock(g_logMutex);
  g_minLevel = level;
}

LuxLogLevel LuxLogGetMinLevel() {
  return g_minLevel;
}

void LuxLog(LuxLogLevel level, const wchar_t* fmt, ...) {
  if (static_cast<int>(level) > static_cast<int>(g_minLevel) || !fmt) {
    return;
  }
  wchar_t body[2048] = {};
  va_list args;
  va_start(args, fmt);
  _vsnwprintf_s(body, _TRUNCATE, fmt, args);
  va_end(args);

  SYSTEMTIME st = {};
  GetLocalTime(&st);
  wchar_t line[2300] = {};
  swprintf_s(line, L"%04u-%02u-%02u %02u:%02u:%02u.%03u [%s] %s\r\n", st.wYear, st.wMonth,
             st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, LevelName(level), body);

#ifdef _DEBUG
  OutputDebugStringW(line);
#endif

  if (g_ephemeral) {
    return;
  }
  std::lock_guard lock(g_logMutex);
  const std::wstring dir = LogsDirectory();
  if (dir.empty()) {
    return;
  }
  const std::wstring path = dir + L"\\lux.log";
  // Rotate under the same mutex as writers so concurrent threads never race the file.
  RotateIfNeeded(path);
  if (!g_logFile) {
    g_logFile = _wfsopen(path.c_str(), L"a+, ccs=UTF-8", _SH_DENYWR);
  }
  if (!g_logFile) {
    return;
  }
  fputws(line, g_logFile);
  fflush(g_logFile);
}
