#pragma once

#include <string>

// Flat JSON store under %AppData%\DynamicIslandLux\
//   settings.json — user prefs (schema keys + app.* stamps)
//   runtime.json  — ephemeral worker state
//
// Thread-safe (internal mutex). SettingsSet* only mark dirty when the value
// actually changes; SettingsStoreSaveUser is a no-op when clean so LastWriteTime
// does not jump on OK-without-edits / Cancel.

bool SettingsStoreInit();
void SettingsStoreShutdown();
void SettingsStoreReload();
// Persist g_user when dirty. Returns true if disk matches memory (including
// already-clean). False only when a needed write failed.
bool SettingsStoreSaveUser();

int SettingsGetInt(const wchar_t* key, int fallback);
bool SettingsGetBool(const wchar_t* key, bool fallback);
std::wstring SettingsGetString(const wchar_t* key, const wchar_t* fallback);

void SettingsSetInt(const wchar_t* key, int value);
void SettingsSetBool(const wchar_t* key, bool value);
void SettingsSetString(const wchar_t* key, const wchar_t* value);

int RuntimeGetInt(const wchar_t* key, int fallback);
void RuntimeSetInt(const wchar_t* key, int value);
bool RuntimeSave();
void RuntimeSaveDebounced();  // at most ~1 write/sec; call from workers

// Self-check for escape/round-trip. Returns true on success.
bool SettingsStoreSelfTestJson();
