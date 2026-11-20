# Dynamic Island for Windows Lux — v1.0.8

Standalone release (no Windhawk required).

- **Tag:** `v1.0.8`
- **Date:** 2026-11-20

The git tag is the source of truth for the release commit. Do not paste a commit hash here — editing the notes after the tag moves HEAD off the tag.

## Highlights since v1.0.7

- Settings dialog paint restored: `PaintSession` flushes the off-screen buffer before `EndPaint`; child controls redraw after layout/resize (`RDW_ALLCHILDREN`); themed labels; closed combo height fixed
- Like / Dislike for desktop players when `Modules.MediaLikeBridge` is on — browser bridge plus Pear Desktop / ytmdesktop loopback APIs and UI Automation for Spotify, TIDAL, Deezer, Apple Music, YouTube Music desktop/PWA
- Expanded player: session switcher no longer overlaps Repeat; dislike glyph stays in its circle; clear thumbs-up / thumbs-down icons
- Self-tests: paint probe on a real monitor (DWM-safe `GetPixel`); like-bridge label checks match updated UI strings; clangd C++17 config

## Origin

Ported from the Windhawk mod **Dynamic Island for Windows** by **Himanshu (devcode90)**, MIT license, and maintained as Lux by **Averixor**. See `NOTICE`.

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

- `DynamicIslandLux-Setup-1.0.8.exe` (Inno Setup, per-user, no admin)
- `DynamicIslandLux.exe` (portable Release x64, `/MT`)
- `LICENSE`
- `NOTICE`
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

Verify Properties → Details on the exe shows **File version 1.0.8.0**, CompanyName Averixor, ProductName Dynamic Island for Windows Lux.
