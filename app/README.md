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
`--no-workers` — без CreateThread / SetWindowsHookEx / hotkeys.
`--backdrop=` — `acrylic` (дефолт для smoke+workers), `blur`, `none`. Окремо проганяйте blur, бо схема за замовчуванням `none`.
`--settings` на другому екземплярі: чекає Ready ≤ ~1.5 s + FindWindow poll, `SendMessageTimeout` 2 s — не зависає.

Порядок старту: mutex → host HWND → `LuxOverlay_Init` → Ready → workers → кадр.

## Порт `.wh.cpp` (кроки)

- **A/B:** scaffold + `WhTool_ModInit` lifecycle.
- **C:** lookup / DWM / file tray / game overlay / мови.
- **D (зараз):** custom settings dialog (не PROPSHEET) + HKCU Run/StartupApproved + IPC timeout.
- **E:** DPI smoke; `/W4` для legacy.

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
