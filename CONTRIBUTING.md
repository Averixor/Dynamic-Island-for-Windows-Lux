# Contributing

## Settings keys

`app/settings_schema.h` (`kSettingSchema`) is the **single source of truth** for user settings.

Any new key must:

1. Be added to `kSettingSchema` (type, default, group, enums if needed).
2. Be read in `.wh.cpp` `LoadSettings()` **only** via `Wh_GetIntSetting` / `GetStringSettingCopy` (→ SettingsStore), never hard-coded `?? 1`.
3. Appear in the settings dialog automatically (schema-driven). Do not add one-off dialog controls that bypass the schema.

If the dialog and `LoadSettings` disagree, the schema was skipped.

## Workers / smoke

Background threads and LL hooks must start only from `WhTool_ModInit` → `StartThreads` (gated by `LuxShouldStartWorkers` / `--no-workers`).

Threads created outside `StartThreads` are invisible to `--smoke-test` Uninit waits — do not add them without extending smoke.

## Legacy body

`Dynamic Island for Windows Lux.wh.cpp` is the island implementation TU (also the Windhawk archive). Keep it; do not delete as junk.

## License

MIT — see `LICENSE`. Original Windhawk mod author: Himanshu (Averixor).
