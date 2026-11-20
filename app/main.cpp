#include "main.h"

#include "autostart.h"
#include "logging.h"
#include "resource.h"
#include "app_context.h"
#include "lux_overlay.h"
#include "menu_theme.h"
#include "settings_dialog.h"
#include "settings_schema.h"
#include "settings_store.h"
#include "media_like_bridge.h"
#include "settings_ui_loc.h"

#include <shellapi.h>
#include <sddl.h>

#include <string>
#include <vector>

#ifndef NIN_SELECT
#define NIN_SELECT (WM_USER + 0)
#endif
#ifndef NIN_KEYSELECT
#define NIN_KEYSELECT (WM_USER + 1)
#endif

namespace {
AppState g_app;
std::wstring g_mutexName = L"Local\\DynamicIslandLux";
std::wstring g_readyEventName = L"Local\\DynamicIslandLux.Ready";

std::wstring CurrentUserSid() {
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
    return {};
  }
  DWORD length = 0;
  GetTokenInformation(token, TokenUser, nullptr, 0, &length);
  std::vector<BYTE> buffer(length);
  if (length == 0 ||
      !GetTokenInformation(token, TokenUser, buffer.data(), length, &length)) {
    CloseHandle(token);
    return {};
  }
  CloseHandle(token);
  LPWSTR sid = nullptr;
  auto* user = reinterpret_cast<TOKEN_USER*>(buffer.data());
  if (!ConvertSidToStringSidW(user->User.Sid, &sid) || !sid) {
    return {};
  }
  std::wstring result = sid;
  LocalFree(sid);
  return result;
}

void InitInstanceObjectNames() {
  const std::wstring sid = CurrentUserSid();
  if (sid.empty()) {
    g_mutexName = kAppMutexName;
    g_readyEventName = kAppReadyEventName;
    return;
  }
  g_mutexName = L"Local\\DynamicIslandLux-" + sid;
  g_readyEventName = L"Local\\DynamicIslandLux.Ready-" + sid;
}

bool SecurityForCurrentUser(PSECURITY_DESCRIPTOR* descriptor) {
  *descriptor = nullptr;
  const std::wstring sid = CurrentUserSid();
  if (sid.empty()) {
    return false;
  }
  const std::wstring sddl = L"D:P(A;;GA;;;" + sid + L")";
  return ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1,
                                                              descriptor, nullptr) == TRUE;
}

bool IsTrustedCopyDataSender(HWND sender) {
  if (!IsWindow(sender)) {
    return false;
  }
  DWORD pid = 0;
  GetWindowThreadProcessId(sender, &pid);
  if (pid == 0) {
    return false;
  }
  if (pid == GetCurrentProcessId()) {
    return true;
  }
  HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!process) {
    return false;
  }
  std::wstring senderPath(1024, L'\0');
  for (;;) {
    DWORD size = static_cast<DWORD>(senderPath.size());
    if (QueryFullProcessImageNameW(process, 0, senderPath.data(), &size)) {
      senderPath.resize(size);
      break;
    }
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || senderPath.size() > 32768) {
      CloseHandle(process);
      return false;
    }
    senderPath.resize(senderPath.size() * 2);
  }
  CloseHandle(process);
  std::wstring selfPath(1024, L'\0');
  for (;;) {
    const DWORD n =
        GetModuleFileNameW(nullptr, selfPath.data(), static_cast<DWORD>(selfPath.size()));
    if (n == 0) {
      return false;
    }
    if (n < selfPath.size()) {
      selfPath.resize(n);
      break;
    }
    selfPath.resize(selfPath.size() * 2);
  }
  return _wcsicmp(senderPath.c_str(), selfPath.c_str()) == 0;
}

