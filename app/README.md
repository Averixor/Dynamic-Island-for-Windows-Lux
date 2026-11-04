# Dynamic Island Lux (standalone)

Окремий Win32-застосунок для Windows 10/11. Windhawk не потрібен.

## Збірка

Потрібні Visual Studio 2022 (C++ Desktop) і CMake ≥ 3.20.

```bat
cmake -S app -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Результат: `build\Release\DynamicIslandLux.exe`.

## Запуск

```bat
build\Release\DynamicIslandLux.exe
build\Release\DynamicIslandLux.exe --settings
build\Release\DynamicIslandLux.exe --test-json
build\Release\DynamicIslandLux.exe --smoke-test --no-workers
build\Release\DynamicIslandLux.exe --smoke-test
build\Release\DynamicIslandLux.exe --smoke-test --backdrop=blur
build\Release\DynamicIslandLux.exe --smoke-test --smoke-timeout-ms=3000
```

### Exit codes (`LuxExitCode`)

| Code | Meaning |
|------|---------|
| 0 | OK |
| 1 | Init failure (host/overlay/WhTool) |
| 2 | Worker stuck on Uninit (smoke timeout) |
| 3 | Smoke frame failure (`renderedFrames` / `lastFrameHr`) |
| 4 | `--test-json` failed |

`--smoke-test` — ephemeral. Кадр зі scaffold; **не чекає** media/мережу.
`--no-workers` — без CreateThread / SetWindowsHookEx / hotkeys (exit `2` неможливий — Uninit не чекає потоків).
`--backdrop=` — якщо **не** задано, лишається `Themes.BackdropMaterial` зі схеми (`none`). CI має ганяти `--backdrop=blur` і `--backdrop=acrylic` окремо; у лозі `backdrop: requested=… actual=…`.
`--settings` на другому екземплярі: Ready ≤1.5 s + `SendMessageTimeout` 2 s; якщо peer уже помер — retry mutex як primary.

Порядок старту: mutex → host HWND → `LuxOverlay_Init` → Ready → workers → кадр.

## Порт `.wh.cpp` (кроки)

- **A/B:** scaffold + `WhTool_ModInit` lifecycle.
- **C:** lookup / DWM / file tray / game overlay / мови.
- **D:** custom settings dialog (не PROPSHEET) + HKCU Run/StartupApproved + IPC timeout.
- **E (зараз):** фінальний Release smoke + ручний чекліст.
- **F:** DPI/`W4` legacy cleanup.

### Фінальний smoke (Release x64)

```bat
build\Release\DynamicIslandLux.exe --smoke-test --no-workers
build\Release\DynamicIslandLux.exe --smoke-test --backdrop=blur
build\Release\DynamicIslandLux.exe --smoke-test --backdrop=acrylic
```

Ручний чекліст: пігулка, Quick Lookup hotkey, backdrop blur/acrylic/none, file tray drop на pill, game overlay idle, зміна мови без рестарту, settings Apply=OK без рестарту, autostart toggle + Task Manager disable, лог без зайвих Error у штатному прогоні.

Hotkey invariant: `g_hwnd` + `RegisterHotKey` + `PeekMessage` + `UnregisterHotKey` — усі на **render-thread** (не main). Modal settings на main не блокує render.

### D — Settings UI + autostart

- **Архітектура:** custom schema-driven dialog + filter (див. `settings_dialog.h`). Не property sheet.
- **Autostart:** `HKCU\...\Run` з **quoted** шляхом; синхронно `HKCU\...\StartupApproved\Run` (0x02/0x03).
- **Hotkeys:** `RegisterHotKey` на **render-thread** (не main); `UnregisterHotKey` у `WM_DESTROY` / перед `DestroyWindow`.
- **LL hooks:** owner = Keyboard/MouseThreadProc до Uninit; `Unhook` на тому ж потоці.
- **WM_NCHITTEST:** pill → `HTCLIENT` (drop/click); поза pill → `HTTRANSPARENT`.
- **Loc:** table lookup за `g_uiLanguage` щоразу — немає static LoadString-кешу.

## Дані

- `%AppData%\DynamicIslandLux\settings.json` — користувацькі налаштування
- `%AppData%\DynamicIslandLux\runtime.json` — ефемерні overrides
- `%AppData%\DynamicIslandLux\modstorage` — file tray / lyrics cache
- `%LocalAppData%\DynamicIslandLux\logs\lux.log` — лог з ротацією
