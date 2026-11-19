# Dynamic Island for Windows Lux — v1.0.3

Standalone release (no Windhawk required).

- **Tag:** `v1.0.3`
- **Commit:** 7aec63eb74eb8bfda9f18a63136cfeb0543bcaa7
- **Date:** 2026-11-19

## Highlights since v1.0.2

- Inline media volume scale (digits while adjusting) and queue-mode control; external volume pill removed
- Lyrics button moved left of queue mode (no longer overlaps volume > 100%)
- Weather click opens Bing Weather or wttr.in — no Store `ms-weather` stub dialog
- System monitor network Mbps via `GetIfTable2` (works on localized Windows)
- Fixed red privacy dots with Camera / Microphone labels when expanded
- Settings schema cleanup: brightness / split-wheel / hex privacy colors removed; shape & theme presets added
- Slimmer context menu (Hide / Reset / Exit / Settings)
- Autostart checkbox remembers correctly (StartupApproved `0x02`/`0x03` read was inverted)
- Release `.bat` scripts no longer trip Windows “Open with Write-Host” via bare `|` pipes

## Origin

Ported from the Windhawk mod **Dynamic Island for Windows Lux** by **Himanshu (Averixor)**, MIT license.

The original single-file mod body remains at the repo root as  
`Dynamic Island for Windows Lux.wh.cpp` — intentional archive **and** the translation unit linked into the exe. It is not a leftover file.

## What’s included

- Dynamic island overlay (media, volume, clipboard, battery, weather, calendar, system monitor, Bluetooth, DND, focus timer, lyrics)
- Quick Lookup (dictionary / Wikipedia)
- DWM blur / acrylic backdrop (separate non-layered windows via WCA)
- File tray (OLE drop + atomic persist; corrupt JSON > `.bad` + defaults)
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

See `app/README.md` > **Known limitations**.

## Attachments (GitHub Release)

- `DynamicIslandLux-Setup-1.0.3.exe` (Inno Setup, per-user, no admin)
- `DynamicIslandLux.exe` (portable Release x64, `/MT`)
- `LICENSE`
- `INSTALL.txt`
- `RELEASE_NOTES.md`
- `SHA256SUMS.txt` (`sha256sum -c` compatible)

```bat
app\build.bat
scripts\make-installer.bat
scripts\make-release-assets.bat
```

Verify Properties > Details on the exe shows **File version 1.0.3**, CompanyName Averixor, ProductName Dynamic Island for Windows Lux.