void BringSettingsForward(HWND settings) {
  if (!IsWindow(settings)) {
    return;
  }
  ShowWindow(settings, SW_SHOW);
  SetForegroundWindow(settings);
  BringWindowToTop(settings);
}

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
  case WM_APP_TRAY: {
    if (IsWindow(app.settingsHwnd)) {
      BringSettingsForward(app.settingsHwnd);
      return 0;
    }
    // NOTIFYICON_VERSION_4: event is LOWORD(lParam). Also accept legacy full lParam.
    const UINT trayEvent = LOWORD(lParam);
    if (trayEvent == WM_RBUTTONUP || trayEvent == WM_CONTEXTMENU || lParam == WM_RBUTTONUP ||
        lParam == WM_CONTEXTMENU) {
      POINT pt = {};
      GetCursorPos(&pt);
      ShowTrayMenu(app, pt);
    } else if (trayEvent == NIN_SELECT || trayEvent == NIN_KEYSELECT || trayEvent == WM_LBUTTONUP ||
               lParam == WM_LBUTTONUP || lParam == WM_LBUTTONDBLCLK) {
      // Left click: collapse / expand the island widget.
      LuxOverlay_ToggleIsland();
    }
    return 0;
  }
  case WM_APP_OPEN_SETTINGS:
    if (IsWindow(app.settingsHwnd)) {
      SetForegroundWindow(app.settingsHwnd);
    } else {
      OpenSettingsDialog(hwnd);
    }
    return 0;
  case WM_APP_SHOW_ISLAND:
    LuxOverlay_ShowIsland();
    return 0;
  case WM_APP_TOGGLE_ISLAND:
    LuxOverlay_ToggleIsland();
    return 0;
  case WM_APP_RELOAD:
    if (IsWindow(app.settingsHwnd)) {
      BringSettingsForward(app.settingsHwnd);
      return 0;
    }
    LuxOverlay_Reload();
    return 0;
  case WM_COPYDATA: {
    if (!IsTrustedCopyDataSender(reinterpret_cast<HWND>(wParam))) {
      LUX_LOGW(L"WM_COPYDATA rejected: sender is not this executable");
      return FALSE;
    }
    const COPYDATASTRUCT* cds = reinterpret_cast<COPYDATASTRUCT*>(lParam);
    if (!cds || cds->dwData != 1 || cds->cbData != sizeof(CopyDataPayload) || !cds->lpData) {
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
    default:
      return FALSE;
    }
    return TRUE;
  }
  case WM_COMMAND:
    switch (LOWORD(wParam)) {
    case IDM_TRAY_SETTINGS:
      PostMessageW(hwnd, WM_APP_OPEN_SETTINGS, 0, 0);
      break;
    case IDM_TRAY_SHOW:
      PostMessageW(hwnd, WM_APP_TOGGLE_ISLAND, 0, 0);
      break;
    case IDM_TRAY_RELOAD:
      PostMessageW(hwnd, WM_APP_RELOAD, 0, 0);
      break;
    case IDM_TRAY_EXIT:
      PostMessageW(hwnd, WM_CLOSE, 0, 0);
      break;
    }
    return 0;
  case WM_QUERYENDSESSION:
    return TRUE;
  case WM_ENDSESSION:
    if (wParam) {
      if (IsWindow(app.settingsHwnd)) {
        EndDialog(app.settingsHwnd, IDCANCEL);
      }
      SettingsStoreShutdown();
    }
    return 0;
  case WM_CLOSE:
    if (IsWindow(app.settingsHwnd)) {
      EndDialog(app.settingsHwnd, IDCANCEL);
      PostMessageW(hwnd, WM_CLOSE, 0, 0);
      return 0;
    }
    DestroyWindow(hwnd);
    return 0;
  case WM_DESTROY:
    UnregisterTrayIcon(app);
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
  bool sawNoTray = false;
  bool sawMinimized = false;
  bool forceNoTray = false;
  for (int i = 1; i < argc; ++i) {
    if (_wcsicmp(argv[i], L"--smoke-test") == 0) {
      opt.smokeTest = true;
      opt.ephemeral = true;
      forceNoTray = true;
      opt.noAutostart = true;
    } else if (_wcsicmp(argv[i], L"--test-json") == 0) {
      opt.testJson = true;
      opt.ephemeral = true;
      forceNoTray = true;
      opt.noAutostart = true;
    } else if (_wcsicmp(argv[i], L"--no-tray") == 0) {
      sawNoTray = true;
    } else if (_wcsicmp(argv[i], L"--no-autostart") == 0) {
      opt.noAutostart = true;
    } else if (_wcsicmp(argv[i], L"--minimized") == 0 || _wcsicmp(argv[i], L"--startup") == 0) {
      sawMinimized = true;
    } else if (_wcsicmp(argv[i], L"--quit") == 0) {
      opt.quit = true;
    } else if (_wcsicmp(argv[i], L"--enable-autostart") == 0) {
      opt.enableAutostart = true;
    } else if (_wcsicmp(argv[i], L"--no-workers") == 0) {
      LuxNoWorkersFlag() = true;
    } else if (_wcsicmp(argv[i], L"--ephemeral") == 0) {
      opt.ephemeral = true;
      forceNoTray = true;
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
    } else if (_wcsnicmp(argv[i], L"--log-level=", 12) == 0) {
      wcsncpy_s(opt.logLevel, argv[i] + 12, _TRUNCATE);
    }
  }
  // Flags are order-independent. Hidden + no tray has no way to exit, so keep the island visible.
  if (sawMinimized) {
    opt.fromStartup = true;
    opt.startMinimized = true;
  }
  opt.noTray = forceNoTray || sawNoTray;
  if (opt.noTray && opt.startMinimized && !opt.ephemeral && !opt.smokeTest && !opt.testJson) {
    opt.startMinimized = false;
    opt.fromStartup = false;
  }
  return opt;
}

