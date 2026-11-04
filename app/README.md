# Dynamic Island Lux (standalone)

Окремий Win32-застосунок для Windows 10/11. **Windhawk не потрібен.**

Ported from the Windhawk mod by **Himanshu ([Averixor](https://github.com/Averixor))** — license **MIT** (see `/LICENSE`).  
Root file `Dynamic Island for Windows Lux.wh.cpp` is the **intentional archive** of the original single-file mod and the TU linked into this app — do not delete it as “junk”.

## Збірка

Потрібні Visual Studio 2022 (C++ Desktop) і CMake ≥ 3.20.

```bat
app\build.bat
```

VERSIONINFO (`FileVersion` / `ProductVersion` / `CompanyName` / `ProductName`) генерується з `project(VERSION …)` через `version.rc.in` → `version.gen.rc`. Не хардкодити версію в `resources.rc`.

Результат: `build\Release\DynamicIslandLux.exe` (прапорці `--smoke-test` / `--test-json` доступні в **Release**, не лише Debug).

## Запуск

```bat
build\Release\DynamicIslandLux.exe
build\Release\DynamicIslandLux.exe --settings
build\Release\DynamicIslandLux.exe --test-json
build\Release\DynamicIslandLux.exe --smoke-test --no-workers
build\Release\DynamicIslandLux.exe --smoke-test
build\Release\DynamicIslandLux.exe --smoke-test --backdrop=blur
build\Release\DynamicIslandLux.exe --smoke-test --backdrop=acrylic
```

### Exit codes (`LuxExitCode`)

| Code | Meaning |
|------|---------|
| 0 | OK |
| 1 | Init failure |
| 2 | Worker stuck on Uninit |
| 3 | Smoke frame failure |
| 4 | `--test-json` failed |

`--backdrop=` відсутній → схема `Themes.BackdropMaterial` (`none`). Лог: `backdrop: source=schema|flag value=…`.

`app.version` у `settings.json` оновлюється **лише якщо змінився** (не перезаписує файл щосесії).

## Known limitations

- **`SetWindowCompositionAttribute` (WCA)** — недокументований API. На майбутніх збірках Win11 accent може зникнути без попередження. Fallback = backdrop `none`, не crash (Warn у лозі).
- **LL-хуки** (`WH_KEYBOARD_LL` / `WH_MOUSE_LL`) можуть блокуватись антивірусом/EDR — Caps/Num і mouse-wake тоді мовчать; решта острова працює. Quick Lookup hotkey = `RegisterHotKey` (не LL).
- **Autostart** через `HKCU\…\Run` не поважає Group Policy (`DisableRegistryTools` / `DisableTaskMgr`) — на керованих ПК toggle може бути no-op без Error.
- **`Local\` mutex** — per-session; у RDP з кількома сесіями кожна має свій екземпляр (свідомо).
- **Мінімальна ОС:** Win10 **1809+** (SMTC + WCA); Win11 для acrylic backdrop.

## Acceptance перед тегом v1.0.0

### Автоматичні (`app\build.bat`)

- [ ] `--smoke-test --no-workers` → exit 0
- [ ] `--smoke-test` → exit 0, `backdrop: source=schema value=none`
- [ ] `--smoke-test --backdrop=blur` → exit 0
- [ ] `--smoke-test --backdrop=acrylic` → exit 0
- [ ] `--test-json` → exit 0
- [ ] Дві послідовні `--smoke-test` → друга не падає на mutex

### Холодний старт (CI не покриває — обовʼязково перед тегом)

1. **Чистий старт:** видалити `%AppData%\DynamicIslandLux\` → запуск без аргументів → пігулка; `settings.json` + `runtime.json` валідні; без `.tmp`; лог без Error.
2. **Зміна + рестарт:** змінити Backdrop / QuickLookup / Language → OK → exit з трею ≤1 s (`worker … stopped`) → знову запуск → зміни на місці; `LastWriteTime` не стрибає без змін.
3. **Cancel:** діалог → змінити → Cancel → `settings.json` незмінний, острів без reload.
4. **Пошкоджений JSON:** обрізати `settings.json` → запуск → Warn, файл → `settings.json.bad`, новий валідний з дефолтами, без крашу.
5. **Read-only (опційно):** `attrib +R settings.json` → OK у діалозі → Warn, зміни в памʼяті, без крашу.
6. **Другий екземпляр:** повторний `.exe` → ≤1.5 s exit, без другого трею.
7. **`--settings` після Task Manager kill** → новий primary + діалог.

Release: Info-лог за замовчуванням; Debug лише з `--log-level=debug`.

### Ручні (~10 хв)

Див. попередній чекліст: пігулка, media, weather, lookup, backdrop+DPI, file tray, game overlay, мови, settings OK/Cancel, autostart, `--settings` IPC, чистий exit, лог без зайвих Error.

### Тег

Після зеленого чекліста:

```bat
git tag -a v1.0.0 -m "Dynamic Island Lux standalone v1.0.0"
```

Release assets: `DynamicIslandLux.exe` + `LICENSE` + `INSTALL.txt` (див. `/RELEASE_NOTES.md`).

## Архітектурні інваріанти

- Hotkey / PeekMessage / Unregister — **render-thread**
- Island layered; DwmBackdrop non-layered + WCA; Show → EnsureBehind; accent після SetWindowPos
- Settings OK → store reload (main) + `WM_APP_RELOAD_SETTINGS` (render)
- Custom settings dialog (не PROPSHEET)

## Дані

- `%AppData%\DynamicIslandLux\settings.json`
- `%AppData%\DynamicIslandLux\runtime.json`
- `%AppData%\DynamicIslandLux\modstorage`
- `%LocalAppData%\DynamicIslandLux\logs\lux.log`
