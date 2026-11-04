#include "main.h"

#include "autostart.h"
#include "logging.h"
#include "resource.h"
#include "app_context.h"
#include "lux_overlay.h"
#include "settings_dialog.h"
#include "settings_schema.h"
#include "settings_store.h"

#include <shellapi.h>

#include <string>
#include <vector>

namespace {
AppState g_app;

LRESULT CALLBACK HostWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  AppState& app = GetApp();
  if (msg == app.taskbarCreatedMsg && app.taskbarCreatedMsg != 0) {
    app.trayRegistered = false;
    if (!app.options.noTray && !app.options.smokeTest && !app.options.ephemeral) {
      RegisterTrayIcon(app);
    }
    return 0;
  }

  switch (msg) {
  case WM_APP_TRAY:
    if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
      POINT pt = {};
      GetCursorPos(&pt);
      ShowTrayMenu(app, pt);
    } else if (lParam == WM_LBUTTONDBLCLK) {
      OpenSettingsDialog(hwnd);
    }
    return 0;
  case WM_APP_OPEN_SETTINGS:
    OpenSettingsDialog(hwnd);
    return 0;
  case WM_APP_SHOW_ISLAND:
    if (app.overlayHwnd) {
      ShowWindow(app.overlayHwnd, SW_SHOWNOACTIVATE);
      SetWindowPos(app.overlayHwnd, HWND_TOPMOST, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    return 0;
  case WM_COPYDATA: {
    const COPYDATASTRUCT* cds = reinterpret_cast<COPYDATASTRUCT*>(lParam);
    if (!cds || cds->cbData != sizeof(CopyDataPayload) || !cds->lpData) {
      return FALSE;
    }
    const CopyDataPayload* payload = static_cast<const CopyDataPayload*>(cds->lpData);
    if (payload->version != kCopyDataProtocolVersion) {
      return FALSE;
    }
    switch (static_cast<CopyDataCommand>(payload->cmd)) {
    case CopyDataCommand::OpenSettings:
      PostMessageW(hwnd, WM_APP_OPEN_SETTINGS, 0, 0);
      break;
    case CopyDataCommand::ShowIsland:
      PostMessageW(hwnd, WM_APP_SHOW_ISLAND, 0, 0);
      break;
    case CopyDataCommand::Quit:
      PostMessageW(hwnd, WM_CLOSE, 0, 0);
      break;
    }
    return TRUE;
  }
  case WM_COMMAND:
    switch (LOWORD(wParam)) {
    case IDM_TRAY_SETTINGS:
      OpenSettingsDialog(hwnd);
      break;
    case IDM_TRAY_SHOW:
      PostMessageW(hwnd, WM_APP_SHOW_ISLAND, 0, 0);
      break;
    case IDM_TRAY_EXIT:
      PostMessageW(hwnd, WM_CLOSE, 0, 0);
      break;
    }
    return 0;
  case WM_DESTROY:
    app.running = false;
    PostQuitMessage(0);
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}
}  // namespace

AppState& GetApp() {
  return g_app;
}

AppOptions ParseCommandLine(int argc, wchar_t** argv) {
  AppOptions opt;
  for (int i = 1; i < argc; ++i) {
    if (_wcsicmp(argv[i], L"--smoke-test") == 0) {
      opt.smokeTest = true;
      opt.ephemeral = true;
      opt.noTray = true;
      opt.noAutostart = true;
    } else if (_wcsicmp(argv[i], L"--test-json") == 0) {
      opt.testJson = true;
      opt.ephemeral = true;
      opt.noTray = true;
      opt.noAutostart = true;
    } else if (_wcsicmp(argv[i], L"--no-tray") == 0) {
      opt.noTray = true;
    } else if (_wcsicmp(argv[i], L"--no-autostart") == 0) {
      opt.noAutostart = true;
    } else if (_wcsicmp(argv[i], L"--no-workers") == 0) {
      LuxNoWorkersFlag() = true;
    } else if (_wcsicmp(argv[i], L"--ephemeral") == 0) {
      opt.ephemeral = true;
      opt.noTray = true;
      opt.noAutostart = true;
    } else if (_wcsicmp(argv[i], L"--settings") == 0) {
      opt.openSettings = true;
    } else if (_wcsnicmp(argv[i], L"--smoke-timeout-ms=", 19) == 0) {
      const int ms = _wtoi(argv[i] + 19);
      if (ms > 0) {
        opt.smokeTimeoutMs = static_cast<DWORD>(ms);
      }
    } else if (_wcsnicmp(argv[i], L"--backdrop=", 11) == 0) {
      wcsncpy_s(opt.smokeBackdrop, argv[i] + 11, _TRUNCATE);
    }
  }
  return opt;
}

bool AcquireSingleInstance(AppState& app) {
  SetLastError(0);
  app.singleInstanceMutex = CreateMutexW(nullptr, TRUE, kAppMutexName);
  if (!app.singleInstanceMutex) {
    return false;
  }
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    return false;
  }
  app.readyEvent = CreateEventW(nullptr, TRUE, FALSE, kAppReadyEventName);
  return true;
}

