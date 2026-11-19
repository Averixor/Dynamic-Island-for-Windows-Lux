#include "settings_store.h"

#include "logging.h"
#include "settings_schema.h"

#include <shlobj.h>

#include <algorithm>
#include <atomic>
#include <cwchar>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <threadpoolapiset.h>
#include <vector>

struct SettingsSnapshotToken {
  SettingsSnapshotToken* previous = nullptr;
  std::map<std::wstring, std::wstring> user;
  std::map<std::wstring, std::wstring> runtime;
};

namespace {

std::mutex g_mutex;
std::mutex g_writeMutex;
std::map<std::wstring, std::wstring> g_user;
std::map<std::wstring, std::wstring> g_runtime;
std::atomic<ULONGLONG> g_runtimeLastFlushMs{0};
std::atomic<bool> g_runtimeDirty{false};
std::atomic<bool> g_userDirty{false};
std::atomic<bool> g_ephemeral{false};
// Sticky: settings.json existed but could not be opened. Refuse disk writes until a
// successful Reload so Apply/OK cannot overwrite the real file with schema defaults.
std::atomic<bool> g_userDiskUnreadable{false};
std::atomic<bool> g_userFileMissingAtInit{false};
PTP_TIMER g_runtimeFlushTimer = nullptr;
std::once_flag g_runtimeFlushTimerOnce;
thread_local SettingsSnapshotToken* g_tlsSnapshot = nullptr;

std::wstring AppDataDir() {
  wchar_t base[MAX_PATH] = {};
  if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, base))) {
    return {};
  }
  std::wstring dir = base;
  dir += L"\\DynamicIslandLux";
  SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
  return dir;
}

std::wstring UserPath() {
  return AppDataDir() + L"\\settings.json";
}

std::wstring RuntimePath() {
  return AppDataDir() + L"\\runtime.json";
}

std::string Narrow(const std::wstring& w) {
  if (w.empty()) {
    return {};
  }
  const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
  if (n <= 1) {
    return {};
  }
  std::string out(static_cast<size_t>(n - 1), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, out.data(), n, nullptr, nullptr);
  return out;
}

std::wstring Widen(const std::string& s) {
  if (s.empty()) {
    return {};
  }
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
  if (n <= 1) {
    return {};
  }
  std::wstring out(static_cast<size_t>(n - 1), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out.data(), n);
  return out;
}

std::string EscapeJson(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 8);
  for (unsigned char c : s) {
    switch (c) {
    case '\\':
      out += "\\\\";
      break;
    case '"':
      out += "\\\"";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\t':
      out += "\\t";
      break;
    default:
      if (c < 0x20) {
        char buf[8];
        sprintf_s(buf, "\\u%04x", c);
        out += buf;
      } else {
        out.push_back(static_cast<char>(c));
      }
      break;
    }
  }
  return out;
}

