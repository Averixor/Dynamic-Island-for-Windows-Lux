# Dynamic Island for Windows Lux — v1.0.2

Standalone release (no Windhawk required).

- **Tag:** `v1.0.2`
- **Commit:** `19c07015611ee4cef2584cb8a353295a7354d8ef`
- **Date:** 2026-11-04

## Highlights since v1.0.1

- Settings dialog and context/tray menus follow the OS light/dark theme
- Schema defaults for AlwaysOnTop, media, modules, aura, clock/date
- Locale-aware island menu + settings UI (EN/UK/RU)
- Scrollable fixed-size settings dialog without paint tearing
- Standalone **Exit** closes the app; Close no longer only collapses the island

## Origin

Ported from the Windhawk mod **Dynamic Island for Windows Lux** by **Himanshu (Averixor)**, MIT license.

The original single-file mod body remains at the repo root as  
`Dynamic Island for Windows Lux.wh.cpp` — intentional archive **and** the translation unit linked into the exe. It is not a leftover file.

## What’s included

- Dynamic island overlay (media, volume, brightness, clipboard, battery, weather, calendar, system monitor, Bluetooth, DND, focus timer, lyrics)
- Quick Lookup (dictionary / Wikipedia)
- DWM blur / acrylic backdrop (separate non-layered windows via WCA)
- File tray (OLE drop + atomic persist; corrupt JSON → `.bad` + defaults)
- Game overlay metrics (idle without a game is fine)
- Multi-language UI via `General.Language`
- Tray icon, custom settings dialog, HKCU autostart
- Flat JSON settings + separate `runtime.json`, rotating logs
- `--smoke-test` / `--test-json` / `--log-level=` / exit codes 0–4

## What changed vs Windhawk

| Windhawk | Standalone |
| --- | --- |
| Injected / tool-mod process | Own Win32 exe + tray |
| Windhawk YAML settings | Flat `settings.json` + schema-driven dialog |
| Windhawk storage API | `%AppData%\DynamicIslandLux\` |
| No CI hook | `--smoke-test`, `--test-json`, exit codes 0–4 |

## Minimum OS

- Windows 10 **1809+** (SMTC, `SetWindowCompositionAttribute`)
- Windows 11 recommended for acrylic backdrop

## Known limitations

See `app/README.md` → **Known limitations**.

## Attachments (GitHub Release)

- `DynamicIslandLux.exe` (Release x64, `/MT`)
- `LICENSE`
- `INSTALL.txt`
- `RELEASE_NOTES.md`
- `SHA256SUMS.txt` (`sha256sum -c` compatible)

```bat
app\build.bat
scripts\make-release-assets.bat
```

Verify Properties → Details on the exe shows **File version 1.0.2**, CompanyName Averixor, ProductName Dynamic Island for Windows Lux.
