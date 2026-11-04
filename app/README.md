# Dynamic Island Lux (standalone)

Окремий Win32-застосунок для Windows 10/11. Windhawk не потрібен.

## Збірка

Потрібні Visual Studio 2022 (C++ Desktop) і CMake ≥ 3.20.

```bat
app\build.bat
```

або:

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
build\Release\DynamicIslandLux.exe --smoke-test --backdrop=acrylic
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
`--no-workers` — без CreateThread / SetWindowsHookEx / hotkeys (exit `2` неможливий).
`--backdrop=` — якщо **не** задано → схема `Themes.BackdropMaterial` (`none`). Лог: `backdrop: source=schema|flag value=…`.
`--settings` — другий екземпляр чекає Ready; якщо peer помер → retry mutex як primary **і відкриває діалог**.

Порядок старту: mutex → host HWND → `LuxOverlay_Init` → Ready → workers → кадр.

## Acceptance перед тегом v1.0

### Автоматичні (CI / `app\build.bat`)

- [ ] `--smoke-test --no-workers` → exit 0, лог без Error
- [ ] `--smoke-test` (без `--backdrop`) → exit 0, лог `backdrop: source=schema value=none`
- [ ] `--smoke-test --backdrop=blur` → exit 0, лог `source=flag value=blur`
- [ ] `--smoke-test --backdrop=acrylic` → exit 0, лог `source=flag value=acrylic`
- [ ] `--smoke-test --smoke-timeout-ms=3000` → exit 0 (не 2)
- [ ] `--test-json` → exit 0
- [ ] Дві послідовні `--smoke-test` → друга не падає на mutex

### Ручні (~10 хв)

- [ ] Запуск без аргументів: пігулка, трей, Idle → media
- [ ] Медіа play/pause/next → пігулка оновлюється
- [ ] Погода: оновлення або Warn мережі, не crash
- [ ] Quick Lookup hotkey; після 10 хв ще працює
- [ ] Backdrop у діалозі без рестарту; DPI 100→150→100 без чорного backdrop
- [ ] File tray: drop на pill; поза pill ігнор; explorer restart → tray іконка + persist
- [ ] Game overlay idle без гри; alt-tab; закриття гри → idle
- [ ] Мова в діалозі → перерендер без рестарту
- [ ] Settings OK застосовує; Cancel без змін; без Apply-кнопки
- [ ] Autostart on → HKCU Run quoted; Task Manager Disable → діалог «вимкнено»; off → ключ видалено
- [ ] `--settings` при запущеному → діалог у першому; другий виходить
- [ ] `--settings` після краху peer → новий primary + діалог
- [ ] Вихід з трею ≤1 s; лог `worker … stopped`
- [ ] Лог 10 хв: без Error; Warn лише очікувані

### Регресії

- [ ] Візуально ≈ Windhawk-версія (розмір/позиція/анімації)

## Архітектурні інваріанти

- Hotkey: `RegisterHotKey` + `PeekMessage` + `UnregisterHotKey` на **render-thread**
- Backdrop: island layered; DwmBackdrop non-layered + WCA; Show island → EnsureBehind; accent після SetWindowPos
- Settings OK → `SettingsStoreReload` (main) + `PostMessage(WM_APP_RELOAD_SETTINGS)` (render)
- Modal dialog на main не блокує render-thread

## Дані

- `%AppData%\DynamicIslandLux\settings.json`
- `%AppData%\DynamicIslandLux\runtime.json`
- `%AppData%\DynamicIslandLux\modstorage`
- `%LocalAppData%\DynamicIslandLux\logs\lux.log`