bool ParseFlatJsonObject(const std::string& text, std::map<std::wstring, std::wstring>* out) {
  out->clear();
  size_t i = 0;
  // Notepad and some editors write UTF-8 BOM; treat it as whitespace so the
  // file is not marked Corrupt and replaced with schema defaults.
  if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
      static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF) {
    i = 3;
  }
  auto skipWs = [&] {
    while (i < text.size() &&
           (text[i] == ' ' || text[i] == '\t' || text[i] == '\r' || text[i] == '\n')) {
      ++i;
    }
  };
  auto appendUtf8 = [](std::string* acc, unsigned code) -> bool {
    // Reject lone UTF-16 surrogates (would emit invalid UTF-8).
    if (code >= 0xD800 && code <= 0xDFFF) {
      return false;
    }
    if (code > 0x10FFFF) {
      return false;
    }
    if (code < 0x80) {
      acc->push_back(static_cast<char>(code));
    } else if (code < 0x800) {
      acc->push_back(static_cast<char>(0xC0 | (code >> 6)));
      acc->push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else if (code < 0x10000) {
      acc->push_back(static_cast<char>(0xE0 | (code >> 12)));
      acc->push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
      acc->push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else {
      acc->push_back(static_cast<char>(0xF0 | (code >> 18)));
      acc->push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
      acc->push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
      acc->push_back(static_cast<char>(0x80 | (code & 0x3F)));
    }
    return true;
  };
  auto parseHex4 = [&](unsigned* code) -> bool {
    if (i + 4 > text.size()) {
      return false;
    }
    unsigned value = 0;
    for (int n = 0; n < 4; ++n) {
      const char h = text[i++];
      value <<= 4;
      if (h >= '0' && h <= '9') {
        value |= static_cast<unsigned>(h - '0');
      } else if (h >= 'a' && h <= 'f') {
        value |= static_cast<unsigned>(h - 'a' + 10);
      } else if (h >= 'A' && h <= 'F') {
        value |= static_cast<unsigned>(h - 'A' + 10);
      } else {
        return false;
      }
    }
    *code = value;
    return true;
  };
  auto parseString = [&](std::string* value) -> bool {
    if (i >= text.size() || text[i] != '"') {
      return false;
    }
    ++i;
    std::string acc;
    while (i < text.size()) {
      char c = text[i++];
      if (c == '"') {
        *value = acc;
        return true;
      }
      if (c == '\\' && i < text.size()) {
        const char e = text[i++];
        if (e == '"' || e == '\\' || e == '/') {
          acc.push_back(e);
        } else if (e == 'n') {
          acc.push_back('\n');
        } else if (e == 'r') {
          acc.push_back('\r');
        } else if (e == 't') {
          acc.push_back('\t');
        } else if (e == 'b') {
          acc.push_back('\b');
        } else if (e == 'f') {
          acc.push_back('\f');
        } else if (e == 'u') {
          unsigned code = 0;
          if (!parseHex4(&code)) {
            return false;
          }
          // UTF-16 surrogate pair → one Unicode scalar (emoji etc.).
          if (code >= 0xD800 && code <= 0xDBFF) {
            if (i + 6 > text.size() || text[i] != '\\' || text[i + 1] != 'u') {
              return false;
            }
            i += 2;
            unsigned low = 0;
            if (!parseHex4(&low) || low < 0xDC00 || low > 0xDFFF) {
              return false;
            }
            code = 0x10000 + (((code - 0xD800) << 10) | (low - 0xDC00));
          }
          if (!appendUtf8(&acc, code)) {
            return false;
          }
        } else {
          return false;
        }
      } else {
        acc.push_back(c);
      }
    }
    return false;
  };

  skipWs();
  // Empty or whitespace-only files are an empty object, not corruption.
  if (i >= text.size()) {
    return true;
  }
  if (text[i] != '{') {
    return false;
  }
  ++i;
  skipWs();
  if (i < text.size() && text[i] == '}') {
    return true;
  }
  while (i < text.size()) {
    skipWs();
    std::string key;
    if (!parseString(&key)) {
      return false;
    }
    skipWs();
    if (i >= text.size() || text[i] != ':') {
      return false;
    }
    ++i;
    skipWs();
    std::string value;
    if (i < text.size() && text[i] == '"') {
      if (!parseString(&value)) {
        return false;
      }
    } else if (i < text.size() && (text[i] == '{' || text[i] == '[')) {
      // Flat store: skip one nested value instead of rejecting the whole file.
      const char open = text[i];
      const char close = open == '{' ? '}' : ']';
      int depth = 0;
      bool inString = false;
      bool escaped = false;
      bool closed = false;
      for (; i < text.size(); ++i) {
        const char c = text[i];
        if (inString) {
          if (escaped) {
            escaped = false;
            continue;
          }
          if (c == '\\') {
            escaped = true;
            continue;
          }
          if (c == '"') {
            inString = false;
          }
          continue;
        }
        if (c == '"') {
          inString = true;
          continue;
        }
        if (c == open) {
          ++depth;
        } else if (c == close) {
          --depth;
          if (depth == 0) {
            ++i;
            closed = true;
            break;
          }
        }
      }
      if (!closed) {
        return false;
      }
    } else if (i + 4 <= text.size() && text.compare(i, 4, "true") == 0) {
      value = "1";
      i += 4;
    } else if (i + 5 <= text.size() && text.compare(i, 5, "false") == 0) {
      value = "0";
      i += 5;
    } else if (i + 4 <= text.size() && text.compare(i, 4, "null") == 0) {
      value.clear();
      i += 4;
    } else {
      const size_t start = i;
      while (i < text.size() && text[i] != ',' && text[i] != '}' && text[i] != ' ' &&
             text[i] != '\n' && text[i] != '\r' && text[i] != '\t') {
        ++i;
      }
      value = text.substr(start, i - start);
    }
    (*out)[Widen(key)] = Widen(value);
    skipWs();
    if (i < text.size() && text[i] == ',') {
      ++i;
      skipWs();
      // Tolerate trailing commas from hand-edits: {"a":1,}
      if (i < text.size() && text[i] == '}') {
        return true;
      }
      continue;
    }
    if (i < text.size() && text[i] == '}') {
      return true;
    }
    return false;
  }
  return false;
}

std::string WriteFlatJsonObject(const std::map<std::wstring, std::wstring>& map) {
  std::ostringstream oss;
  oss << "{\n";
  bool first = true;
  for (const auto& kv : map) {
    if (!first) {
      oss << ",\n";
    }
    first = false;
    const std::string key = EscapeJson(Narrow(kv.first));
    const SettingDef* matched = nullptr;
    for (const SettingDef& def : kSettingSchema) {
      if (kv.first == def.key) {
        matched = &def;
        break;
      }
    }
    oss << "  \"" << key << "\": ";
    if (matched && matched->type == SettingType::Bool) {
      oss << ((kv.second == L"1" || kv.second == L"true") ? "true" : "false");
    } else if (matched && matched->type == SettingType::Int) {
      // Never emit raw map text — "1,2" would break JSON and trigger corrupt wipe.
      wchar_t* end = nullptr;
      long value = wcstol(kv.second.c_str(), &end, 10);
      while (end && iswspace(*end)) {
        ++end;
      }
      if (kv.second.empty() || end == kv.second.c_str() || (end && *end != L'\0')) {
        value = wcstol(matched->defaultString ? matched->defaultString : L"0", nullptr, 10);
      }
      if (matched->minValue < matched->maxValue) {
        value = (std::max)(static_cast<long>(matched->minValue), value);
        value = (std::min)(static_cast<long>(matched->maxValue), value);
      }
      oss << value;
    } else {
      oss << '"' << EscapeJson(Narrow(kv.second)) << '"';
    }
  }
  oss << "\n}\n";
  return oss.str();
}

bool AtomicWriteFile(const std::wstring& path, const std::string& body) {
  const std::wstring tmp = path + L".tmp";
  {
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
      const DWORD err = GetLastError();
      if (err == ERROR_ACCESS_DENIED || err == ERROR_WRITE_PROTECT) {
        LUX_LOGW(L"AtomicWrite: cannot write %s (access denied %lu) — in-memory only", path.c_str(),
                 err);
      } else {
        LUX_LOGE(L"AtomicWrite: CreateFile failed for %s (%lu)", tmp.c_str(), err);
      }
      return false;
    }
    DWORD written = 0;
    const BOOL ok =
        WriteFile(h, body.data(), static_cast<DWORD>(body.size()), &written, nullptr);
    FlushFileBuffers(h);
    CloseHandle(h);
    if (!ok || written != body.size()) {
      DeleteFileW(tmp.c_str());
      return false;
    }
  }
  if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    const DWORD err = GetLastError();
    DeleteFileW(tmp.c_str());
    if (err == ERROR_ACCESS_DENIED || err == ERROR_WRITE_PROTECT || err == ERROR_SHARING_VIOLATION) {
      LUX_LOGW(L"AtomicWrite: MoveFileEx denied for %s (%lu) — keep in-memory settings", path.c_str(),
               err);
    } else {
      LUX_LOGE(L"AtomicWrite: MoveFileEx failed for %s (%lu)", path.c_str(), err);
    }
    return false;
  }
  return true;
}