bool AcquireSingleInstance(AppState& app) {
  SetLastError(0);
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  SECURITY_ATTRIBUTES security = {};
  SECURITY_ATTRIBUTES* securityPtr = nullptr;
  if (SecurityForCurrentUser(&descriptor)) {
    security.nLength = sizeof(security);
    security.lpSecurityDescriptor = descriptor;
    security.bInheritHandle = FALSE;
    securityPtr = &security;
  }
  app.singleInstanceMutex = CreateMutexW(securityPtr, TRUE, g_mutexName.c_str());
  if (descriptor) {
    LocalFree(descriptor);
  }
  if (!app.singleInstanceMutex) {
    return false;
  }
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    CloseHandle(app.singleInstanceMutex);
    app.singleInstanceMutex = nullptr;
    return false;
  }
  PSECURITY_DESCRIPTOR eventDescriptor = nullptr;
  SECURITY_ATTRIBUTES eventSecurity = {};
  SECURITY_ATTRIBUTES* eventSecurityPtr = nullptr;
  if (SecurityForCurrentUser(&eventDescriptor)) {
    eventSecurity.nLength = sizeof(eventSecurity);
    eventSecurity.lpSecurityDescriptor = eventDescriptor;
    eventSecurity.bInheritHandle = FALSE;
    eventSecurityPtr = &eventSecurity;
  }
  app.readyEvent = CreateEventW(eventSecurityPtr, TRUE, FALSE, g_readyEventName.c_str());
  if (eventDescriptor) {
    LocalFree(eventDescriptor);
  }
  if (!app.readyEvent) {
    CloseHandle(app.singleInstanceMutex);
    app.singleInstanceMutex = nullptr;
    return false;
  }
  return true;
}

void SignalAppReady(AppState& app) {
  if (app.readyEvent) {
    SetEvent(app.readyEvent);
  }
}

