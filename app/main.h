#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <cstdint>

constexpr wchar_t kAppMutexName[] = L"Local\\DynamicIslandLux";
constexpr wchar_t kAppReadyEventName[] = L"Local\\DynamicIslandLux.Ready";
constexpr wchar_t kOverlayClassName[] = L"DynamicIslandLux.Overlay";
constexpr wchar_t kHostClassName[] = L"DynamicIslandLux.Host";

constexpr UINT32 kCopyDataProtocolVersion = 1;

enum class CopyDataCommand : UINT32 {
  OpenSettings = 1,
  ShowIsland = 2,
  Quit = 3,
};

#pragma pack(push, 1)
struct CopyDataPayload {
  UINT32 version;
  UINT32 cmd;
};
#pragma pack(pop)

struct AppOptions {
  bool smokeTest = false;
  bool testJson = false;
  bool noTray = false;
  bool noAutostart = false;
  bool ephemeral = false;
  bool openSettings = false;
  DWORD smokeTimeoutMs = 10000;  // used when smokeTest; override via --smoke-timeout-ms=
};

struct AppState {
  HINSTANCE instance = nullptr;
  HWND hostHwnd = nullptr;
  HWND overlayHwnd = nullptr;
  HANDLE singleInstanceMutex = nullptr;
  HANDLE readyEvent = nullptr;
  NOTIFYICONDATAW tray = {};
  bool trayRegistered = false;
  UINT taskbarCreatedMsg = 0;
  AppOptions options;
  volatile LONG renderedFrames = 0;
  volatile LONG lastFrameHr = S_OK;
  bool running = true;
};

AppState& GetApp();
AppOptions ParseCommandLine(int argc, wchar_t** argv);
bool AcquireSingleInstance(AppState& app);
void SignalAppReady(AppState& app);
bool NotifyRunningInstance(CopyDataCommand cmd);
void RegisterTrayIcon(AppState& app);
void UnregisterTrayIcon(AppState& app);
void ShowTrayMenu(AppState& app, POINT screenPt);

// Settings dialog (settings_dialog.cpp).
void OpenSettingsDialog(HWND owner);