enum class JsonLoadResult {
  Missing,
  Ok,
  Corrupt,
  Unreadable,  // file exists but could not be opened (AV lock, sharing) — never overwrite disk
};

JsonLoadResult LoadFileMap(const std::wstring& path, std::map<std::wstring, std::wstring>* out) {
  const DWORD attrs = GetFileAttributesW(path.c_str());
  if (attrs == INVALID_FILE_ATTRIBUTES) {
    out->clear();
    return JsonLoadResult::Missing;
  }
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    // Keep caller's map intact — do not clear and do not rewrite disk.
    LUX_LOGW(L"Settings JSON open failed: %s — leave memory and disk untouched", path.c_str());
    return JsonLoadResult::Unreadable;
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  in.close();
  std::map<std::wstring, std::wstring> parsed;
  if (!ParseFlatJsonObject(ss.str(), &parsed)) {
    out->clear();
    SYSTEMTIME stamp = {};
    GetLocalTime(&stamp);
    wchar_t suffix[40] = {};
    swprintf_s(suffix, L".bad.%04u%02u%02u-%02u%02u%02u", stamp.wYear, stamp.wMonth, stamp.wDay,
               stamp.wHour, stamp.wMinute, stamp.wSecond);
    std::wstring bad = path + suffix;
    if (GetFileAttributesW(bad.c_str()) != INVALID_FILE_ATTRIBUTES) {
      bad += L"-";
      bad += std::to_wstring(GetTickCount64());
    }
    if (MoveFileExW(path.c_str(), bad.c_str(), MOVEFILE_COPY_ALLOWED)) {
      LUX_LOGW(L"Settings JSON corrupt: %s — moved to %s, restoring defaults", path.c_str(),
               bad.c_str());
    } else {
      LUX_LOGW(L"Settings JSON corrupt: %s — using defaults (could not rename to .bad, %lu)",
               path.c_str(), GetLastError());
    }
    return JsonLoadResult::Corrupt;
  }
  *out = std::move(parsed);
  return JsonLoadResult::Ok;
}

