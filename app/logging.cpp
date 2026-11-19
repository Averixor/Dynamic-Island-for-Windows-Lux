#include "logging.h"

#include <shlobj.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cwchar>
#include <mutex>
#include <string>

namespace {
std::mutex g_logMutex;
FILE* g_logFile = nullptr;
bool g_ephemeral = false;
std::atomic<int> g_minLevel{static_cast<int>(LuxLogLevel::Info)};
std::wstring g_logPath;
unsigned g_writesSinceSizeCheck = 0;

constexpr ULONGLONG kMaxLogBytes = 5ull * 1024ull * 1024ull;
constexpr unsigned kSizeCheckEveryN = 64;

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

void CloseLogFileUnlocked() {
  if (g_logFile) {
    fclose(g_logFile);
    g_logFile = nullptr;
  }
}

void RotateIfNeededUnlocked(const std::wstring& path) {
  WIN32_FILE_ATTRIBUTE_DATA fad = {};
  if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad)) {
    return;
  }
  ULARGE_INTEGER size = {};
  size.HighPart = fad.nFileSizeHigh;
  size.LowPart = fad.nFileSizeLow;
  if (size.QuadPart < kMaxLogBytes) {
    return;
  }
  const std::wstring bak2 = path + L".2";
  const std::wstring bak1 = path + L".1";
  DeleteFileW(bak2.c_str());
  MoveFileExW(bak1.c_str(), bak2.c_str(), MOVEFILE_REPLACE_EXISTING);
  CloseLogFileUnlocked();
  MoveFileExW(path.c_str(), bak1.c_str(), MOVEFILE_REPLACE_EXISTING);
}

bool OpenLogFileUnlocked(const std::wstring& path) {
  if (g_logFile) {
    return true;
  }
  g_logFile = _wfsopen(path.c_str(), L"a+, ccs=UTF-8", _SH_DENYWR);
  return g_logFile != nullptr;
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
  CloseLogFileUnlocked();
  g_ephemeral = ephemeral;
  g_logPath.clear();
  g_writesSinceSizeCheck = 0;
  // Release default = Info (no Debug spam). Override with --log-level=debug.
#ifdef _DEBUG
  g_minLevel.store(static_cast<int>(LuxLogLevel::Debug), std::memory_order_relaxed);
#else
  g_minLevel.store(static_cast<int>(LuxLogLevel::Info), std::memory_order_relaxed);
#endif
  if (g_ephemeral) {
    return;
  }
  const std::wstring dir = LogsDirectory();
  if (dir.empty()) {
    return;
  }
  EnsureDirectory(dir);
  g_logPath = dir + L"\\lux.log";
  RotateIfNeededUnlocked(g_logPath);
  OpenLogFileUnlocked(g_logPath);
}

void LuxLogShutdown() {
  std::lock_guard lock(g_logMutex);
  CloseLogFileUnlocked();
  g_logPath.clear();
  g_writesSinceSizeCheck = 0;
}

void LuxLogSetMinLevel(LuxLogLevel level) {
  g_minLevel.store(static_cast<int>(level), std::memory_order_relaxed);
}

LuxLogLevel LuxLogGetMinLevel() {
  return static_cast<LuxLogLevel>(g_minLevel.load(std::memory_order_relaxed));
}

void LuxLog(LuxLogLevel level, const wchar_t* fmt, ...) {
  if (!fmt) {
    return;
  }
  if (static_cast<int>(level) > g_minLevel.load(std::memory_order_relaxed)) {
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

#if defined(_DEBUG)
  OutputDebugStringW(line);
#else
  if (g_minLevel.load(std::memory_order_relaxed) >= static_cast<int>(LuxLogLevel::Debug)) {
    OutputDebugStringW(line);
  }
#endif

  std::lock_guard lock(g_logMutex);
  if (g_ephemeral || g_logPath.empty()) {
    return;
  }

  if (++g_writesSinceSizeCheck >= kSizeCheckEveryN) {
    g_writesSinceSizeCheck = 0;
    RotateIfNeededUnlocked(g_logPath);
  }
  if (!OpenLogFileUnlocked(g_logPath)) {
    return;
  }
  if (fputws(line, g_logFile) < 0) {
    CloseLogFileUnlocked();
    if (!OpenLogFileUnlocked(g_logPath) || fputws(line, g_logFile) < 0) {
      return;
    }
  }
  fflush(g_logFile);
}
