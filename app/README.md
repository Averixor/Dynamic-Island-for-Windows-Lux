# Dynamic Island Lux (standalone)

Окремий Win32-застосунок для Windows 10/11. **Windhawk не потрібен.**

Derivative of [Dynamic Island for Windows](https://github.com/devcode90/Dynamic-Island-for-Windows) by **Himanshu (devcode90)**, maintained by **[Averixor](https://github.com/Averixor)** — license **MIT** (see `/LICENSE` and `/NOTICE`).  
Root file `Dynamic Island for Windows Lux.wh.cpp` is the **intentional archive** of the original single-file mod and the TU linked into this app — do not delete it as “junk”.

## Збірка

Потрібні Visual Studio 2022 або 2026 (C++ Desktop) і CMake ≥ 3.20 (CMake ≥ 4.2 для Visual Studio 2026).

```bat
app\build.bat
```

Версія = `project(DynamicIslandLux VERSION …)` у `CMakeLists.txt`.  
Пакування/інсталер читають її через `scripts\_version.cmd` (не дублювати вручну).

VERSIONINFO (`FileVersion` / `ProductVersion` / `CompanyName` / `ProductName`) генерується з `project(VERSION …)` через `version.rc.in` → `version.gen.rc`.  
Маніфест + іконка: `resources.rc.in` → `resources.gen.rc` (`DynamicIslandLux.manifest.in`, `app.ico`). Не компілювати `resources.rc` вручну.

Результат: `build\Release\DynamicIslandLux.exe` (прапорці `--smoke-test` / `--test-json` доступні в **Release**, не лише Debug).

## Запуск

```bat
build\Release\DynamicIslandLux.exe
build\Release\DynamicIslandLux.exe --settings
build\Release\DynamicIslandLux.exe --minimized
build\Release\DynamicIslandLux.exe --test-json
build\Release\DynamicIslandLux.exe --smoke-test --no-workers
build\Release\DynamicIslandLux.exe --smoke-test
build\Release\DynamicIslandLux.exe --smoke-test --backdrop=blur
build\Release\DynamicIslandLux.exe --smoke-test --backdrop=acrylic
build\Release\DynamicIslandLux.exe --log-level=debug
```

| Flag | Effect |
| --- | --- |
| `--smoke-test` | Ephemeral run; frame proof then exit (workers start unless `--no-workers`) |
| `--no-workers` | No threads/hooks (CI with `--smoke-test`) |
| `--test-json` | SettingsStore JSON round-trip, then exit |
| `--settings` | Open settings (IPC to existing instance, or become primary) |
| `--minimized` / `--startup` | Tray only; island hidden (HKCU Run uses `--minimized`) |
| `--no-tray` | Skip tray icon. Combined with `--minimized`, the island stays visible so the process can still be closed |
| `--no-autostart` | Do not refresh HKCU Run on this launch |
| `--enable-autostart` | Persist `General.LaunchAtStartup` and rewrite the Run value (installer) |
| `--quit` | Ask the running instance to exit |
| `--ephemeral` | No settings/log file I/O |
| `--backdrop=none\|blur\|acrylic` | Override backdrop for this run (`Appearance=auto` + `Themes=…`) |
| `--smoke-timeout-ms=N` | Smoke wait (default 10000) |
| `--log-level=error\|warn\|info\|debug` | Min log level |

### Exit codes (`LuxExitCode`)

| Code | Meaning |
| --- | --- |
| 0 | OK |
| 1 | Init failure |
| 2 | Worker stuck on Uninit |
| 3 | Smoke frame failure |
| 4 | `--test-json` failed |

`--backdrop=` відсутній → схема: `Appearance.BackdropMaterial=auto`, `Themes.BackdropMaterial=acrylic` (ефективно acrylic).  
Лог: `backdrop: source=schema|flag appearance=… themes=… effective=…`.

`app.version` у `settings.json` оновлюється **лише якщо змінився** (не перезаписує файл щосесії).

## Known limitations

- **`SetWindowCompositionAttribute` (WCA)** — недокументований API. На майбутніх збірках Win11 accent може зникнути без попередження. Fallback = backdrop `none`, не crash (Warn у лозі).
- **LL-хук** `WH_MOUSE_LL` (mouse-wake) може блокуватись антивірусом/EDR. Caps/Num читаються через `GetAsyncKeyState` без `WH_KEYBOARD_LL`. Quick Lookup / Hide-Show = `RegisterHotKey` (`MOD_NOREPEAT`).
- **Autostart** (`HKCU\…\Run` + `StartupApproved\Run`) — Group Policy може зробити toggle no-op. Disable у Task Manager зберігається. Інсталятор пише `--enable-autostart`; перший запуск без `settings.json` приймає вже існуючий Run. `StalePath` переписує шлях на поточний exe. Uninstall видаляє обидва значення.
- **`Local\` mutex** — per-session; у RDP з кількома сесіями кожна має свій екземпляр (свідомо).
- **Мінімальна ОС:** Win10 **1809+** (SMTC + WCA); Win11 для acrylic backdrop.

## Acceptance перед тегом (версія = `project(VERSION)` у `CMakeLists.txt`, зараз **1.0.7**)

### Автоматичні (`app\build.bat`)

- [ ] `--smoke-test --no-workers` → exit 0
- [ ] `--smoke-test` → exit 0, лог `source=schema` / `appearance=auto` / `themes=acrylic` / `effective=acrylic`
- [ ] `--smoke-test --backdrop=blur` → exit 0
- [ ] `--smoke-test --backdrop=acrylic` → exit 0
- [ ] `--test-json` → exit 0 (включно UTF-8 BOM і `\u` escapes)
- [ ] Дві послідовні `--smoke-test` → друга не падає на mutex

### Холодний старт (CI не покриває — обовʼязково перед тегом)

1. **Чистий старт:** видалити `%AppData%\DynamicIslandLux\` → запуск без аргументів → пігулка; `settings.json` + `runtime.json` валідні; без `.tmp`; лог без Error.
2. **Зміна + рестарт:** змінити Backdrop / QuickLookup / Language → OK → exit з трею ≤1 s (`worker … stopped`) → знову запуск → зміни на місці; `LastWriteTime` не стрибає без змін.
3. **Cancel:** діалог → змінити → Cancel → UI до baseline (відкриття або після Apply) і закриття; диск лишає вже застосоване Apply/OK. Escape теж не відкочує Apply.
4. **Reset defaults:** Settings → Reset → Yes → схема + drag offsets 0; автозапуск вимкнено.
5. **Пошкоджений JSON:** обрізати `settings.json` → запуск → Warn, файл → `settings.json.bad`, новий валідний з дефолтами, без крашу. UTF-8 BOM (Блокнот) **не** вважається corrupt.
6. **Read-only (опційно):** `attrib +R settings.json` → OK у діалозі → Warn, зміни в памʼяті, без крашу.
7. **Другий екземпляр:** повторний `.exe` → ≤1.5 s exit, без другого трею.
8. **`--settings` після Task Manager kill** → новий primary + діалог.

Release: Info-лог за замовчуванням; Debug лише з `--log-level=debug`.

### Ручні (~10 хв)

Див. попередній чекліст: пігулка, media, weather, lookup, backdrop+DPI, file tray, game overlay, мови, settings OK/Apply/Cancel/Reset, autostart + Task Manager Disable, `--settings` IPC, чистий exit, лог без зайвих Error.

### Тег (жорсткий порядок)

```bat
scripts\release.bat
```

Еквівалент вручну — **лише в цьому порядку** (інакше SHA256SUMS застаріє):

1. `app\build.bat` — Release + smoke (exit ≠ 0 → стоп)
2. Cold-start чекліст вище (corrupt JSON обовʼязково)
3. `scripts\make-release-assets.bat` — копіює **той самий** `.exe`, SHA256, VERSIONINFO≠`@PROJECT_VERSION@` (exit ≠ 0 → стоп)
4. `git tag -a v1.0.7 -m "Dynamic Island Lux standalone v1.0.7"` на **поточний** `HEAD` після зеленого build (версію беріть з `CMakeLists.txt` / `_version.cmd`)

Assets: `dist\v1.0.7\` (папка = `scripts\_version.cmd`).

## Архітектурні інваріанти

- Hotkey / PeekMessage / Unregister — **render-thread**
- Island layered; DwmBackdrop non-layered + WCA; Show → EnsureBehind; accent після SetWindowPos
- Settings Apply/OK → `SettingsStoreSaveUser` + reload (main) + `WM_APP_RELOAD_SETTINGS` (render); dirty `runtime.json` скидається на диск **перед** reload, щоб не затерти drag/pin
- Cancel без Apply = no write / no reload; Cancel після Apply лишає вже записане
- Custom settings dialog (не PROPSHEET)
- Schema (`settings_schema.h`) = єдине джерело ключів — див. `/CONTRIBUTING.md`

## Дані

- `%AppData%\DynamicIslandLux\settings.json`
- `%AppData%\DynamicIslandLux\runtime.json`
- `%AppData%\DynamicIslandLux\modstorage`
- `%LocalAppData%\DynamicIslandLux\logs\lux.log`