const SettingDef* FindDef(const wchar_t* key) {
  for (const SettingDef& def : kSettingSchema) {
    if (wcscmp(def.key, key) == 0) {
      return &def;
    }
  }
  return nullptr;
}

void NoteSnapshotUser(const wchar_t* key, const std::wstring& value) {
  if (g_tlsSnapshot && key) {
    g_tlsSnapshot->user[key] = value;
  }
}

void NoteSnapshotRuntime(const wchar_t* key, const std::wstring& value) {
  if (g_tlsSnapshot && key) {
    g_tlsSnapshot->runtime[key] = value;
  }
}

void CALLBACK RuntimeFlushTimerCallback(PTP_CALLBACK_INSTANCE, PVOID, PTP_TIMER) {
  if (g_ephemeral.load() || !g_runtimeDirty.load()) {
    return;
  }
  RuntimeSave();
}

void EnsureRuntimeFlushTimer() {
  std::call_once(g_runtimeFlushTimerOnce, [] {
    g_runtimeFlushTimer = CreateThreadpoolTimer(RuntimeFlushTimerCallback, nullptr, nullptr);
  });
}

void CancelRuntimeFlushTimer() {
  if (!g_runtimeFlushTimer) {
    return;
  }
  SetThreadpoolTimer(g_runtimeFlushTimer, nullptr, 0, 0);
  WaitForThreadpoolTimerCallbacks(g_runtimeFlushTimer, TRUE);
}

void ScheduleRuntimeFlush(DWORD delayMs) {
  EnsureRuntimeFlushTimer();
  if (!g_runtimeFlushTimer) {
    return;
  }
  if (delayMs == 0) {
    delayMs = 1;
  }
  LARGE_INTEGER due = {};
  due.QuadPart = -static_cast<LONGLONG>(delayMs) * 10000LL;
  FILETIME when = {};
  when.dwLowDateTime = static_cast<DWORD>(due.LowPart);
  when.dwHighDateTime = static_cast<DWORD>(due.HighPart);
  SetThreadpoolTimer(g_runtimeFlushTimer, &when, 0, 50);
}

bool WriteMap(const std::wstring& path, const std::map<std::wstring, std::wstring>& map) {
  return AtomicWriteFile(path, WriteFlatJsonObject(map));
}

std::wstring Lookup(const std::map<std::wstring, std::wstring>& primary,
                    const std::map<std::wstring, std::wstring>& secondary, const wchar_t* key,
                    const wchar_t* fallback) {
  auto it = primary.find(key);
  if (it != primary.end()) {
    return it->second;
  }
  it = secondary.find(key);
  if (it != secondary.end()) {
    return it->second;
  }
  if (const SettingDef* def = FindDef(key)) {
    return def->defaultString ? def->defaultString : (fallback ? fallback : L"");
  }
  return fallback ? fallback : L"";
}

}  // namespace

