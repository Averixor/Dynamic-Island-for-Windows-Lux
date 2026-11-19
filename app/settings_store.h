#pragma once

#include <string>

// Flat JSON store under %AppData%\DynamicIslandLux\
//   settings.json — user prefs (schema keys + app.* stamps)
//   runtime.json  — ephemeral worker state
//
// Thread-safe (internal mutex). SettingsSet* only mark dirty when the value
// actually changes; SettingsStoreSaveUser is a no-op when clean so LastWriteTime
// does not jump on OK-without-edits / Cancel.

// ephemeral=true: load into memory for the session, never write settings.json /
// runtime.json (smoke / --test-json / --ephemeral must not mutate user prefs).
bool SettingsStoreInit(bool ephemeral = false);
void SettingsStoreShutdown();
void SettingsStoreReload();
// Persist g_user when dirty. Returns true if disk matches memory (including
// already-clean / ephemeral). False only when a needed write failed.
bool SettingsStoreSaveUser();

// Rebuild the user map from the schema (unknown keys dropped), clear runtime.json
// entirely, and write both files. Returns false if a needed write failed.
bool SettingsStoreResetToDefaults();

// True when settings.json was absent at the last SettingsStoreInit (first launch).
bool SettingsStoreUserFileWasMissing();

// Consistent read view for LoadSettings. Writes on this thread update the snapshot
// so a reload cannot observe a torn mix of old and new keys.
struct SettingsSnapshotToken;
SettingsSnapshotToken* SettingsStoreAcquireSnapshot();
void SettingsStoreReleaseSnapshot(SettingsSnapshotToken* token);

class SettingsSnapshotScope {
 public:
  SettingsSnapshotScope() : token_(SettingsStoreAcquireSnapshot()) {}
  ~SettingsSnapshotScope() { SettingsStoreReleaseSnapshot(token_); }
  SettingsSnapshotScope(const SettingsSnapshotScope&) = delete;
  SettingsSnapshotScope& operator=(const SettingsSnapshotScope&) = delete;

 private:
  SettingsSnapshotToken* token_;
};

int SettingsGetInt(const wchar_t* key, int fallback);
bool SettingsGetBool(const wchar_t* key, bool fallback);
std::wstring SettingsGetString(const wchar_t* key, const wchar_t* fallback);

void SettingsSetInt(const wchar_t* key, int value);
void SettingsSetBool(const wchar_t* key, bool value);
void SettingsSetString(const wchar_t* key, const wchar_t* value);

int RuntimeGetInt(const wchar_t* key, int fallback);
void RuntimeSetInt(const wchar_t* key, int value);
bool RuntimeSave();
void RuntimeSaveDebounced();  // leading save, plus a trailing flush within ~1s

// Self-check for escape/round-trip. Returns true on success.
bool SettingsStoreSelfTestJson();
