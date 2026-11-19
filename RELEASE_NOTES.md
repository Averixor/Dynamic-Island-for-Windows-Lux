# Dynamic Island for Windows Lux — v1.0.6

Standalone release (no Windhawk required).

- **Tag:** `v1.0.6`
- **Commit:** _(filled after release commit)_
- **Date:** 2026-11-19

## Highlights since v1.0.5

- Host IPC: top-level host HWND + `FindWindowW` first; `SignalAppReady` after workers; COPYDATA send retries
- Settings store: refuse overwrite when `settings.json` is locked/unreadable; validated Int JSON emit; UTF-8 `\u` surrogates; trailing commas tolerated
- Settings UI: Cancel after Apply keeps Apply; unknown enums preserved; Appearance backdrop `none`/`blur`; Tab stops; combo no longer deadens Apply/OK
- Island: honor `--minimized` / manual hide; Caps Lock does not force-unhide; blacklist park no longer 1.5s flicker; render ready only after `Renderer::Initialize`; standalone `StopThreads` bounded; Bluetooth WinRT lifetime hardened
- Packaging: SHA256 via `_sha256.ps1` (no `Get-FileHash` dependency)

## Origin

Ported from the Windhawk mod **Dynamic Island for Windows Lux** by **Averixor**, MIT license.

The original single-file mod body remains at the repo root as  
`Dynamic Island for Windows Lux.wh.cpp` — intentional archive **and** the translation unit linked into the exe. It is not a leftover file.

## What’s included

- Dynamic island overlay (media, volume, clipboard, battery, weather, calendar, system monitor, Bluetooth, DND, focus timer, lyrics)
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

- `DynamicIslandLux-Setup-1.0.6.exe` (Inno Setup, per-user, no admin)
- `DynamicIslandLux.exe` (portable Release x64, `/MT`)
- `LICENSE`
- `INSTALL.txt`
- `RELEASE_NOTES.md`
- `SHA256SUMS.txt` (`sha256sum -c` compatible)

```bat
scripts\release.bat
```

Or step-by-step:

```bat
app\build.bat
scripts\make-release-assets.bat
```

Verify Properties → Details on the exe shows **File version 1.0.6.0**, CompanyName Averixor, ProductName Dynamic Island for Windows Lux.