bool SettingsStoreInit(bool ephemeral) {
  g_ephemeral.store(ephemeral);
  std::unique_lock lock(g_mutex);
  g_user.clear();
  g_runtime.clear();
  const JsonLoadResult userLoad = LoadFileMap(UserPath(), &g_user);
  const JsonLoadResult runtimeLoad = LoadFileMap(RuntimePath(), &g_runtime);
  g_userFileMissingAtInit.store(userLoad == JsonLoadResult::Missing);
  bool runtimePurged = false;
  if (runtimeLoad == JsonLoadResult::Ok) {
    // Menu writers for these overrides were removed. A leftover value would
    // permanently hide the settings dialog choice.
    const wchar_t* kOrphanRuntimeKeys[] = {
        L"AdaptiveThemeOverride", L"AuraStyleOverride",     L"BackdropMaterialOverride",
        L"PillOpacityOverride",   L"MediaTabLyricsOverride", L"MediaTabLyricsOverrideBase",
        L"W11StyleOverride",      L"NotchStyleOverride",     L"GameOverlayPinned",
        L"CollapsedLyrics",       L"LyricsPinnedFirst",
    };
    for (const wchar_t* key : kOrphanRuntimeKeys) {
      if (g_runtime.erase(key) > 0) {
        runtimePurged = true;
      }
    }
  }
  if (runtimePurged) {
    g_runtimeDirty.store(true);
  }

  bool schemaFilled = false;
  for (const SettingDef& def : kSettingSchema) {
    if (g_user.find(def.key) == g_user.end() && def.defaultString) {
      g_user[def.key] = def.defaultString;
      schemaFilled = true;
    }
  }
  // One-shot only: set ULW default if the key is absent. Never overwrite an explicit user choice.
  {
    auto mig = g_user.find(L"app.migrate.ulwDefault");
    if (mig == g_user.end() || mig->second != L"1") {
      auto it = g_user.find(L"Appearance.UseDirectComposition");
      if (it == g_user.end()) {
        g_user[L"Appearance.UseDirectComposition"] = L"0";
      }
      g_user[L"app.migrate.ulwDefault"] = L"1";
      schemaFilled = true;
    }
  }
  // One-shot: adopt WheelStep=2 as the new product default (was 5).
  // Only fill when the key is absent — never overwrite an explicit user value.
  {
    auto mig = g_user.find(L"app.migrate.wheelStep2");
    if (mig == g_user.end() || mig->second != L"1") {
      if (g_user.find(L"Behavior.WheelStep") == g_user.end()) {
        g_user[L"Behavior.WheelStep"] = L"2";
      }
      g_user[L"app.migrate.wheelStep2"] = L"1";
      schemaFilled = true;
    }
  }
  // One-shot: Appearance used to default to acrylic and Themes to none, so the
  // Themes row never applied. New defaults are Appearance=auto + Themes=acrylic.
  // Rewrite only the exact legacy pair so intentional acrylic/mica/solid stays.
  {
    auto mig = g_user.find(L"app.migrate.backdropAuto");
    if (mig == g_user.end() || mig->second != L"1") {
      auto appearance = g_user.find(L"Appearance.BackdropMaterial");
      auto themes = g_user.find(L"Themes.BackdropMaterial");
      if (appearance != g_user.end() && themes != g_user.end() &&
          _wcsicmp(appearance->second.c_str(), L"acrylic") == 0 &&
          _wcsicmp(themes->second.c_str(), L"none") == 0) {
        appearance->second = L"auto";
        themes->second = L"acrylic";
        schemaFilled = true;
      }
      g_user[L"app.migrate.backdropAuto"] = L"1";
      schemaFilled = true;
    }
  }
  const std::wstring ver = LUX_APP_VERSION_STRING;
  const bool versionBump = g_user[L"app.version"] != ver;
  if (versionBump) {
    // Bump version stamp only — do not reset any user preference keys.
    g_user[L"app.version"] = ver;
  }
  // Unreadable on cold start: map holds schema defaults only. Never write them over
  // a locked settings.json — sticky flag blocks SaveUser until Reload succeeds.
  if (userLoad == JsonLoadResult::Unreadable) {
    LUX_LOGE(L"settings.json unreadable — disk left unchanged (in-memory defaults only)");
    g_userDiskUnreadable.store(true);
    g_userDirty.store(false);
    return true;
  }
  g_userDiskUnreadable.store(false);
  std::map<std::wstring, std::wstring> userToWrite;
  std::map<std::wstring, std::wstring> runtimeToWrite;
  bool writeUser = false;
  bool writeRuntime = false;
  // First launch / corrupt recovery / schema growth / one-shot migration: persist.
  if (!g_ephemeral.load() &&
      (userLoad != JsonLoadResult::Ok || versionBump || schemaFilled)) {
    userToWrite = g_user;
    writeUser = true;
    g_userDirty.store(false);
  } else {
    g_userDirty.store(false);
  }
  if (!g_ephemeral.load() && runtimePurged) {
    runtimeToWrite = g_runtime;
    writeRuntime = true;
    g_runtimeDirty.store(false);
  }
  lock.unlock();
  if (writeUser) {
    if (WriteMap(UserPath(), userToWrite)) {
      g_userDirty.store(false);
    } else {
      g_userDirty.store(true);
      if (userLoad == JsonLoadResult::Corrupt) {
        LUX_LOGW(L"Could not rewrite settings.json after corrupt recovery");
      }
    }
  }
  if (writeRuntime && !WriteMap(RuntimePath(), runtimeToWrite)) {
    g_runtimeDirty.store(true);
  }
  return true;
}

