# Contributing

## Settings keys

`app/settings_schema.h` (`kSettingSchema`) is the **single source of truth** for user settings.

Any new key must:

1. Be added to `kSettingSchema` (type, default, group, enums if needed).
2. Be read in `.wh.cpp` `LoadSettings()` **only** via `Wh_GetIntSetting` / `GetStringSettingCopy` (→ SettingsStore), never hard-coded `?? 1`.
3. Get EN/UK/RU labels in `app/settings_ui_loc.h` (`SettingLabel` / enum labels). Dialog rows are schema-driven — do not add one-off controls that bypass the schema.
4. Treat OS-backed toggles specially when needed (example: `General.LaunchAtStartup` ↔ `app/autostart.*` / HKCU Run).

If the dialog and `LoadSettings` disagree, the schema was skipped.

Settings dialog: **Apply** / **OK** write store + reload (dirty `runtime.json` is flushed first so drag/pin survive). **Apply** refreshes the dialog baseline. **Cancel** restores UI to that baseline and closes (keeps already-applied disk). Escape / title-bar close also leave applied settings. **Reset to defaults** rebuilds settings from the schema, clears runtime.json, and disables autostart.

## Workers / smoke

Background threads and LL hooks must start only from `WhTool_ModInit` → `StartThreads` (gated by `LuxShouldStartWorkers` / `--no-workers`).

Threads created outside `StartThreads` are invisible to `--smoke-test` Uninit waits — do not add them without extending smoke.

```bat
app\build.bat
```

Smoke flags and exit codes: [app/README.md](app/README.md).

## Version & resources

- App version: `project(DynamicIslandLux VERSION …)` in `app/CMakeLists.txt` only. Packaging/installer read it via `scripts/_version.cmd`.
- Do not hand-edit `version.gen.rc` / `resources.gen.rc`. The manifest embedded in the exe is generated from `DynamicIslandLux.manifest.in` (there is no checked-in manifest). Edit the `*.in` templates (`version.rc.in`, `resources.rc.in`, `DynamicIslandLux.manifest.in`) and `app.ico`.

## Spellcheck

UI / loc tokens live in `dictionaries/lux-ui.txt` (cspell). After adding visible strings, run cspell or append unknown words there.

## Legacy body

`Dynamic Island for Windows Lux.wh.cpp` is the island implementation TU (also the Windhawk archive). Keep it; do not delete as junk.

## License

MIT — see [LICENSE](LICENSE) and [NOTICE](NOTICE). Copyright: (c) 2026 devcode90 and Averixor and contributors. Lux derives from Dynamic Island for Windows by Himanshu (devcode90).
