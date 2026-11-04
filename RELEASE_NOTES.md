# Dynamic Island for Windows Lux — v1.0.0

First **standalone** release (no Windhawk required).

## Origin

Ported from the Windhawk mod **Dynamic Island for Windows Lux** by **Himanshu (Averixor)**, MIT license.
The original single-file mod body is kept at the repo root as  
`Dynamic Island for Windows Lux.wh.cpp` (archive / link unit — do not delete).

## What’s included

- Dynamic island overlay (media, volume, brightness, clipboard, battery, weather, calendar, system monitor, Bluetooth, DND, focus timer, lyrics)
- Quick Lookup (dictionary / Wikipedia)
- DWM blur / acrylic backdrop (separate non-layered windows)
- File tray (OLE drop + atomic persist)
- Game overlay metrics (idle without a game is fine)
- Multi-language UI via `General.Language`
- Tray icon, custom settings dialog, HKCU autostart
- Flat JSON settings + separate `runtime.json`, rotating logs

## What changed vs Windhawk

| Windhawk | Standalone |
|----------|------------|
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
- `SHA256SUMS.txt` (hash of the `.exe`)

```bat
certutil -hashfile build\Release\DynamicIslandLux.exe SHA256 > SHA256SUMS.txt
```

Verify Properties → Details on the exe shows **File version 1.0.0**, CompanyName Averixor, ProductName Dynamic Island for Windows Lux.