void SettingsStoreShutdown() {
  CancelRuntimeFlushTimer();
  if (!g_ephemeral.load()) {
    if (g_runtimeDirty.load()) {
      RuntimeSave();
    }
    std::map<std::wstring, std::wstring> userCopy;
    bool writeUser = false;
    {
      std::lock_guard lock(g_mutex);
      if (g_userDirty.load() && !g_userDiskUnreadable.load()) {
        userCopy = g_user;
        writeUser = true;
        g_userDirty.store(false);
      } else if (g_userDiskUnreadable.load() && g_userDirty.load()) {
        LUX_LOGW(L"SettingsStoreShutdown: skip user write — settings.json unreadable");
      }
    }
    if (writeUser && !WriteMap(UserPath(), userCopy)) {
      g_userDirty.store(true);
    }
  } else {
    g_userDirty.store(false);
    g_runtimeDirty.store(false);
  }
  if (g_runtimeFlushTimer) {
    CloseThreadpoolTimer(g_runtimeFlushTimer);
    g_runtimeFlushTimer = nullptr;
  }
}

void SettingsStoreReload() {
  // OK/Apply reloads the store after writing settings.json. Flush dirty runtime
  // first so LoadFileMap cannot wipe drag offsets / pins still in the debounce window.
  if (g_runtimeDirty.load()) {
    RuntimeSave();
  }
  std::map<std::wstring, std::wstring> userCopy;
  bool writeUser = false;
  {
    std::lock_guard lock(g_mutex);
    const JsonLoadResult userLoad = LoadFileMap(UserPath(), &g_user);
    if (userLoad == JsonLoadResult::Unreadable) {
      LUX_LOGW(L"SettingsStoreReload: settings.json unreadable — keep in-memory map");
      g_userDiskUnreadable.store(true);
      return;
    }
    g_userDiskUnreadable.store(false);
    bool filled = false;
    for (const SettingDef& def : kSettingSchema) {
      if (g_user.find(def.key) == g_user.end() && def.defaultString) {
        g_user[def.key] = def.defaultString;
        filled = true;
      }
    }
    if (!g_ephemeral.load() && (userLoad == JsonLoadResult::Corrupt || filled)) {
      userCopy = g_user;
      writeUser = true;
      g_userDirty.store(false);
    } else {
      g_userDirty.store(false);
    }
    if (!g_runtimeDirty.load()) {
      const JsonLoadResult runtimeLoad = LoadFileMap(RuntimePath(), &g_runtime);
      if (runtimeLoad == JsonLoadResult::Unreadable) {
        LUX_LOGW(L"SettingsStoreReload: runtime.json unreadable — keep in-memory runtime");
      }
    }
  }
  if (writeUser && !WriteMap(UserPath(), userCopy)) {
    g_userDirty.store(true);
  }
}

bool SettingsStoreSaveUser() {
  if (g_ephemeral.load()) {
    g_userDirty.store(false);
    return true;
  }
  if (g_userDiskUnreadable.load()) {
    LUX_LOGE(L"SettingsStoreSaveUser: refused — settings.json still unreadable");
    return false;
  }
  std::lock_guard writeLock(g_writeMutex);
  std::map<std::wstring, std::wstring> userCopy;
  {
    std::lock_guard lock(g_mutex);
    if (!g_userDirty.load()) {
      return true;
    }
    userCopy = g_user;
    g_userDirty.store(false);
  }
  if (!WriteMap(UserPath(), userCopy)) {
    g_userDirty.store(true);
    return false;
  }
  return true;
}

bool SettingsStoreResetToDefaults() {
  {
    std::lock_guard lock(g_mutex);
    std::map<std::wstring, std::wstring> fresh;
    for (const SettingDef& def : kSettingSchema) {
      if (!def.defaultString) {
        continue;
      }
      if (wcscmp(def.key, L"app.version") == 0) {
        fresh[def.key] = LUX_APP_VERSION_STRING;
      } else {
        fresh[def.key] = def.defaultString;
      }
    }
    // Keep one-shot migrations marked done so Init does not rewrite defaults again.
    fresh[L"app.migrate.ulwDefault"] = L"1";
    fresh[L"app.migrate.wheelStep2"] = L"1";
    fresh[L"app.migrate.backdropAuto"] = L"1";
    g_user.swap(fresh);
    g_userDirty.store(true);
    g_runtime.clear();
    g_runtimeDirty.store(true);
    if (g_tlsSnapshot) {
      g_tlsSnapshot->user = g_user;
      g_tlsSnapshot->runtime.clear();
    }
  }
  const bool userOk = SettingsStoreSaveUser();
  const bool runtimeOk = RuntimeSave();
  return userOk && runtimeOk;
}