void SignalAppReady(AppState& app) {
  if (app.readyEvent) {
    SetEvent(app.readyEvent);
  }
}

bool NotifyRunningInstance(CopyDataCommand cmd) {
  // Bounded wait — double-click on shortcut must never hang forever.
  constexpr DWORD kReadyWaitMs = 1500;
  constexpr int kPollAttempts = 15;
  constexpr DWORD kPollSleepMs = 100;

  HANDLE ready = OpenEventW(SYNCHRONIZE, FALSE, kAppReadyEventName);
  if (ready) {
    WaitForSingleObject(ready, kReadyWaitMs);
    CloseHandle(ready);
  } else {
    for (int i = 0; i < kPollAttempts; ++i) {
      Sleep(kPollSleepMs);
      ready = OpenEventW(SYNCHRONIZE, FALSE, kAppReadyEventName);
      if (ready) {
        WaitForSingleObject(ready, 300);
        CloseHandle(ready);
        break;
      }
    }
  }

  HWND hwnd = nullptr;
  for (int i = 0; i < kPollAttempts; ++i) {
    hwnd = FindWindowW(kHostClassName, nullptr);
    if (hwnd) {
      break;
    }
    Sleep(kPollSleepMs);
  }
  if (!hwnd) {
    return false;
  }
  CopyDataPayload payload = {};
  payload.version = kCopyDataProtocolVersion;
  payload.cmd = static_cast<UINT32>(cmd);
  COPYDATASTRUCT cds = {};
  cds.dwData = 1;
  cds.cbData = sizeof(payload);
  cds.lpData = &payload;
  // Short timeout so a hung primary instance cannot freeze the second process.
  SendMessageTimeoutW(hwnd, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&cds), SMTO_ABORTIFHUNG,
                      2000, nullptr);
  return true;
}

void RegisterTrayIcon(AppState& app) {
  if (app.trayRegistered) {
    return;
  }
  ZeroMemory(&app.tray, sizeof(app.tray));
  app.tray.cbSize = sizeof(app.tray);
  app.tray.hWnd = app.hostHwnd;
  app.tray.uID = 1;
  app.tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
  app.tray.uCallbackMessage = WM_APP_TRAY;
  app.tray.hIcon = LoadIconW(app.instance, MAKEINTRESOURCEW(IDI_APP));
  if (!app.tray.hIcon) {
    app.tray.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
  }
  wcsncpy_s(app.tray.szTip, L"Dynamic Island for Windows Lux", _TRUNCATE);
  if (Shell_NotifyIconW(NIM_ADD, &app.tray)) {
    app.tray.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &app.tray);
    app.trayRegistered = true;
  }
}

