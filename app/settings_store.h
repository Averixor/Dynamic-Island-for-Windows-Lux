#pragma once

#include <string>

bool SettingsStoreInit();
void SettingsStoreShutdown();
void SettingsStoreReload();
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