bool NotifyRunningInstance(CopyDataCommand cmd) {
  // One budget for ready-event, window lookup, and COPYDATA retries.
  constexpr ULONGLONG kBudgetMs = 3000;
  const ULONGLONG deadline = GetTickCount64() + kBudgetMs;
  auto remaining = [&]() -> DWORD {
    const ULONGLONG now = GetTickCount64();
    if (now >= deadline) {
      return 0;
    }
    return static_cast<DWORD>(deadline - now);
  };
  auto sleepSlice = [&](DWORD cap) {
    const DWORD left = remaining();
    if (left == 0) {
      return;
    }
    Sleep(left < cap ? left : cap);
  };

  HANDLE ready = OpenEventW(SYNCHRONIZE, FALSE, g_readyEventName.c_str());
  if (ready) {
    WaitForSingleObject(ready, remaining());
    CloseHandle(ready);
  } else {
    while (remaining() > 0) {
      sleepSlice(100);
      ready = OpenEventW(SYNCHRONIZE, FALSE, g_readyEventName.c_str());
      if (ready) {
        WaitForSingleObject(ready, remaining());
        CloseHandle(ready);
        break;
      }
    }
  }

  HWND hwnd = nullptr;
  while (remaining() > 0 && !hwnd) {
    hwnd = FindWindowW(kHostClassName, nullptr);
    if (!hwnd) {
      hwnd = FindWindowExW(HWND_MESSAGE, nullptr, kHostClassName, nullptr);
    }
    if (!hwnd) {
      sleepSlice(100);
    }
  }
  if (!hwnd) {
    return false;
  }

  // A real HWND is the COPYDATA sender so the primary can check the image path.
  HWND sender = CreateWindowExW(0, L"Static", L"DynamicIslandLux.Ipc", 0, 0, 0, 0, 0, HWND_MESSAGE,
                                nullptr, nullptr, nullptr);
  if (!sender) {
    return false;
  }
  CopyDataPayload payload = {};
  payload.version = kCopyDataProtocolVersion;
  payload.cmd = static_cast<UINT32>(cmd);
  COPYDATASTRUCT cds = {};
  cds.dwData = 1;
  cds.cbData = sizeof(payload);
  cds.lpData = &payload;
  bool delivered = false;
  while (remaining() > 0) {
    const DWORD slice = remaining() < 400 ? remaining() : 400;
    if (slice == 0) {
      break;
    }
    DWORD_PTR sendResult = 0;
    const LRESULT ok =
        SendMessageTimeoutW(hwnd, WM_COPYDATA, reinterpret_cast<WPARAM>(sender),
                            reinterpret_cast<LPARAM>(&cds), SMTO_ABORTIFHUNG, slice, &sendResult);
    if (ok != 0 && sendResult != FALSE) {
      delivered = true;
      break;
    }
    sleepSlice(50);
  }
  DestroyWindow(sender);
  return delivered;
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
  wcsncpy_s(app.tray.szTip, SettingsUiLoc::TrayTip(SettingsUiLoc::ResolveLang()), _TRUNCATE);
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
  MenuTheme::PreparePopup();
  const SettingsUiLoc::Lang lang = SettingsUiLoc::ResolveLang();
  HMENU menu = CreatePopupMenu();
  if (!menu) {
    LUX_LOGE(L"ShowTrayMenu: CreatePopupMenu failed");
    return;
  }
  AppendMenuW(menu, MF_STRING, IDM_TRAY_SETTINGS, SettingsUiLoc::TraySettings(lang));
  AppendMenuW(menu, MF_STRING, IDM_TRAY_SHOW, SettingsUiLoc::TrayShow(lang));
  AppendMenuW(menu, MF_STRING, IDM_TRAY_RELOAD, SettingsUiLoc::TrayReload(lang));
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, IDM_TRAY_EXIT, SettingsUiLoc::TrayExit(lang));
  // WS_EX_NOACTIVATE blocks the foreground trick, so the menu would stay open on an outside click.
  const LONG_PTR previousEx = GetWindowLongPtrW(app.hostHwnd, GWL_EXSTYLE);
  if (previousEx & WS_EX_NOACTIVATE) {
    SetWindowLongPtrW(app.hostHwnd, GWL_EXSTYLE, previousEx & ~static_cast<LONG_PTR>(WS_EX_NOACTIVATE));
  }
  SetForegroundWindow(app.hostHwnd);
  TrackPopupMenuEx(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN, screenPt.x, screenPt.y,
                   app.hostHwnd, nullptr);
  if (previousEx & WS_EX_NOACTIVATE) {
    SetWindowLongPtrW(app.hostHwnd, GWL_EXSTYLE, previousEx);
  }
  DestroyMenu(menu);
  PostMessageW(app.hostHwnd, WM_NULL, 0, 0);
}

