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
```

`--smoke-test` — ephemeral (без трею/автозапуску). Кадр береться зі scaffold; **не чекає** media/мережу. Успіх: `renderedFrames > 0` і `lastFrameHr == S_OK`.

`--no-workers` — без CreateThread / SetWindowsHookEx (медіа, аудіо, погода, PDH, LL hooks). CI: завжди `--smoke-test --no-workers`.

`--smoke-test` без `--no-workers` — після кадру стартує `WhTool_ModInit`; Uninit має чисто зупинити потоки (`worker … started` + `WhTool_ModUninit: workers stopped`).

`--test-json` — round-trip escape для плаского JSON (шляхи з `"`, `\n`).

Порядок старту: mutex → host HWND → `LuxOverlay_Init` → Ready → workers → кадр.

## Порт `.wh.cpp` (кроки)

- **A:** `.wh.cpp` у лінку; `Wh_Mod*` вимкнені; scaffold-пігулка для smoke.
- **B:** `StartWorkers` → лише `WhTool_ModInit`; `Uninit` → `WhTool_ModUninit`; `LuxShouldStartWorkers = !--no-workers`.
- **C (зараз):** C1 lookup/hooks → C2 DWM → C3 file tray → C4 game overlay → C5 мови.
- **D:** DPI smoke; `/W4` для legacy.

Прогони C (кожен підкрок):
```bat
build\Release\DynamicIslandLux.exe --smoke-test --no-workers
build\Release\DynamicIslandLux.exe --smoke-test
build\Release\DynamicIslandLux.exe --smoke-test --smoke-timeout-ms=3000
```
Кадр = scaffold (`renderedFrames>0`, `lastFrameHr==S_OK`), без очікування media/погоди.
`--smoke-test` без `--no-workers` вмикає `Themes.BackdropMaterial=acrylic` (окремі DWM HWND, не на layered island).
Exit `2` = воркер не зупинився за timeout (у лозі `worker X did not stop`).

### C1 Lookup + hooks
- Quick Lookup = `RegisterHotKey` на render-thread `g_hwnd` (не LL, не temp thread).
- Caps/Num + mouse wake = LL hooks на довгоживучих `KeyboardThreadProc` / `MouseThreadProc`.
- `--no-workers` → жоден `CreateThread` / `SetWindowsHookEx` / hotkey.
- Збій хука/hotkey на CI = Warn, не abort.

### C2 DWM blur/acrylic
- Island: `WS_EX_LAYERED` + `UpdateLayeredWindowIndirect`.
- Backdrop: окремі non-layered HWND + `SetWindowCompositionAttribute` (`DwmBackdrop::`).
- На island `DwmEnableBlurBehind` **вимкнено** — змішувати layered+DWM на одному HWND не можна.

### C3 File tray
- DnD: OLE `IDropTarget` (+ legacy `WM_DROPFILES` → `AddPathsToFileTray`).
- Drop лише на пігулці (`WM_NCHITTEST` → `HTTRANSPARENT` поза pill).
- Persist: `WriteFileAtomic` (`.tmp` + `MoveFileEx`) у `%AppData%\DynamicIslandLux\modstorage`.
- `TaskbarCreated` не чіпає file-tray стан.

### C4 Game overlay
- Метрики/foreground — у render/metrics циклі, без окремого polling-потоку.
- Idle без гри: оверлей малює CPU/RAM… якщо модуль/pin увімкнені.
- Кліки поза пігулкою: `WS_EX_TRANSPARENT` + `HTTRANSPARENT`.

### C5 Мови
- `General.Language` зі `SettingsStore` → `g_uiLanguage` у `LoadSettings`.
- Діалог → `SettingsStoreReload` → `WhTool_ModSettingsChanged` → `g_layoutDirty` + Loc().

Compat: кожен `Wh_GetStringSetting` іде через `GetStringSettingCopy` → парний `Wh_FreeStringSetting`.

## Дані

- `%AppData%\DynamicIslandLux\settings.json` — користувацькі налаштування (плаский JSON)
- `%AppData%\DynamicIslandLux\runtime.json` — ефемерні overrides
- `%LocalAppData%\DynamicIslandLux\logs\lux.log` — лог з ротацією
