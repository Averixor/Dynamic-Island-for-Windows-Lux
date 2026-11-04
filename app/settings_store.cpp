#include "settings_store.h"

#include "logging.h"
#include "settings_schema.h"

#include <shlobj.h>

#include <atomic>
#include <cwchar>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::mutex g_mutex;
std::map<std::wstring, std::wstring> g_user;
std::map<std::wstring, std::wstring> g_runtime;
std::atomic<ULONGLONG> g_runtimeLastFlushMs{0};
std::atomic<bool> g_runtimeDirty{false};

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
  auto skipWs = [&] {
    while (i < text.size() &&
           (text[i] == ' ' || text[i] == '\t' || text[i] == '\r' || text[i] == '\n')) {
      ++i;
    }
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
        } else if (e == 'u' && i + 4 <= text.size()) {
          unsigned code = 0;
          for (int n = 0; n < 4; ++n) {
            const char h = text[i++];
            code <<= 4;
            if (h >= '0' && h <= '9') {
              code |= static_cast<unsigned>(h - '0');
            } else if (h >= 'a' && h <= 'f') {
              code |= static_cast<unsigned>(h - 'a' + 10);
            } else if (h >= 'A' && h <= 'F') {
              code |= static_cast<unsigned>(h - 'A' + 10);
            }
          }
          if (code < 0x80) {
            acc.push_back(static_cast<char>(code));
          }
        } else {
          acc.push_back(e);
        }
      } else {
        acc.push_back(c);
      }
    }
    return false;
  };

  skipWs();
  if (text.empty()) {
    return true;
  }
  if (i >= text.size() || text[i] != '{') {
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
    bool asBool = false;
    bool asInt = false;
    for (const SettingDef& def : kSettingSchema) {
      if (kv.first == def.key) {
        if (def.type == SettingType::Bool) {
          asBool = true;
        } else if (def.type == SettingType::Int) {
          asInt = true;
        }
        break;
      }
    }
    oss << "  \"" << key << "\": ";
    if (asBool) {
      oss << ((kv.second == L"1" || kv.second == L"true") ? "true" : "false");
    } else if (asInt) {
      oss << Narrow(kv.second.empty() ? L"0" : kv.second);
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
      LUX_LOGE(L"AtomicWrite: CreateFile failed for %s (%lu)", tmp.c_str(), GetLastError());
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
    LUX_LOGE(L"AtomicWrite: MoveFileEx failed for %s (%lu)", path.c_str(), GetLastError());
    DeleteFileW(tmp.c_str());
    return false;
  }
  return true;
}

bool LoadFileMap(const std::wstring& path, std::map<std::wstring, std::wstring>* out) {
  out->clear();
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return false;
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  if (!ParseFlatJsonObject(ss.str(), out)) {
    LUX_LOGW(L"Settings JSON unreadable: %s — using defaults", path.c_str());
    out->clear();
    return false;
  }
  return true;
}

bool SaveFileMap(const std::wstring& path, const std::map<std::wstring, std::wstring>& map) {
  return AtomicWriteFile(path, WriteFlatJsonObject(map));
}

const SettingDef* FindDef(const wchar_t* key) {
  for (const SettingDef& def : kSettingSchema) {
    if (wcscmp(def.key, key) == 0) {
      return &def;
    }
  }
  return nullptr;
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

bool SettingsStoreInit() {
  std::lock_guard lock(g_mutex);
  g_user.clear();
  g_runtime.clear();
  LoadFileMap(UserPath(), &g_user);
  LoadFileMap(RuntimePath(), &g_runtime);

  for (const SettingDef& def : kSettingSchema) {
    if (g_user.find(def.key) == g_user.end() && def.defaultString) {
      g_user[def.key] = def.defaultString;
    }
  }
  const std::wstring ver = LUX_APP_VERSION_STRING;
  if (g_user[L"app.version"] != ver) {
    g_user[L"app.version"] = ver;
    SaveFileMap(UserPath(), g_user);
  }
  return true;
}

void SettingsStoreShutdown() {
  RuntimeSaveDebounced();
  if (g_runtimeDirty.load()) {
    RuntimeSave();
  }
}

void SettingsStoreReload() {
  std::lock_guard lock(g_mutex);
  LoadFileMap(UserPath(), &g_user);
  LoadFileMap(RuntimePath(), &g_runtime);
}

bool SettingsStoreSaveUser() {
  std::lock_guard lock(g_mutex);
  return SaveFileMap(UserPath(), g_user);
}

int SettingsGetInt(const wchar_t* key, int fallback) {
  std::lock_guard lock(g_mutex);
  const std::wstring v = Lookup(g_runtime, g_user, key, nullptr);
  if (v.empty()) {
    return fallback;
  }
  return _wtoi(v.c_str());
}

bool SettingsGetBool(const wchar_t* key, bool fallback) {
  std::lock_guard lock(g_mutex);
  const std::wstring v = Lookup(g_runtime, g_user, key, nullptr);
  if (v.empty()) {
    return fallback;
  }
  return _wtoi(v.c_str()) != 0;
}

std::wstring SettingsGetString(const wchar_t* key, const wchar_t* fallback) {
  std::lock_guard lock(g_mutex);
  return Lookup(g_user, g_runtime, key, fallback);
}

void SettingsSetInt(const wchar_t* key, int value) {
  std::lock_guard lock(g_mutex);
  g_user[key] = std::to_wstring(value);
}

void SettingsSetBool(const wchar_t* key, bool value) {
  std::lock_guard lock(g_mutex);
  g_user[key] = value ? L"1" : L"0";
}

void SettingsSetString(const wchar_t* key, const wchar_t* value) {
  std::lock_guard lock(g_mutex);
  g_user[key] = value ? value : L"";
}

int RuntimeGetInt(const wchar_t* key, int fallback) {
  std::lock_guard lock(g_mutex);
  auto it = g_runtime.find(key);
  if (it == g_runtime.end()) {
    return fallback;
  }
  return _wtoi(it->second.c_str());
}

void RuntimeSetInt(const wchar_t* key, int value) {
  std::lock_guard lock(g_mutex);
  g_runtime[key] = std::to_wstring(value);
  g_runtimeDirty.store(true);
}

bool RuntimeSave() {
  std::lock_guard lock(g_mutex);
  const bool ok = SaveFileMap(RuntimePath(), g_runtime);
  if (ok) {
    g_runtimeDirty.store(false);
    g_runtimeLastFlushMs.store(GetTickCount64());
  }
  return ok;
}

void RuntimeSaveDebounced() {
  if (!g_runtimeDirty.load()) {
    return;
  }
  const ULONGLONG now = GetTickCount64();
  const ULONGLONG last = g_runtimeLastFlushMs.load();
  if (now - last < 1000) {
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
  LUX_LOGI(L"JSON self-test PASS");
  return true;
}