void NotifyTrayBalloon(const wchar_t* title, const wchar_t* text) {
  AppState& app = GetApp();
  if (!app.trayRegistered || !title || !text) {
    LUX_LOGW(L"%s", text ? text : L"");
    return;
  }
  NOTIFYICONDATAW nid = app.tray;
  nid.uFlags = NIF_INFO;
  nid.dwInfoFlags = NIIF_WARNING;
  wcsncpy_s(nid.szInfoTitle, title, _TRUNCATE);
  wcsncpy_s(nid.szInfo, text, _TRUNCATE);
  Shell_NotifyIconW(NIM_MODIFY, &nid);
}

namespace {
void CleanupProcessHandles(AppState& app) {
  UnregisterTrayIcon(app);
  if (app.readyEvent) {
    CloseHandle(app.readyEvent);
    app.readyEvent = nullptr;
  }
  if (app.singleInstanceMutex) {
    CloseHandle(app.singleInstanceMutex);
    app.singleInstanceMutex = nullptr;
  }
}
}  // namespace

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
  InitInstanceObjectNames();
  // Prefer System32 for later LoadLibrary calls. The exe is per-user; do not search the cwd.
  SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32 | LOAD_LIBRARY_SEARCH_USER_DIRS);
  // Startup order (documented contract):
  //   1) parse argv + LuxLogInit
  //   2) mutex (single instance) / WM_COPYDATA handoff
  //   3) SettingsStoreInit
  //   4) host HWND (WM_COPYDATA target)
  //   5) LuxOverlay_Init → overlay HWND + D2D (no workers yet)
  //   6) SignalAppReady  (second instance may open settings now)
  //   7) LuxOverlay_StartWorkers
  //   8) message loop
  int argc = 0;
  wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  AppOptions options = ParseCommandLine(argv ? argc : 0, argv);
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
  MenuTheme::EnableSystemMenus();

  const bool ephemeral = options.ephemeral || options.smokeTest || options.testJson;
  LuxLogInit(ephemeral);
  if (options.logLevel[0]) {
    if (_wcsicmp(options.logLevel, L"error") == 0) {
      LuxLogSetMinLevel(LuxLogLevel::Error);
    } else if (_wcsicmp(options.logLevel, L"warn") == 0) {
      LuxLogSetMinLevel(LuxLogLevel::Warn);
    } else if (_wcsicmp(options.logLevel, L"info") == 0) {
      LuxLogSetMinLevel(LuxLogLevel::Info);
    } else if (_wcsicmp(options.logLevel, L"debug") == 0) {
      LuxLogSetMinLevel(LuxLogLevel::Debug);
    }
  }

  if (options.quit) {
    if (!AcquireSingleInstance(g_app)) {
      const bool stopped = NotifyRunningInstance(CopyDataCommand::Quit);
      LuxLogShutdown();
      return stopped ? static_cast<int>(LuxExitCode::Ok)
                     : static_cast<int>(LuxExitCode::InitFailure);
    }
    CleanupProcessHandles(g_app);
    LuxLogShutdown();
    return static_cast<int>(LuxExitCode::Ok);
  }

  if (!AcquireSingleInstance(g_app)) {
    if (options.smokeTest || options.testJson) {
      LUX_LOGE(L"smoke/test-json: another instance holds the session mutex — abort");
      LuxLogShutdown();
      return static_cast<int>(LuxExitCode::InitFailure);
    }
    const CopyDataCommand cmd =
        options.openSettings ? CopyDataCommand::OpenSettings : CopyDataCommand::ShowIsland;
    // If the peer exited mid-wait (e.g. smoke), Notify fails — retry mutex and become primary.
    if (NotifyRunningInstance(cmd)) {
      LuxLogShutdown();
      return static_cast<int>(LuxExitCode::Ok);
    }
    LUX_LOGW(L"Peer gone during handoff; retrying as primary instance");
    if (!AcquireSingleInstance(g_app)) {
      MessageBoxW(nullptr,
                  L"Dynamic Island Lux is already running, but it did not respond.\n"
                  L"Use the tray icon of the running copy, or end that process and start again.",
                  L"Dynamic Island Lux", MB_OK | MB_ICONWARNING);
      LuxLogShutdown();
      return static_cast<int>(LuxExitCode::InitFailure);
    }
    // Keep options.openSettings — after host HWND is up we still Post WM_APP_OPEN_SETTINGS.
    if (options.openSettings) {
      LUX_LOGI(L"--settings: became primary after peer exit; will open dialog");
    }
  }

  SettingsStoreInit(ephemeral);
  LUX_LOGI(L"Dynamic Island Lux %s starting", LUX_APP_VERSION_STRING);

  // Sync HKCU Run with JSON, but honor Task Manager "Disabled" (StartupApproved).
  // Never force-enable from JSON alone — that undoes the user's Task Manager toggle.
  // Installer Run uses --enable-autostart. A first launch with no settings.json also
  // adopts an existing Run value so the wizard checkbox survives startup.
  if (!ephemeral && !options.noAutostart) {
    if (options.enableAutostart) {
      SettingsSetBool(L"General.LaunchAtStartup", true);
      SettingsStoreSaveUser();
      if (!AutostartSetEnabled(true)) {
        LUX_LOGE(L"Autostart: --enable-autostart could not write HKCU Run");
      }
    } else {
      const AutostartState state = AutostartQuery();
      bool want = SettingsGetBool(L"General.LaunchAtStartup", false);
      if (!want && SettingsStoreUserFileWasMissing() &&
          (state == AutostartState::On || state == AutostartState::StalePath)) {
        want = true;
        SettingsSetBool(L"General.LaunchAtStartup", true);
        SettingsStoreSaveUser();
        LUX_LOGI(L"Autostart: adopted existing HKCU Run value on first launch");
      }
      switch (state) {
      case AutostartState::On:
        if (want) {
          AutostartSetEnabled(true);
        } else {
          AutostartSetEnabled(false);
        }
        break;
      case AutostartState::StalePath:
        if (want) {
          if (!AutostartSetEnabled(true)) {
            LUX_LOGE(L"Autostart: failed to rewrite a stale Run path");
          }
        } else {
          AutostartSetEnabled(false);
        }
        break;
      case AutostartState::DisabledByUser:
        if (want) {
          SettingsSetBool(L"General.LaunchAtStartup", false);
          SettingsStoreSaveUser();
          LUX_LOGI(L"Autostart: OS disabled — cleared General.LaunchAtStartup");
        }
        break;
      case AutostartState::Off:
        if (want) {
          SettingsSetBool(L"General.LaunchAtStartup", false);
          SettingsStoreSaveUser();
          LUX_LOGI(L"Autostart: Run value missing — cleared General.LaunchAtStartup");
        }
        break;
      }
      // New AutostartState values must be handled above (C4062).
      static_assert(static_cast<int>(AutostartState::Off) == 0, "AutostartState::Off");
      static_assert(static_cast<int>(AutostartState::On) == 1, "AutostartState::On");
      static_assert(static_cast<int>(AutostartState::DisabledByUser) == 2, "AutostartState::DisabledByUser");
      static_assert(static_cast<int>(AutostartState::StalePath) == 3, "AutostartState::StalePath");
    }
  }

  if (options.testJson) {
    const bool ok = SettingsStoreSelfTestJson() && MediaLikeBridgeSelfTest();
    SettingsStoreShutdown();
    CleanupProcessHandles(g_app);
    LuxLogShutdown();
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

  // Hidden top-level (not HWND_MESSAGE): receives TaskbarCreated broadcasts for tray
  // recreate after Explorer restart, and is findable via FindWindowW / FindWindowExW.
  g_app.hostHwnd = CreateWindowExW(
      WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kHostClassName, L"DynamicIslandLuxHost", WS_POPUP, 0, 0,
      0, 0, nullptr, nullptr, instance, nullptr);
  if (!g_app.hostHwnd) {
    LUX_LOGE(L"Host window failed");
    SettingsStoreShutdown();
    CleanupProcessHandles(g_app);
    LuxLogShutdown();
    return static_cast<int>(LuxExitCode::InitFailure);
  }

  if (!LuxOverlay_Init(g_app)) {
    LUX_LOGE(L"LuxOverlay_Init failed");
    SettingsStoreShutdown();
    CleanupProcessHandles(g_app);
    LuxLogShutdown();
    return static_cast<int>(LuxExitCode::InitFailure);
  }

  if (!options.noTray && !ephemeral) {
    RegisterTrayIcon(g_app);
  }

  if (options.fromStartup && !ephemeral) {
    if (options.startMinimized) {
      LuxOverlay_HideIsland();
      LUX_LOGI(L"Autostart (--minimized): tray registered, island hidden");
    } else {
      LuxOverlay_ShowIsland();
      LUX_LOGI(L"Autostart: island shown, tray registered");
    }
  }

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
      // LoadSettings prefers Appearance.BackdropMaterial unless it is empty/auto,
      // then Themes.BackdropMaterial. Smoke must set both so --backdrop=blur|acrylic stick.
      // Without --backdrop=, leave store/schema as-is.
      const bool fromFlag = options.smokeBackdrop[0] != L'\0';
      if (fromFlag) {
        SettingsSetString(L"Appearance.BackdropMaterial", L"auto");
        SettingsSetString(L"Themes.BackdropMaterial", options.smokeBackdrop);
      }
      const std::wstring appearance =
          SettingsGetString(L"Appearance.BackdropMaterial", L"auto");
      const std::wstring themes = SettingsGetString(L"Themes.BackdropMaterial", L"acrylic");
      const std::wstring requested =
          (!appearance.empty() && _wcsicmp(appearance.c_str(), L"auto") != 0) ? appearance
                                                                              : themes;
      LUX_LOGI(L"backdrop: source=%s appearance=%s themes=%s effective=%s",
               fromFlag ? L"flag" : L"schema", appearance.c_str(), themes.c_str(),
               requested.c_str());
      if (!LuxOverlay_StartWorkers(g_app)) {
        LUX_LOGE(L"smoke-test: WhTool_ModInit failed");
        initOk = false;
      }
    }

    LuxOverlay_Uninit(g_app);  // WhTool_ModUninit → wait threads → destroy scaffold
    const bool workersStuck = LuxWorkersStuckOnExit();
    if (workersStuck) {
      LUX_LOGE(L"smoke-test: worker(s) stuck on Uninit (see worker … did not stop)");
    }
    SettingsStoreShutdown();
    CleanupProcessHandles(g_app);
    LuxLogShutdown();
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
    SettingsStoreShutdown();
    CleanupProcessHandles(g_app);
    LuxLogShutdown();
    return static_cast<int>(LuxExitCode::InitFailure);
  }

  // Ready only once workers started and this thread is about to pump — second-instance
  // IPC uses SendMessageTimeout and would fail during the StartThreads wait.
  SignalAppReady(g_app);

  MSG msg = {};
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }

  LuxOverlay_Uninit(g_app);
  SettingsStoreShutdown();
  CleanupProcessHandles(g_app);
  LuxLogShutdown();
  return static_cast<int>(msg.wParam);
}