bool SettingsStoreUserFileWasMissing() {
  return g_userFileMissingAtInit.load();
}

SettingsSnapshotToken* SettingsStoreAcquireSnapshot() {
  auto* token = new SettingsSnapshotToken();
  {
    std::lock_guard lock(g_mutex);
    token->user = g_user;
    token->runtime = g_runtime;
  }
  token->previous = g_tlsSnapshot;
  g_tlsSnapshot = token;
  return token;
}

void SettingsStoreReleaseSnapshot(SettingsSnapshotToken* token) {
  if (!token) {
    return;
  }
  if (g_tlsSnapshot == token) {
    g_tlsSnapshot = token->previous;
  }
  delete token;
}

int SettingsGetInt(const wchar_t* key, int fallback) {
  if (g_tlsSnapshot) {
    const std::wstring v = Lookup(g_tlsSnapshot->user, g_tlsSnapshot->runtime, key, nullptr);
    if (v.empty()) {
      return fallback;
    }
    return _wtoi(v.c_str());
  }
  std::lock_guard lock(g_mutex);
  // User settings.json always wins over runtime ephemeral values.
  const std::wstring v = Lookup(g_user, g_runtime, key, nullptr);
  if (v.empty()) {
    return fallback;
  }
  return _wtoi(v.c_str());
}

bool SettingsGetBool(const wchar_t* key, bool fallback) {
  if (g_tlsSnapshot) {
    const std::wstring v = Lookup(g_tlsSnapshot->user, g_tlsSnapshot->runtime, key, nullptr);
    if (v.empty()) {
      return fallback;
    }
    return _wtoi(v.c_str()) != 0;
  }
  std::lock_guard lock(g_mutex);
  const std::wstring v = Lookup(g_user, g_runtime, key, nullptr);
  if (v.empty()) {
    return fallback;
  }
  return _wtoi(v.c_str()) != 0;
}

std::wstring SettingsGetString(const wchar_t* key, const wchar_t* fallback) {
  if (g_tlsSnapshot) {
    return Lookup(g_tlsSnapshot->user, g_tlsSnapshot->runtime, key, fallback);
  }
  std::lock_guard lock(g_mutex);
  return Lookup(g_user, g_runtime, key, fallback);
}

void SettingsSetInt(const wchar_t* key, int value) {
  const std::wstring next = std::to_wstring(value);
  std::lock_guard lock(g_mutex);
  auto it = g_user.find(key);
  if (it != g_user.end() && it->second == next) {
    return;
  }
  g_user[key] = next;
  NoteSnapshotUser(key, next);
  g_userDirty.store(true);
}

void SettingsSetBool(const wchar_t* key, bool value) {
  const wchar_t* next = value ? L"1" : L"0";
  std::lock_guard lock(g_mutex);
  auto it = g_user.find(key);
  if (it != g_user.end() && it->second == next) {
    return;
  }
  g_user[key] = next;
  NoteSnapshotUser(key, next);
  g_userDirty.store(true);
}

void SettingsSetString(const wchar_t* key, const wchar_t* value) {
  const wchar_t* next = value ? value : L"";
  std::lock_guard lock(g_mutex);
  auto it = g_user.find(key);
  if (it != g_user.end() && it->second == next) {
    return;
  }
  g_user[key] = next;
  NoteSnapshotUser(key, next);
  g_userDirty.store(true);
}

int RuntimeGetInt(const wchar_t* key, int fallback) {
  if (g_tlsSnapshot) {
    auto it = g_tlsSnapshot->runtime.find(key ? key : L"");
    if (it == g_tlsSnapshot->runtime.end()) {
      return fallback;
    }
    return _wtoi(it->second.c_str());
  }
  std::lock_guard lock(g_mutex);
  auto it = g_runtime.find(key);
  if (it == g_runtime.end()) {
    return fallback;
  }
  return _wtoi(it->second.c_str());
}

void RuntimeSetInt(const wchar_t* key, int value) {
  const std::wstring next = std::to_wstring(value);
  std::lock_guard lock(g_mutex);
  g_runtime[key] = next;
  NoteSnapshotRuntime(key, next);
  g_runtimeDirty.store(true);
}

bool RuntimeSave() {
  if (g_ephemeral.load()) {
    g_runtimeDirty.store(false);
    return true;
  }
  std::lock_guard writeLock(g_writeMutex);
  std::map<std::wstring, std::wstring> copy;
  {
    std::lock_guard lock(g_mutex);
    copy = g_runtime;
    g_runtimeDirty.store(false);
  }
  if (!WriteMap(RuntimePath(), copy)) {
    g_runtimeDirty.store(true);
    return false;
  }
  g_runtimeLastFlushMs.store(GetTickCount64());
  return true;
}

