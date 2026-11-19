# Dynamic Island for Windows Lux

Living pill overlay for Windows 10 / 11, inspired by the iPhone Dynamic Island.  
**Standalone app** — Windhawk is not required.

**Current release:** [v1.0.6](https://github.com/Averixor/Dynamic-Island-for-Windows-Lux/releases/tag/v1.0.6)  
**Author:** [Averixor](https://github.com/Averixor) · **Upstream:** [devcode90](https://github.com/devcode90/Dynamic-Island-for-Windows) · **License:** [MIT](LICENSE)

---

## Install

### Recommended (installer)

1. Download `DynamicIslandLux-Setup-1.0.6.exe` from the [latest release](https://github.com/Averixor/Dynamic-Island-for-Windows-Lux/releases/latest).
2. Run the wizard (per-user install to `%LOCALAPPDATA%\DynamicIslandLux\`, no admin).
3. Optional: enable **Start with Windows** in the wizard or later in Settings.
4. Tray icon appears — right-click for **Settings…** / **Show island**.

### Portable

1. Download `DynamicIslandLux.exe` from the release.
2. Put it somewhere permanent (for example `%LOCALAPPDATA%\DynamicIslandLux\`).
3. Run it. Optional: enable autostart in Settings.

More detail: [INSTALL.txt](INSTALL.txt).

---

## What it does

Collapsed pill: clock, date, media glance. Expand on hover or click for tabs and short-lived alerts.

| Area | Features |
| --- | --- |
| **Media** | SMTC player, artwork, transport, queue mode, inline volume, scrubber, lyrics |
| **System** | Volume, battery, clipboard, Caps/Num Lock, privacy dots (mic/cam) |
| **Info** | Weather (wttr.in), calendar, system monitor (CPU / RAM / GPU / net / disk) |
| **Extras** | File tray, Bluetooth, Do Not Disturb, focus timer, Quick Lookup (dictionary / Wikipedia) |
| **Look** | Pill / Win11 flyout / notch shapes, theme presets, adaptive theme, DWM blur / acrylic, aura |

**Languages:** Ukrainian (default), English, Russian, and more via `General.Language`.

---

## Controls

- **Hover** expands the island (or click — configurable).
- **Drag** empty pill area to move; offset is saved.
- **Right-click** → Hide / Reset position / Exit / Settings.
- **Tray** → Show / Settings / Exit.
- **Fullscreen** apps can auto-hide the island (setting).

---

## Data & logs

Created on first launch:

| Path | Purpose |
| --- | --- |
| `%AppData%\DynamicIslandLux\settings.json` | User settings |
| `%AppData%\DynamicIslandLux\runtime.json` | Runtime state |
| `%AppData%\DynamicIslandLux\modstorage\` | Mod-compatible storage |
| `%LocalAppData%\DynamicIslandLux\logs\lux.log` | Rotating log |

Weather, lyrics, and Quick Lookup send the city or the query you type to third-party services (wttr.in, lrclib.net, dictionaryapi.dev, Wikipedia, Wiktionary). A blank weather city lets wttr.in choose a place from your IP address. Uninstall leaves these files in place; delete the two folders above if you want them gone. Track and city lines are written to the log only at Debug level.

---

## Requirements

- Windows 10 **1809+** (build 17763+) — SMTC + DWM backdrop APIs  
- Windows 11 recommended for acrylic backdrop  

---

## Build from source

Needs **Visual Studio 2022 or 2026** (Desktop C++) and **CMake ≥ 3.20** (CMake ≥ 4.2 for Visual Studio 2026).

```bat
app\build.bat
```

Output: `build\Release\DynamicIslandLux.exe`

Version for packaging/installer is parsed from `app\CMakeLists.txt` via `scripts\_version.cmd` (keep that `project(… VERSION …)` in sync with tags).

Release packaging:

```bat
scripts\release.bat
```

Installer (needs [Inno Setup](https://jrsoftware.org/isinfo.php) 6+):

```bat
winget install --id JRSoftware.InnoSetup.7 -e
scripts\make-installer.bat
```

Developer notes, smoke flags, exit codes, and release checklist: **[app/README.md](app/README.md)**.  
Settings schema rules: **[CONTRIBUTING.md](CONTRIBUTING.md)**.  
Changelog: **[RELEASE_NOTES.md](RELEASE_NOTES.md)**.

---

## Architecture note

The root file `Dynamic Island for Windows Lux.wh.cpp` is the Windhawk single-file mod **and** the translation unit linked into the standalone exe. Keep it — it is not leftover junk. Lux is a derivative of [Dynamic Island for Windows](https://github.com/devcode90/Dynamic-Island-for-Windows) by Himanshu (devcode90), MIT. See [NOTICE](NOTICE).

| Windhawk mod | Standalone |
| --- | --- |
| Injected / tool-mod process | Own Win32 exe + tray |
| YAML settings in Windhawk | Flat `settings.json` + schema dialog |
| Windhawk storage API | `%AppData%\DynamicIslandLux\` |

---

## Uninstall

- **Installed:** Settings → Apps → Dynamic Island for Windows Lux, or Start Menu uninstall.  
- **Portable:** Exit from the tray, delete the exe folder.  
- Optional cleanup: `%AppData%\DynamicIslandLux\` and `%LocalAppData%\DynamicIslandLux\`.

---

## Links

- [Releases](https://github.com/Averixor/Dynamic-Island-for-Windows-Lux/releases)
- [Issues](https://github.com/Averixor/Dynamic-Island-for-Windows-Lux/issues)
- [License](LICENSE)