void UnregisterTrayIcon(AppState& app) {
  if (!app.trayRegistered) {
    return;
  }
  Shell_NotifyIconW(NIM_DELETE, &app.tray);
  app.trayRegistered = false;
}

void ShowTrayMenu(AppState& app, POINT screenPt) {
  HMENU menu = CreatePopupMenu();
  AppendMenuW(menu, MF_STRING, IDM_TRAY_SHOW, L"Show island");
  AppendMenuW(menu, MF_STRING, IDM_TRAY_SETTINGS, L"Settings...");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, IDM_TRAY_EXIT, L"Exit");
  SetForegroundWindow(app.hostHwnd);
  TrackPopupMenuEx(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN, screenPt.x, screenPt.y,
                   app.hostHwnd, nullptr);
  DestroyMenu(menu);
  PostMessageW(app.hostHwnd, WM_NULL, 0, 0);
}

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
  // Startup order (documented contract):
  //   1) mutex (single instance)
  //   2) host HWND (WM_COPYDATA handler lives here)
  //   3) LuxOverlay_Init → overlay HWND + D2D (no workers yet)
  //   4) SignalAppReady  (second instance may open settings now)
  //   5) LuxOverlay_StartWorkers
  //   6) first frame / message loop
  int argc = 0;
  wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  AppOptions options = ParseCommandLine(argc, argv ? argv : nullptr);
  if (argv) {
    LocalFree(argv);
  }

  g_app.instance = instance;
  g_app.options = options;
  // Smoke does NOT imply --no-workers.
  // CI: `--smoke-test --no-workers`. Local: `--smoke-test` alone starts workers after frame.
  if (options.smokeTest) {
    LuxSmokeTestFlag() = true;
    LuxSmokeTimeoutMs() = options.smokeTimeoutMs;
  }

  INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES};
  InitCommonControlsEx(&icc);

  if (!AcquireSingleInstance(g_app)) {
    const CopyDataCommand cmd =
        options.openSettings ? CopyDataCommand::OpenSettings : CopyDataCommand::ShowIsland;
    NotifyRunningInstance(cmd);
    if (g_app.singleInstanceMutex) {
      CloseHandle(g_app.singleInstanceMutex);
    }
    return 0;
  }

  const bool ephemeral = options.ephemeral || options.smokeTest || options.testJson;
  LuxLogInit(ephemeral);
  SettingsStoreInit();
  LUX_LOGI(L"Dynamic Island Lux %s starting", LUX_APP_VERSION_STRING);

  if (options.testJson) {
    const bool ok = SettingsStoreSelfTestJson();
    SettingsStoreShutdown();
    LuxLogShutdown();
    if (g_app.readyEvent) {
      CloseHandle(g_app.readyEvent);
    }
    if (g_app.singleInstanceMutex) {
      CloseHandle(g_app.singleInstanceMutex);
    }
    return ok ? static_cast<int>(LuxExitCode::Ok)
              : static_cast<int>(LuxExitCode::JsonTestFailure);
  }

  g_app.taskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarCreated");

  WNDCLASSEXW hostWc = {};
  hostWc.cbSize = sizeof(hostWc);
  hostWc.lpfnWndProc = HostWndProc;
  hostWc.hInstance = instance;
  hostWc.lpszClassName = kHostClassName;
  RegisterClassExW(&hostWc);

  g_app.hostHwnd =
      CreateWindowExW(0, kHostClassName, L"DynamicIslandLuxHost", WS_OVERLAPPED, 0, 0, 0, 0,
                      HWND_MESSAGE, nullptr, instance, nullptr);
  if (!g_app.hostHwnd) {
    LUX_LOGE(L"Host window failed");
    return static_cast<int>(LuxExitCode::InitFailure);
  }

  if (!LuxOverlay_Init(g_app)) {
    LUX_LOGE(L"LuxOverlay_Init failed");
    return static_cast<int>(LuxExitCode::InitFailure);
  }

  if (!options.noTray && !ephemeral) {
    RegisterTrayIcon(g_app);
  }

  // Ready AFTER host HWND (+ scaffold if any) exist; BEFORE workers.
  // Settings dialog reads settings.json only — safe while workers init.
  SignalAppReady(g_app);

  if (options.openSettings && !options.smokeTest) {
    PostMessageW(g_app.hostHwnd, WM_APP_OPEN_SETTINGS, 0, 0);
  }

  if (options.smokeTest) {
    // Frame criterion uses scaffold only — does not wait on media/network.
    for (int i = 0; i < 30; ++i) {
      MSG msg = {};
      while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
      }
      if (LuxOverlay_PumpFrame(g_app)) {
        break;
      }
      Sleep(16);
    }
    const bool frameOk = InterlockedCompareExchange(&g_app.renderedFrames, 0, 0) > 0 &&
                         SUCCEEDED(static_cast<HRESULT>(
                             InterlockedCompareExchange(&g_app.lastFrameHr, 0, 0)));
    LUX_LOGI(L"smoke-test frame %s (frames=%ld hr=0x%08lX)", frameOk ? L"PASS" : L"FAIL",
             InterlockedCompareExchange(&g_app.renderedFrames, 0, 0),
             static_cast<unsigned long>(
                 InterlockedCompareExchange(&g_app.lastFrameHr, 0, 0)));

    bool initOk = true;
    // Optional: start workers after frame proof, then require clean Uninit.
    // Criterion stays scaffold frame only — never waits on media/weather/network.
    if (frameOk && LuxShouldStartWorkers()) {
      // Default smoke forces acrylic; override with --backdrop=blur|none|acrylic.
      const wchar_t* backdrop = options.smokeBackdrop[0] ? options.smokeBackdrop : L"acrylic";
      SettingsSetString(L"Themes.BackdropMaterial", backdrop);
      LUX_LOGI(L"smoke-test: starting workers after frame proof (backdrop=%s)", backdrop);
      if (!LuxOverlay_StartWorkers(g_app)) {
        LUX_LOGE(L"smoke-test: WhTool_ModInit failed");
        initOk = false;
      }
    }

    UnregisterTrayIcon(g_app);
    LuxOverlay_Uninit(g_app);  // WhTool_ModUninit → wait threads → destroy scaffold
    const bool workersStuck = LuxWorkersStuckOnExit();
    if (workersStuck) {
      LUX_LOGE(L"smoke-test: worker(s) stuck on Uninit (see worker … did not stop)");
    }
    SettingsStoreShutdown();
    LuxLogShutdown();
    if (g_app.readyEvent) {
      CloseHandle(g_app.readyEvent);
    }
    if (g_app.singleInstanceMutex) {
      CloseHandle(g_app.singleInstanceMutex);
    }
    if (!initOk) {
      return static_cast<int>(LuxExitCode::InitFailure);
    }
    if (!frameOk) {
      return static_cast<int>(LuxExitCode::FrameFailure);
    }
    if (workersStuck) {
      return static_cast<int>(LuxExitCode::WorkersStuck);
    }
    return static_cast<int>(LuxExitCode::Ok);
  }

  if (!LuxOverlay_StartWorkers(g_app)) {
    LUX_LOGE(L"StartWorkers failed");
    LuxOverlay_Uninit(g_app);
    return static_cast<int>(LuxExitCode::InitFailure);
  }

  MSG msg = {};
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }

  UnregisterTrayIcon(g_app);
  LuxOverlay_Uninit(g_app);
  SettingsStoreShutdown();
  LuxLogShutdown();
  if (g_app.readyEvent) {
    CloseHandle(g_app.readyEvent);
  }
  if (g_app.singleInstanceMutex) {
    CloseHandle(g_app.singleInstanceMutex);
  }
  return static_cast<int>(msg.wParam);
}