void RuntimeSaveDebounced() {
  if (g_ephemeral.load()) {
    g_runtimeDirty.store(false);
    return;
  }
  if (!g_runtimeDirty.load()) {
    return;
  }
  const ULONGLONG now = GetTickCount64();
  const ULONGLONG last = g_runtimeLastFlushMs.load();
  // Leading edge when the last flush is older than 1s. Changes inside that
  // window stay dirty and a thread-pool timer writes them on the trailing edge.
  if (last != 0 && now - last < 1000) {
    ScheduleRuntimeFlush(static_cast<DWORD>(1000 - (now - last)));
    return;
  }
  RuntimeSave();
}

bool SettingsStoreSelfTestJson() {
  std::map<std::wstring, std::wstring> sample;
  sample[L"Modules.MediaExpandBlocklist"] = L"C:\\Music\\\"Weird\"\\path\nline";
  sample[L"Themes.BackdropMaterial"] = L"acrylic";
  sample[L"Modules.QuickLookup"] = L"1";
  sample[L"Themes.BackdropTint"] = L"55";
  const std::string json = WriteFlatJsonObject(sample);
  std::map<std::wstring, std::wstring> round;
  if (!ParseFlatJsonObject(json, &round)) {
    LUX_LOGE(L"JSON self-test: parse failed");
    return false;
  }
  if (round[L"Modules.MediaExpandBlocklist"] != sample[L"Modules.MediaExpandBlocklist"]) {
    LUX_LOGE(L"JSON self-test: escape round-trip failed");
    return false;
  }
  if (round[L"Themes.BackdropMaterial"] != L"acrylic") {
    LUX_LOGE(L"JSON self-test: string value failed");
    return false;
  }
  const std::string bomJson = std::string("\xEF\xBB\xBF") + json;
  std::map<std::wstring, std::wstring> bomRound;
  if (!ParseFlatJsonObject(bomJson, &bomRound) ||
      bomRound[L"Themes.BackdropMaterial"] != L"acrylic") {
    LUX_LOGE(L"JSON self-test: UTF-8 BOM parse failed");
    return false;
  }
  const std::string unicodeJson = "{\"Themes.FontFamily\":\"\\u0410\\u0431\"}";
  std::map<std::wstring, std::wstring> unicodeRound;
  if (!ParseFlatJsonObject(unicodeJson, &unicodeRound) ||
      unicodeRound[L"Themes.FontFamily"] != L"\x0410\x0431") {
    LUX_LOGE(L"JSON self-test: \\u escape parse failed");
    return false;
  }
  // U+1F600 😀 as UTF-16 surrogate pair in JSON.
  const std::string emojiJson = "{\"Themes.FontFamily\":\"\\uD83D\\uDE00\"}";
  std::map<std::wstring, std::wstring> emojiRound;
  if (!ParseFlatJsonObject(emojiJson, &emojiRound) ||
      emojiRound[L"Themes.FontFamily"] != L"\xD83D\xDE00") {
    LUX_LOGE(L"JSON self-test: surrogate pair \\u parse failed");
    return false;
  }
  if (ParseFlatJsonObject("{\"k\":\"\\uD800\"}", &emojiRound)) {
    LUX_LOGE(L"JSON self-test: lone high surrogate should fail");
    return false;
  }
  std::map<std::wstring, std::wstring> escapeRound;
  if (!ParseFlatJsonObject("{\"k\":\"a\\b\\f\"}", &escapeRound) || escapeRound[L"k"] != L"a\b\f") {
    LUX_LOGE(L"JSON self-test: \\\\b \\\\f parse failed");
    return false;
  }
  std::map<std::wstring, std::wstring> blankRound;
  if (!ParseFlatJsonObject(" \r\n\t ", &blankRound) || !blankRound.empty()) {
    LUX_LOGE(L"JSON self-test: whitespace-only file should be empty");
    return false;
  }
  std::map<std::wstring, std::wstring> nestedRound;
  if (!ParseFlatJsonObject("{\"nested\":{\"a\":1},\"Themes.BackdropMaterial\":\"acrylic\"}",
                           &nestedRound) ||
      nestedRound[L"Themes.BackdropMaterial"] != L"acrylic") {
    LUX_LOGE(L"JSON self-test: nested value should be skipped");
    return false;
  }
  LUX_LOGI(L"JSON self-test PASS");
  return true;
}
