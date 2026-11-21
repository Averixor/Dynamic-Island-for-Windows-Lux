# Dynamic Island for Windows Lux — v1.0.11

Windows application release.

- **Tag:** `v1.0.11`
- **Date:** 2026-11-21

The git tag is the source of truth for the release commit. Do not paste a commit hash here — editing the notes after the tag moves HEAD off the tag.

## Highlights since v1.0.10

- Removed theme aura (glow / rays / rings / orbit), cover-spin on the pill, and wavy / squiggle progress styles
- Polished Ukrainian and Russian settings labels (volume step, Direct Composition, privacy dots, backdrop materials, Bluetooth, and related strings)
- clangd / VS Code indexing for `.h` headers as C++; editor word wrap enabled in the workspace

## Origin

The island implementation lives in `app/island.cpp` and is compiled into the application executable.

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

## Architecture

The Win32 executable owns its process lifecycle, settings dialog, tray icon, autostart, and storage. The island implementation is compiled from `app/island.cpp`.

## Minimum OS

- Windows 10 **1809+** (SMTC, `SetWindowCompositionAttribute`)
- Windows 11 recommended for acrylic backdrop

## Known limitations

See `app/README.md` → **Known limitations**.

## Attachments (GitHub Release)

- `DynamicIslandLux-Setup-1.0.11.exe` (Inno Setup, per-user, no admin)
- `Dynamic Island Lux.exe` (portable Release x64, `/MT`)
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

Verify Properties → Details on the exe shows **File version 1.0.11**, CompanyName Dynamic Island Lux, ProductName Dynamic Island for Windows Lux.
