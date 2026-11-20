#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#endif

#include "media_like_bridge.h"
#include "media_desktop_rate.h"

#ifdef _WIN32
#include "logging.h"
#endif

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace {

using WakeFn = void (*)();
std::atomic<WakeFn> g_wake{nullptr};

#ifdef _WIN32
void NotifyUi() {
  if (WakeFn fn = g_wake.load(std::memory_order_acquire)) {
    fn();
  }
}
#endif

std::string TrimAscii(const std::string& value) {
  size_t begin = 0;
  while (begin < value.size() && (value[begin] == ' ' || value[begin] == '\t')) {
    ++begin;
  }
  size_t end = value.size();
  while (end > begin && (value[end - 1] == ' ' || value[end - 1] == '\t' || value[end - 1] == '\r')) {
    --end;
  }
  return value.substr(begin, end - begin);
}

std::string LowerAscii(std::string value) {
  for (char& ch : value) {
    if (ch >= 'A' && ch <= 'Z') {
      ch = static_cast<char>(ch - 'A' + 'a');
    }
  }
  return value;
}

bool IsDigit(char ch) {
  return ch >= '0' && ch <= '9';
}

bool IsIdChar(char ch) {
  return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || IsDigit(ch) || ch == '.' ||
         ch == '_' || ch == '-';
}

void AppendUtf8(std::string* out, uint32_t codePoint) {
  if (!out) {
    return;
  }
  if (codePoint > 0x10FFFF || (codePoint >= 0xD800 && codePoint <= 0xDFFF)) {
    codePoint = 0xFFFD;
  }
  if (codePoint < 0x80) {
    out->push_back(static_cast<char>(codePoint));
  } else if (codePoint < 0x800) {
    out->push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
    out->push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
  } else if (codePoint < 0x10000) {
    out->push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
    out->push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
    out->push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
  } else {
    out->push_back(static_cast<char>(0xF0 | (codePoint >> 18)));
    out->push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
    out->push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
    out->push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
  }
}

std::string StripDecorations(std::string text) {
  for (;;) {
    const size_t open = text.find_first_of("([");
    if (open == std::string::npos) {
      break;
    }
    const char close = text[open] == '(' ? ')' : ']';
    const size_t end = text.find(close, open + 1);
    if (end == std::string::npos) {
      break;
    }
    text.erase(open, end - open + 1);
  }
  return text;
}

std::string NormalizeMatch(const std::string& raw) {
  std::string text = LowerAscii(StripDecorations(raw));
  std::string out;
  out.reserve(text.size());
  bool pendingSpace = false;
  for (unsigned char ch : text) {
    const bool keep = (ch >= 'a' && ch <= 'z') || IsDigit(static_cast<char>(ch)) || ch >= 0x80;
    if (keep) {
      if (pendingSpace && !out.empty()) {
        out.push_back(' ');
      }
      pendingSpace = false;
      out.push_back(static_cast<char>(ch));
    } else if (!out.empty()) {
      pendingSpace = true;
    }
  }
  return out;
}

int Similarity(const std::string& leftRaw, const std::string& rightRaw) {
  const std::string left = NormalizeMatch(leftRaw);
  const std::string right = NormalizeMatch(rightRaw);
  if (left.empty() || right.empty()) {
    return 0;
  }
  if (left == right) {
    return 100;
  }
  int score = 0;
  const std::string& shorter = left.size() <= right.size() ? left : right;
  const std::string& longer = left.size() <= right.size() ? right : left;
  if (shorter.size() >= 4 && shorter.size() * 2 >= longer.size() &&
      longer.find(shorter) != std::string::npos) {
    score = 86;
  }
  auto tokensOf = [](const std::string& text) {
    std::vector<std::string> tokens;
    size_t index = 0;
    while (index < text.size()) {
      while (index < text.size() && text[index] == ' ') {
        ++index;
      }
      const size_t start = index;
      while (index < text.size() && text[index] != ' ') {
        ++index;
      }
      if (index > start && index - start >= 2) {
        tokens.push_back(text.substr(start, index - start));
      }
    }
    std::sort(tokens.begin(), tokens.end());
    tokens.erase(std::unique(tokens.begin(), tokens.end()), tokens.end());
    return tokens;
  };
  const std::vector<std::string> leftTokens = tokensOf(left);
  const std::vector<std::string> rightTokens = tokensOf(right);
  if (!leftTokens.empty() && !rightTokens.empty()) {
    size_t shared = 0;
    size_t i = 0;
    size_t j = 0;
    while (i < leftTokens.size() && j < rightTokens.size()) {
      if (leftTokens[i] == rightTokens[j]) {
        ++shared;
        ++i;
        ++j;
      } else if (leftTokens[i] < rightTokens[j]) {
        ++i;
      } else {
        ++j;
      }
    }
    const size_t uni = leftTokens.size() + rightTokens.size() - shared;
    if (uni > 0) {
      const int jaccard = static_cast<int>((shared * 100) / uni);
      if (jaccard > score) {
        score = jaccard;
      }
    }
  }
  return score;
}

bool HasWord(const std::string& lower, const char* word) {
  const std::string padded = std::string(" ") + lower + " ";
  const std::string key = std::string(" ") + word + " ";
  return padded.find(key) != std::string::npos;
}

bool IsBrowserSource(const std::string& lower) {
  return HasWord(lower, "chrome") || HasWord(lower, "chromium") || HasWord(lower, "msedge") ||
         HasWord(lower, "edge") || HasWord(lower, "firefox") || HasWord(lower, "brave") ||
         HasWord(lower, "opera") || HasWord(lower, "vivaldi");
}

bool SourceOpensBridge(const std::string& sourceRaw) {
  if (TrimAscii(sourceRaw).empty()) {
    return false;
  }
  const std::string source = LowerAscii(sourceRaw);
  if (source.find("youtube") != std::string::npos) {
    return true;
  }
  return IsBrowserSource(source);
}

int SiteAffinity(BridgeSite site, const std::string& sourceRaw) {
  const std::string source = LowerAscii(sourceRaw);
  const bool youtubeMusic =
      source.find("youtube music") != std::string::npos || source.find("music.youtube") != std::string::npos;
  const bool youtube = !youtubeMusic && source.find("youtube") != std::string::npos;
  if (youtubeMusic) {
    return site == BridgeSite::YouTubeMusic ? 30 : 6;
  }
  if (youtube) {
    return site == BridgeSite::YouTube ? 28 : 10;
  }
  if (IsBrowserSource(source)) {
    return 8;
  }
  return 0;
}

bool AcceptMatch(int titleScore, int artistScore, const std::string& hintArtist,
                 const std::string& tabArtist, const std::string& source, BridgeSite site) {
  if (!SourceOpensBridge(source)) {
    return false;
  }
  if (!NormalizeMatch(hintArtist).empty() && !NormalizeMatch(tabArtist).empty() && artistScore < 40) {
    return false;
  }
  if (titleScore >= 86) {
    return true;
  }
  if (titleScore >= 70 && artistScore >= 40) {
    return true;
  }
  if (titleScore >= 55 && SiteAffinity(site, source) >= 20) {
    return true;
  }
  return false;
}

int RankMatch(const TabState& tab, const SmtcHint& hint, int titleScore, int artistScore) {
  int score = titleScore * 3 + artistScore;
  if (tab.playing && hint.playing) {
    score += 40;
  } else if (tab.playing) {
    score += 12;
  } else if (!hint.playing) {
    score += 8;
  }
  score += SiteAffinity(tab.site, hint.source);
  if (tab.canRate) {
    score += 4;
  }
  return score;
}

bool ExtensionOrigin(const std::string& origin) {
  const std::string trimmed = TrimAscii(origin);
  if (trimmed.empty() || trimmed.size() > 180) {
    return false;
  }
  const std::string lower = LowerAscii(trimmed);
  const char* prefixes[] = {"chrome-extension://", "moz-extension://", "extension://"};
  const char* matched = nullptr;
  for (const char* prefix : prefixes) {
    const size_t len = std::strlen(prefix);
    if (lower.compare(0, len, prefix) == 0) {
      matched = prefix;
      break;
    }
  }
  if (!matched) {
    return false;
  }
  const size_t idStart = std::strlen(matched);
  if (trimmed.size() <= idStart) {
    return false;
  }
  const size_t idLen = trimmed.size() - idStart;
  if (idLen < 8 || idLen > 128) {
    return false;
  }
  for (size_t i = idStart; i < trimmed.size(); ++i) {
    if (!IsIdChar(trimmed[i])) {
      return false;
    }
  }
  return true;
}

bool LoopbackHost(const std::string& host, int listenPort) {
  if (listenPort < kMediaLikeBridgeMinPort || listenPort > kMediaLikeBridgeMaxPort) {
    return false;
  }
  const std::string trimmed = TrimAscii(host);
  const size_t colon = trimmed.rfind(':');
  if (colon == std::string::npos || colon == 0 || colon + 1 >= trimmed.size()) {
    return false;
  }
  const std::string name = LowerAscii(trimmed.substr(0, colon));
  const std::string port = trimmed.substr(colon + 1);
  if (name != "127.0.0.1" && name != "localhost") {
    return false;
  }
  if (name.find(':') != std::string::npos) {
    return false;
  }
  char expected[16] = {};
  std::snprintf(expected, sizeof(expected), "%d", listenPort);
  return port == expected;
}

class JsonParser {
 public:
  explicit JsonParser(const std::string& text) : text_(text) {}

  bool ParseUpdate(ParsedTabUpdate* out) {
    if (!out) {
      return false;
    }
    SkipWs();
    if (Consume('\xEF')) {
      if (!Consume('\xBB') || !Consume('\xBF')) {
        return false;
      }
      SkipWs();
    }
    if (!Consume('{')) {
      return false;
    }
    bool sawTab = false;
    bool sawSite = false;
    bool sawLike = false;
    SkipWs();
    if (Consume('}')) {
      return false;
    }
    for (;;) {
      std::string key;
      if (!ParseString(&key)) {
        return false;
      }
      SkipWs();
      if (!Consume(':')) {
        return false;
      }
      SkipWs();
      if (key == "tabId") {
        if (!ParseString(&out->state.tabId) || out->state.tabId.empty() || out->state.tabId.size() > 80) {
          return false;
        }
        sawTab = true;
      } else if (key == "site") {
        std::string site;
        if (!ParseString(&site)) {
          return false;
        }
        site = LowerAscii(site);
        if (site == "youtube") {
          out->state.site = BridgeSite::YouTube;
        } else if (site == "youtube_music") {
          out->state.site = BridgeSite::YouTubeMusic;
        } else {
          return false;
        }
        sawSite = true;
      } else if (key == "videoId") {
        if (!ParseString(&out->state.videoId)) {
          return false;
        }
        if (out->state.videoId.size() > 64) {
          out->state.videoId.resize(64);
        }
      } else if (key == "title") {
        if (!ParseString(&out->state.title)) {
          return false;
        }
        if (out->state.title.size() > 500) {
          out->state.title.resize(500);
        }
      } else if (key == "artist") {
        if (!ParseString(&out->state.artist)) {
          return false;
        }
        if (out->state.artist.size() > 500) {
          out->state.artist.resize(500);
        }
      } else if (key == "playing") {
        if (!ParseBool(&out->state.playing)) {
          return false;
        }
      } else if (key == "likeState") {
        std::string like;
        if (!ParseString(&like)) {
          return false;
        }
        like = LowerAscii(like);
        if (like == "none" || like == "indifferent") {
          out->state.like = BridgeLikeState::None;
        } else if (like == "liked" || like == "like") {
          out->state.like = BridgeLikeState::Liked;
        } else if (like == "disliked" || like == "dislike") {
          out->state.like = BridgeLikeState::Disliked;
        } else {
          return false;
        }
        sawLike = true;
      } else if (key == "canRate") {
        if (!ParseBool(&out->state.canRate)) {
          return false;
        }
      } else if (key == "timestamp") {
        if (!ParseInt64(&out->state.clientTimestamp)) {
          return false;
        }
      } else if (key == "ack") {
        if (!ParseAck(&out->ack)) {
          return false;
        }
      } else if (!SkipValue(0)) {
        return false;
      }
      SkipWs();
      if (Consume('}')) {
        break;
      }
      if (!Consume(',')) {
        return false;
      }
      SkipWs();
    }
    SkipWs();
    return sawTab && sawSite && sawLike && index_ == text_.size();
  }

 private:
  const std::string& text_;
  size_t index_ = 0;

  bool Has(size_t count) const { return index_ + count <= text_.size(); }

  char Peek() const { return index_ < text_.size() ? text_[index_] : '\0'; }

  bool Consume(char ch) {
    if (Peek() != ch) {
      return false;
    }
    ++index_;
    return true;
  }

  void SkipWs() {
    while (Peek() == ' ' || Peek() == '\t' || Peek() == '\r' || Peek() == '\n') {
      ++index_;
    }
  }

  bool ParseString(std::string* out) {
    if (!out || !Consume('"')) {
      return false;
    }
    out->clear();
    while (Has(1)) {
      const char ch = text_[index_++];
      if (ch == '"') {
        return true;
      }
      if (ch == '\\') {
        if (!Has(1)) {
          return false;
        }
        const char esc = text_[index_++];
        switch (esc) {
        case '"':
        case '\\':
        case '/':
          out->push_back(esc);
          break;
        case 'b':
          out->push_back('\b');
          break;
        case 'f':
          out->push_back('\f');
          break;
        case 'n':
          out->push_back('\n');
          break;
        case 'r':
          out->push_back('\r');
          break;
        case 't':
          out->push_back('\t');
          break;
        case 'u':
          if (!ParseUnicodeEscape(out)) {
            return false;
          }
          break;
        default:
          return false;
        }
      } else if (static_cast<unsigned char>(ch) < 0x20) {
        return false;
      } else {
        out->push_back(ch);
      }
      if (out->size() > 8192) {
        return false;
      }
    }
    return false;
  }

  bool ParseHex4(uint32_t* out) {
    if (!out || !Has(4)) {
      return false;
    }
    uint32_t value = 0;
    for (int i = 0; i < 4; ++i) {
      const char ch = text_[index_++];
      value <<= 4;
      if (ch >= '0' && ch <= '9') {
        value |= static_cast<uint32_t>(ch - '0');
      } else if (ch >= 'a' && ch <= 'f') {
        value |= static_cast<uint32_t>(ch - 'a' + 10);
      } else if (ch >= 'A' && ch <= 'F') {
        value |= static_cast<uint32_t>(ch - 'A' + 10);
      } else {
        return false;
      }
    }
    *out = value;
    return true;
  }

  bool ParseUnicodeEscape(std::string* out) {
    uint32_t unit = 0;
    if (!ParseHex4(&unit)) {
      return false;
    }
    if (unit >= 0xD800 && unit <= 0xDBFF) {
      if (Has(2) && text_[index_] == '\\' && text_[index_ + 1] == 'u') {
        index_ += 2;
        uint32_t low = 0;
        if (!ParseHex4(&low)) {
          return false;
        }
        if (low < 0xDC00 || low > 0xDFFF) {
          AppendUtf8(out, 0xFFFD);
          return true;
        }
        const uint32_t cp = 0x10000 + (((unit - 0xD800) << 10) | (low - 0xDC00));
        AppendUtf8(out, cp);
        return true;
      }
      AppendUtf8(out, 0xFFFD);
      return true;
    }
    if (unit >= 0xDC00 && unit <= 0xDFFF) {
      AppendUtf8(out, 0xFFFD);
      return true;
    }
    AppendUtf8(out, unit);
    return true;
  }

  bool ParseBool(bool* out) {
    if (!out) {
      return false;
    }
    if (Consume('t')) {
      if (Consume('r') && Consume('u') && Consume('e')) {
        *out = true;
        return true;
      }
      return false;
    }
    if (Consume('f')) {
      if (Consume('a') && Consume('l') && Consume('s') && Consume('e')) {
        *out = false;
        return true;
      }
      return false;
    }
    int64_t number = 0;
    if (!ParseInt64(&number)) {
      return false;
    }
    if (number != 0 && number != 1) {
      return false;
    }
    *out = number == 1;
    return true;
  }

  bool ParseInt64(int64_t* out) {
    if (!out) {
      return false;
    }
    const bool neg = Consume('-');
    if (!IsDigit(Peek())) {
      return false;
    }
    int64_t value = 0;
    while (IsDigit(Peek())) {
      const int digit = Peek() - '0';
      if (value > (9223372036854775807LL - digit) / 10) {
        return false;
      }
      value = value * 10 + digit;
      ++index_;
    }
    if (Peek() == '.' || Peek() == 'e' || Peek() == 'E') {
      return false;
    }
    *out = neg ? -value : value;
    return true;
  }

  bool ParseAck(std::vector<uint64_t>* out) {
    if (!out || !Consume('[')) {
      return false;
    }
    out->clear();
    SkipWs();
    if (Consume(']')) {
      return true;
    }
    for (;;) {
      int64_t number = 0;
      if (!ParseInt64(&number) || number < 0) {
        return false;
      }
      if (out->size() < 16) {
        out->push_back(static_cast<uint64_t>(number));
      }
      SkipWs();
      if (Consume(']')) {
        return true;
      }
      if (!Consume(',')) {
        return false;
      }
      SkipWs();
    }
  }

  bool SkipValue(int depth) {
    if (depth > 8) {
      return false;
    }
    if (Peek() == '"') {
      std::string ignored;
      return ParseString(&ignored);
    }
    if (Peek() == '{') {
      ++index_;
      SkipWs();
      if (Consume('}')) {
        return true;
      }
      for (;;) {
        std::string ignored;
        if (!ParseString(&ignored)) {
          return false;
        }
        SkipWs();
        if (!Consume(':') || !SkipValue(depth + 1)) {
          return false;
        }
        SkipWs();
        if (Consume('}')) {
          return true;
        }
        if (!Consume(',')) {
          return false;
        }
        SkipWs();
      }
    }
    if (Peek() == '[') {
      ++index_;
      SkipWs();
      if (Consume(']')) {
        return true;
      }
      for (;;) {
        if (!SkipValue(depth + 1)) {
          return false;
        }
        SkipWs();
        if (Consume(']')) {
          return true;
        }
        if (!Consume(',')) {
          return false;
        }
        SkipWs();
      }
    }
    if (Peek() == 't' || Peek() == 'f') {
      bool ignored = false;
      return ParseBool(&ignored);
    }
    if (Peek() == 'n') {
      return Consume('n') && Consume('u') && Consume('l') && Consume('l');
    }
    if (Peek() == '-' || IsDigit(Peek())) {
      int64_t ignored = 0;
      return ParseInt64(&ignored);
    }
    return false;
  }
};

bool ParseContentLength(const std::string& value, int* out) {
  if (!out || value.empty() || value.size() > 8) {
    return false;
  }
  int number = 0;
  for (char ch : value) {
    if (!IsDigit(ch)) {
      return false;
    }
    number = number * 10 + (ch - '0');
  }
  if (number > 32768) {
    return false;
  }
  *out = number;
  return true;
}

#ifdef _WIN32
std::string HttpResponse(int code, const char* reason, const std::string& body,
                         const std::string& allowOrigin, bool options) {
  std::string out = "HTTP/1.1 " + std::to_string(code) + " " + reason + "\r\n";
  out += "Cache-Control: no-store\r\n";
  out += "Connection: close\r\n";
  if (!allowOrigin.empty()) {
    out += "Access-Control-Allow-Origin: " + allowOrigin + "\r\n";
    out += "Vary: Origin\r\n";
    out += "Access-Control-Allow-Private-Network: true\r\n";
  }
  if (options) {
    out += "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
    out += "Access-Control-Allow-Headers: Content-Type, X-Dynamic-Island-Lux\r\n";
    out += "Access-Control-Max-Age: 600\r\n";
    out += "Content-Length: 0\r\n\r\n";
    return out;
  }
  out += "Content-Type: application/json; charset=utf-8\r\n";
  out += "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n";
  out += body;
  return out;
}

constexpr int kMaxHeaderBytes = 8192;
constexpr int kMaxBodyBytes = 32768;

std::atomic<bool> g_stop{false};
std::atomic<int> g_port{kMediaLikeBridgeDefaultPort};
std::atomic<SOCKET> g_listen{INVALID_SOCKET};
std::atomic<int> g_wsaRefs{0};
HANDLE g_thread = nullptr;
std::mutex g_life;
TabRegistry g_registry;

void WsaAcquire() {
  if (g_wsaRefs.fetch_add(1) == 0) {
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
      g_wsaRefs.store(0);
      LUX_LOGE(L"media-like-bridge: WSAStartup failed (%d)", WSAGetLastError());
    }
  }
}

void WsaRelease() {
  const int left = g_wsaRefs.fetch_sub(1) - 1;
  if (left == 0) {
    WSACleanup();
  }
}

std::string WideToUtf8(const std::wstring& value) {
  if (value.empty()) {
    return {};
  }
  const int bytes = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()),
                                        nullptr, 0, nullptr, nullptr);
  if (bytes <= 0) {
    return {};
  }
  std::string out(static_cast<size_t>(bytes), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), out.data(), bytes,
                      nullptr, nullptr);
  return out;
}

bool SendAll(SOCKET sock, const std::string& bytes) {
  size_t sent = 0;
  while (sent < bytes.size()) {
    const int chunk = static_cast<int>(std::min<size_t>(bytes.size() - sent, 16 * 1024));
    const int wrote = send(sock, bytes.data() + sent, chunk, 0);
    if (wrote <= 0) {
      return false;
    }
    sent += static_cast<size_t>(wrote);
  }
  return true;
}

std::string HandleState(const HttpHeaderSet& headers, const std::string& body, int port) {
  const BridgeAccess access =
      EvaluateBridgeAccess(headers.host, headers.origin, headers.bridgeHeader, port);
  if (access == BridgeAccess::Reject) {
    return HttpResponse(403, "Forbidden", "{\"ok\":false,\"error\":\"forbidden\"}", "", false);
  }
  const std::string allow = access == BridgeAccess::AllowExtension ? TrimAscii(headers.origin) : "";
  if (headers.method == "OPTIONS") {
    return HttpResponse(204, "No Content", "", allow, true);
  }
  if (headers.method == "GET" && headers.path == "/v1/health") {
    return HttpResponse(200, "OK", "{\"ok\":true,\"service\":\"dynamic-island-lux\"}", allow, false);
  }
  if (headers.method == "POST" && headers.path == "/v1/state") {
    if (!headers.contentLengthPresent) {
      return HttpResponse(411, "Length Required", "{\"ok\":false,\"error\":\"length\"}", allow, false);
    }
    if (headers.chunked) {
      return HttpResponse(400, "Bad Request", "{\"ok\":false,\"error\":\"chunked\"}", allow, false);
    }
    ParsedTabUpdate update;
    if (!ParseTabStateJson(body, &update)) {
      return HttpResponse(400, "Bad Request", "{\"ok\":false,\"error\":\"bad_state\"}", allow, false);
    }
    const std::string tabId = update.state.tabId;
    const uint64_t now = GetTickCount64();
    g_registry.Ack(tabId, update.ack);
    if (g_registry.Upsert(std::move(update.state), now)) {
      NotifyUi();
    }
    return HttpResponse(200, "OK", FormatCommandsJson(g_registry.Peek(tabId)), allow, false);
  }
  if (headers.chunked) {
    return HttpResponse(400, "Bad Request", "{\"ok\":false,\"error\":\"chunked\"}", allow, false);
  }
  if (headers.method != "GET" && headers.method != "POST" && headers.method != "OPTIONS") {
    return HttpResponse(405, "Method Not Allowed", "{\"ok\":false,\"error\":\"method\"}", allow,
                        false);
  }
  return HttpResponse(404, "Not Found", "{\"ok\":false,\"error\":\"not_found\"}", allow, false);
}

void ServeClient(SOCKET client, int port) {
  u_long blocking = 0;
  ioctlsocket(client, FIONBIO, &blocking);
  const DWORD timeoutMs = 1200;
  setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeoutMs),
             sizeof(timeoutMs));
  setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeoutMs),
             sizeof(timeoutMs));
  const BOOL noDelay = TRUE;
  setsockopt(client, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay), sizeof(noDelay));

  std::string buffer;
  while (buffer.find("\r\n\r\n") == std::string::npos) {
    if (g_stop.load(std::memory_order_acquire) || buffer.size() > static_cast<size_t>(kMaxHeaderBytes)) {
      return;
    }
    char chunk[1024];
    const int read = recv(client, chunk, static_cast<int>(sizeof(chunk)), 0);
    if (read <= 0) {
      return;
    }
    buffer.append(chunk, chunk + read);
  }
  const size_t split = buffer.find("\r\n\r\n");
  HttpHeaderSet headers;
  if (!ParseHttpHeaderBlock(buffer.substr(0, split), &headers) || !headers.ok) {
    SendAll(client, HttpResponse(400, "Bad Request", "{\"ok\":false,\"error\":\"bad_request\"}", "",
                                 false));
    return;
  }
  if (headers.contentLength > kMaxBodyBytes) {
    const BridgeAccess access =
        EvaluateBridgeAccess(headers.host, headers.origin, headers.bridgeHeader, port);
    const std::string allow = access == BridgeAccess::AllowExtension ? TrimAscii(headers.origin) : "";
    SendAll(client, HttpResponse(413, "Payload Too Large", "{\"ok\":false,\"error\":\"too_large\"}",
                                 allow, false));
    return;
  }
  std::string body = buffer.substr(split + 4);
  if (headers.contentLengthPresent) {
    while (body.size() < static_cast<size_t>(headers.contentLength)) {
      if (g_stop.load(std::memory_order_acquire)) {
        return;
      }
      char chunk[1024];
      const int read = recv(client, chunk, static_cast<int>(sizeof(chunk)), 0);
      if (read <= 0) {
        return;
      }
      body.append(chunk, chunk + read);
      if (body.size() > static_cast<size_t>(kMaxBodyBytes)) {
        return;
      }
    }
    body.resize(static_cast<size_t>(headers.contentLength));
  }
  if (!SendAll(client, HandleState(headers, body, port))) {
    return;
  }
  shutdown(client, SD_SEND);
}

bool OpenListenSocket(int port, SOCKET* out) {
  if (!out) {
    return false;
  }
  *out = INVALID_SOCKET;
  const SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (sock == INVALID_SOCKET) {
    return false;
  }
  BOOL reuse = TRUE;
  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons(static_cast<u_short>(port));
  if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || listen(sock, 8) != 0) {
    closesocket(sock);
    return false;
  }
  sockaddr_in bound{};
  int boundLen = sizeof(bound);
  if (getsockname(sock, reinterpret_cast<sockaddr*>(&bound), &boundLen) != 0 ||
      bound.sin_addr.s_addr != htonl(INADDR_LOOPBACK)) {
    closesocket(sock);
    return false;
  }
  u_long nonBlocking = 1;
  ioctlsocket(sock, FIONBIO, &nonBlocking);
  *out = sock;
  return true;
}

void ServeListenSocket(SOCKET listenSock, int port) {
  while (!g_stop.load(std::memory_order_acquire)) {
    fd_set readable;
    FD_ZERO(&readable);
    FD_SET(listenSock, &readable);
    timeval wait{};
    wait.tv_sec = 0;
    wait.tv_usec = 200000;
    const int ready = select(0, &readable, nullptr, nullptr, &wait);
    if (g_stop.load(std::memory_order_acquire)) {
      break;
    }
    if (ready == SOCKET_ERROR) {
      break;
    }
    if (g_registry.ExpireStale(GetTickCount64(), kMediaLikeBridgeStaleMs) > 0) {
      NotifyUi();
    }
    if (ready == 0 || !FD_ISSET(listenSock, &readable)) {
      continue;
    }
    for (;;) {
      sockaddr_in peer{};
      int peerLen = sizeof(peer);
      const SOCKET client = accept(listenSock, reinterpret_cast<sockaddr*>(&peer), &peerLen);
      if (client == INVALID_SOCKET) {
        break;
      }
      if (peer.sin_family != AF_INET || peer.sin_addr.s_addr != htonl(INADDR_LOOPBACK)) {
        closesocket(client);
        continue;
      }
      ServeClient(client, port);
      closesocket(client);
      if (g_stop.load(std::memory_order_acquire)) {
        break;
      }
    }
  }
}

DWORD WINAPI BridgeThreadProc(LPVOID) {
  int loggedFailures = 0;
  while (!g_stop.load(std::memory_order_acquire)) {
    const int port = g_port.load(std::memory_order_acquire);
    SOCKET listenSock = INVALID_SOCKET;
    if (!OpenListenSocket(port, &listenSock)) {
      if (loggedFailures % 40 == 0) {
        LUX_LOGW(L"media-like-bridge: bind 127.0.0.1:%d failed (%d)", port, WSAGetLastError());
      }
      ++loggedFailures;
      for (int i = 0; i < 10 && !g_stop.load(std::memory_order_acquire); ++i) {
        Sleep(50);
      }
      continue;
    }
    loggedFailures = 0;
    g_listen.store(listenSock);
    LUX_LOGI(L"media-like-bridge: listening on 127.0.0.1:%d", port);
    ServeListenSocket(listenSock, port);
    const SOCKET mine = g_listen.exchange(INVALID_SOCKET);
    if (mine != INVALID_SOCKET) {
      closesocket(mine);
    }
  }
  return 0;
}

void StopLocked() {
  DesktopRateStop();
  g_stop.store(true, std::memory_order_release);
  const SOCKET listenSock = g_listen.exchange(INVALID_SOCKET);
  if (listenSock != INVALID_SOCKET) {
    closesocket(listenSock);
  }
  if (g_thread) {
    const DWORD wait = WaitForSingleObject(g_thread, 3000);
    if (wait == WAIT_TIMEOUT) {
      LUX_LOGE(L"media-like-bridge: listener did not stop within 3000 ms");
      return;
    }
    CloseHandle(g_thread);
    g_thread = nullptr;
  }
  if (g_wsaRefs.load() > 0) {
    WsaRelease();
  }
  g_stop.store(false, std::memory_order_release);
}

void StartLocked(int port) {
  DesktopRateStart();
  if (g_thread) {
    return;
  }
  WsaAcquire();
  if (g_wsaRefs.load() <= 0) {
    return;
  }
  g_port.store(port, std::memory_order_release);
  g_stop.store(false, std::memory_order_release);
  g_thread = CreateThread(nullptr, 0, BridgeThreadProc, nullptr, 0, nullptr);
  if (!g_thread) {
    LUX_LOGE(L"media-like-bridge: CreateThread failed (%lu)", GetLastError());
    WsaRelease();
  }
}
#endif

}  // namespace

void MediaLikeBridgeSetWake(void (*wake)()) { g_wake.store(wake, std::memory_order_release); }

void MediaRateNotifyUi() {
#ifdef _WIN32
  NotifyUi();
#endif
}

namespace {

struct RateClickFeedback {
  uint64_t generation = 0;
  uint64_t until = 0;
  RateAction action = RateAction::Like;
  RateFeedback phase = RateFeedback::None;
};

std::mutex g_feedbackMu;
RateClickFeedback g_feedback;

void ApplyRateFeedback(MediaLikeBridgeView* view) {
  if (!view) {
    return;
  }
  std::lock_guard<std::mutex> lock(g_feedbackMu);
  const uint64_t now = GetTickCount64();
  if (g_feedback.phase != RateFeedback::None && now > g_feedback.until) {
    g_feedback.phase = RateFeedback::None;
  }
  if (!view->connected || g_feedback.phase == RateFeedback::None) {
    return;
  }
  if (g_feedback.phase == RateFeedback::Pending) {
    const BridgeLikeState target = OptimisticRate(view->like, g_feedback.action);
    if (view->like == target) {
      g_feedback.phase = RateFeedback::None;
      return;
    }
    view->like = target;
  }
  view->feedback = g_feedback.phase;
  view->feedbackAction = g_feedback.action;
}

ParsedLike BridgeLikeToParsed(BridgeLikeState like) {
  switch (like) {
  case BridgeLikeState::Liked:
    return ParsedLike::Liked;
  case BridgeLikeState::Disliked:
    return ParsedLike::Disliked;
  case BridgeLikeState::None:
    return ParsedLike::None;
  }
  const BridgeLikeState unhandled = like;
  (void)unhandled;
  return ParsedLike::Unknown;
}

struct ResolvedRate {
  MediaLikeBridgeView view;
  DesktopRateBackend backend = DesktopRateBackend::None;
  SmtcHint hint;
  std::string aumid;
};

ResolvedRate ResolveMediaRate(const std::wstring& title, const std::wstring& artist,
                              const std::wstring& source, bool playing,
                              const std::wstring& appUserModelId) {
  ResolvedRate resolved;
  resolved.hint.title = WideToUtf8(title);
  resolved.hint.artist = WideToUtf8(artist);
  resolved.hint.source = WideToUtf8(source);
  resolved.hint.playing = playing;
  resolved.aumid = WideToUtf8(appUserModelId);
  const SourceRoute route = ClassifyPlaybackSource(resolved.hint.source, resolved.aumid);
  TabState matched;
  const bool browserMatch =
      route.allowBrowserBridge &&
      g_registry.BestMatch(resolved.hint, GetTickCount64(), kMediaLikeBridgeStaleMs, &matched);
  DesktopRateNoteSession(resolved.hint, resolved.aumid, browserMatch && !route.preferDesktopApi);
  BackendOffer offer;
  if (browserMatch) {
    offer.browserMatch = true;
    offer.browserCanLike = matched.canRate;
    offer.browserCanDislike = matched.canRate;
    offer.browserLike = BridgeLikeToParsed(matched.like);
  }
  BackendOffer desktop;
  if (DesktopRateReadOffer(&desktop)) {
    offer.pearReachable = desktop.pearReachable;
    offer.pearTrackMatch = desktop.pearTrackMatch;
    offer.pearCanLike = desktop.pearCanLike;
    offer.pearCanDislike = desktop.pearCanDislike;
    offer.pearLike = desktop.pearLike;
    offer.ytmReachable = desktop.ytmReachable;
    offer.ytmTrackMatch = desktop.ytmTrackMatch;
    offer.ytmCanLike = desktop.ytmCanLike;
    offer.ytmCanDislike = desktop.ytmCanDislike;
    offer.ytmLike = desktop.ytmLike;
    offer.ytmLegacy = desktop.ytmLegacy;
    offer.uiaReady = desktop.uiaReady;
    offer.uiaCanLike = desktop.uiaCanLike;
    offer.uiaCanDislike = desktop.uiaCanDislike;
    offer.uiaLike = desktop.uiaLike;
  }
  const BackendChoice choice = ChooseRateBackend(route, offer);
  resolved.backend = choice.backend;
  if (choice.backend == DesktopRateBackend::None) {
    resolved.view.reserveButtons = route.allowBrowserBridge;
    return resolved;
  }
  resolved.view.connected = true;
  resolved.view.canLike = choice.canLike;
  resolved.view.canDislike = choice.canDislike;
  resolved.view.canRate = choice.canLike || choice.canDislike;
  resolved.view.like = ParsedLikeToBridge(choice.like);
  ApplyRateFeedback(&resolved.view);
  return resolved;
}

}  // namespace

uint64_t MediaRateNotePending(RateAction action) {
#ifdef _WIN32
  std::lock_guard<std::mutex> lock(g_feedbackMu);
  ++g_feedback.generation;
  if (g_feedback.generation == 0) {
    g_feedback.generation = 1;
  }
  g_feedback.phase = RateFeedback::Pending;
  g_feedback.action = action;
  g_feedback.until = GetTickCount64() + 2500;
  return g_feedback.generation;
#else
  (void)action;
  return 0;
#endif
}

void MediaRateNoteResult(uint64_t generation, bool ok, RateAction action) {
#ifdef _WIN32
  std::lock_guard<std::mutex> lock(g_feedbackMu);
  if (generation == 0 || generation != g_feedback.generation) {
    return;
  }
  if (ok) {
    g_feedback.phase = RateFeedback::None;
    return;
  }
  g_feedback.phase = RateFeedback::Error;
  g_feedback.action = action;
  g_feedback.until = GetTickCount64() + 1600;
#else
  (void)generation;
  (void)ok;
  (void)action;
#endif
}

const char* RateActionJson(RateAction action) {
  switch (action) {
  case RateAction::Like:
    return "like";
  case RateAction::Dislike:
    return "dislike";
  }
  const RateAction unhandled = action;
  (void)unhandled;
  return "like";
}

std::string EscapeJson(const std::string& value) {
  std::string out;
  out.reserve(value.size());
  for (unsigned char ch : value) {
    switch (ch) {
    case '"':
      out += "\\\"";
      break;
    case '\\':
      out += "\\\\";
      break;
    case '\b':
      out += "\\b";
      break;
    case '\f':
      out += "\\f";
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
      if (ch < 0x20) {
        char hex[8] = {};
        std::snprintf(hex, sizeof(hex), "\\u%04x", ch);
        out += hex;
      } else {
        out.push_back(static_cast<char>(ch));
      }
      break;
    }
  }
  return out;
}

std::string FormatCommandsJson(const std::vector<QueuedCommand>& commands) {
  std::string out = "{\"ok\":true,\"service\":\"dynamic-island-lux\",\"commands\":[";
  for (size_t i = 0; i < commands.size(); ++i) {
    if (i != 0) {
      out.push_back(',');
    }
    out += "{\"id\":";
    out += std::to_string(commands[i].id);
    out += ",\"action\":\"";
    out += RateActionJson(commands[i].action);
    out += "\",\"videoId\":\"";
    out += EscapeJson(commands[i].videoId);
    out += "\"}";
  }
  out += "]}";
  return out;
}

BridgeAccess EvaluateBridgeAccess(const std::string& host, const std::string& origin,
                                  const std::string& bridgeHeader, int listenPort) {
  if (!LoopbackHost(host, listenPort)) {
    return BridgeAccess::Reject;
  }
  const std::string trimmedOrigin = TrimAscii(origin);
  if (ExtensionOrigin(trimmedOrigin)) {
    return BridgeAccess::AllowExtension;
  }
  if (!trimmedOrigin.empty()) {
    return BridgeAccess::Reject;
  }
  if (TrimAscii(bridgeHeader) == "1") {
    return BridgeAccess::AllowLoopbackHeader;
  }
  return BridgeAccess::Reject;
}

bool ParseHttpHeaderBlock(const std::string& block, HttpHeaderSet* out) {
  if (!out) {
    return false;
  }
  *out = HttpHeaderSet{};
  std::string text = block;
  const size_t sepRn = text.find("\r\n\r\n");
  if (sepRn != std::string::npos) {
    text.resize(sepRn);
  } else {
    const size_t sepN = text.find("\n\n");
    if (sepN != std::string::npos) {
      text.resize(sepN);
    }
  }
  std::string normalized;
  normalized.reserve(text.size());
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '\r') {
      continue;
    }
    normalized.push_back(text[i]);
  }
  std::vector<std::string> lines;
  size_t cursor = 0;
  while (cursor <= normalized.size()) {
    const size_t nl = normalized.find('\n', cursor);
    if (nl == std::string::npos) {
      lines.push_back(normalized.substr(cursor));
      break;
    }
    lines.push_back(normalized.substr(cursor, nl - cursor));
    cursor = nl + 1;
    if (cursor == normalized.size()) {
      break;
    }
  }
  if (lines.empty() || lines[0].empty()) {
    return false;
  }
  const std::string& request = lines[0];
  const size_t sp1 = request.find(' ');
  const size_t sp2 = sp1 == std::string::npos ? std::string::npos : request.find(' ', sp1 + 1);
  if (sp1 == std::string::npos || sp2 == std::string::npos || request.find(' ', sp2 + 1) != std::string::npos) {
    return false;
  }
  out->method = request.substr(0, sp1);
  out->path = request.substr(sp1 + 1, sp2 - sp1 - 1);
  const std::string version = request.substr(sp2 + 1);
  if (version != "HTTP/1.1" && version != "HTTP/1.0") {
    return false;
  }
  const size_t query = out->path.find('?');
  if (query != std::string::npos) {
    out->path.resize(query);
  }
  if (out->method.empty() || out->path.empty() || out->path[0] != '/') {
    return false;
  }
  for (size_t i = 1; i < lines.size(); ++i) {
    if (lines[i].empty()) {
      continue;
    }
    const size_t colon = lines[i].find(':');
    if (colon == std::string::npos || colon == 0) {
      return false;
    }
    const std::string name = LowerAscii(TrimAscii(lines[i].substr(0, colon)));
    const std::string value = TrimAscii(lines[i].substr(colon + 1));
    if (name == "host" && out->host.empty()) {
      out->host = value;
    } else if (name == "origin" && out->origin.empty()) {
      out->origin = value;
    } else if (name == "x-dynamic-island-lux" && out->bridgeHeader.empty()) {
      out->bridgeHeader = value;
    } else if (name == "content-length" && !out->contentLengthPresent) {
      out->contentLengthPresent = true;
      if (!ParseContentLength(value, &out->contentLength)) {
        return false;
      }
    } else if (name == "transfer-encoding" && LowerAscii(value).find("chunked") != std::string::npos) {
      out->chunked = true;
    }
  }
  out->ok = true;
  return true;
}

bool ParseTabStateJson(const std::string& json, ParsedTabUpdate* out) {
  if (!out) {
    return false;
  }
  *out = ParsedTabUpdate{};
  JsonParser parser(json);
  return parser.ParseUpdate(out);
}

bool TabRegistry::Stale(const Slot& slot, uint64_t nowTick, uint64_t staleMs) const {
  if (nowTick < slot.state.updatedTick) {
    return false;
  }
  return nowTick - slot.state.updatedTick > staleMs;
}

int TabRegistry::BestIndexLocked(const SmtcHint& hint, uint64_t nowTick, uint64_t staleMs) const {
  int best = -1;
  int bestScore = -1;
  uint64_t bestTick = 0;
  for (size_t i = 0; i < tabs_.size(); ++i) {
    const Slot& slot = tabs_[i];
    if (Stale(slot, nowTick, staleMs)) {
      continue;
    }
    const int titleScore = Similarity(slot.state.title, hint.title);
    const int artistScore = Similarity(slot.state.artist, hint.artist);
    if (!AcceptMatch(titleScore, artistScore, hint.artist, slot.state.artist, hint.source,
                     slot.state.site)) {
      continue;
    }
    const int score = RankMatch(slot.state, hint, titleScore, artistScore);
    if (best < 0 || score > bestScore ||
        (score == bestScore && slot.state.updatedTick > bestTick)) {
      best = static_cast<int>(i);
      bestScore = score;
      bestTick = slot.state.updatedTick;
    }
  }
  return best;
}

bool TabRegistry::Upsert(TabState state, uint64_t nowTick) {
  std::lock_guard<std::mutex> lock(mutex_);
  state.updatedTick = nowTick;
  for (Slot& slot : tabs_) {
    if (slot.state.tabId != state.tabId) {
      continue;
    }
    if (state.clientTimestamp > 0 && slot.state.clientTimestamp > 0 &&
        state.clientTimestamp < slot.state.clientTimestamp) {
      return false;
    }
    const bool videoChanged = !slot.state.videoId.empty() && !state.videoId.empty() &&
                              slot.state.videoId != state.videoId;
    const bool changed = slot.state.title != state.title || slot.state.artist != state.artist ||
                         slot.state.playing != state.playing || slot.state.like != state.like ||
                         slot.state.canRate != state.canRate || slot.state.site != state.site ||
                         videoChanged;
    auto commands = std::move(slot.commands);
    if (videoChanged) {
      commands.clear();
    }
    slot.state = std::move(state);
    slot.commands = std::move(commands);
    return changed;
  }
  if (tabs_.size() >= 16) {
    size_t oldest = 0;
    for (size_t i = 1; i < tabs_.size(); ++i) {
      if (tabs_[i].state.updatedTick < tabs_[oldest].state.updatedTick) {
        oldest = i;
      }
    }
    tabs_.erase(tabs_.begin() + static_cast<std::ptrdiff_t>(oldest));
  }
  Slot created;
  created.state = std::move(state);
  tabs_.push_back(std::move(created));
  return true;
}

int TabRegistry::ExpireStale(uint64_t nowTick, uint64_t staleMs) {
  std::lock_guard<std::mutex> lock(mutex_);
  const size_t before = tabs_.size();
  tabs_.erase(std::remove_if(tabs_.begin(), tabs_.end(),
                             [&](const Slot& slot) { return Stale(slot, nowTick, staleMs); }),
              tabs_.end());
  return static_cast<int>(before - tabs_.size());
}

int TabRegistry::size() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return static_cast<int>(tabs_.size());
}

bool TabRegistry::BestMatch(const SmtcHint& hint, uint64_t nowTick, uint64_t staleMs,
                            TabState* out) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const int index = BestIndexLocked(hint, nowTick, staleMs);
  if (index < 0 || !out) {
    return false;
  }
  *out = tabs_[static_cast<size_t>(index)].state;
  return true;
}

bool TabRegistry::EnqueueBest(const SmtcHint& hint, RateAction action, uint64_t nowTick,
                              uint64_t staleMs, std::string* tabIdOut, QueuedCommand* commandOut) {
  std::lock_guard<std::mutex> lock(mutex_);
  const int index = BestIndexLocked(hint, nowTick, staleMs);
  if (index < 0) {
    return false;
  }
  Slot& slot = tabs_[static_cast<size_t>(index)];
  if (!slot.state.canRate) {
    return false;
  }
  if (slot.commands.size() >= 8) {
    slot.commands.erase(slot.commands.begin());
  }
  QueuedCommand command;
  command.id = nextCommandId_++;
  if (nextCommandId_ == 0) {
    nextCommandId_ = 1;
  }
  command.action = action;
  command.videoId = slot.state.videoId;
  slot.commands.push_back(command);
  if (tabIdOut) {
    *tabIdOut = slot.state.tabId;
  }
  if (commandOut) {
    *commandOut = command;
  }
  return true;
}

std::vector<QueuedCommand> TabRegistry::Peek(const std::string& tabId) const {
  std::lock_guard<std::mutex> lock(mutex_);
  for (const Slot& slot : tabs_) {
    if (slot.state.tabId == tabId) {
      return slot.commands;
    }
  }
  return {};
}

void TabRegistry::Ack(const std::string& tabId, const std::vector<uint64_t>& ids) {
  if (ids.empty()) {
    return;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  for (Slot& slot : tabs_) {
    if (slot.state.tabId != tabId) {
      continue;
    }
    slot.commands.erase(std::remove_if(slot.commands.begin(), slot.commands.end(),
                                       [&](const QueuedCommand& command) {
                                         return std::find(ids.begin(), ids.end(), command.id) !=
                                                ids.end();
                                       }),
                        slot.commands.end());
    return;
  }
}

void MediaLikeBridgeApply(bool enabled, int port) {
#ifdef _WIN32
  if (port < kMediaLikeBridgeMinPort || port > kMediaLikeBridgeMaxPort) {
    port = kMediaLikeBridgeDefaultPort;
  }
  std::lock_guard<std::mutex> lock(g_life);
  if (!enabled) {
    StopLocked();
    return;
  }
  if (g_thread && g_port.load(std::memory_order_acquire) == port &&
      !g_stop.load(std::memory_order_acquire)) {
    return;
  }
  StopLocked();
  StartLocked(port);
#else
  (void)enabled;
  (void)port;
#endif
}

void MediaLikeBridgeStop() {
#ifdef _WIN32
  std::lock_guard<std::mutex> lock(g_life);
  StopLocked();
#endif
}

MediaLikeBridgeView MediaLikeBridgeQuery(const std::wstring& title, const std::wstring& artist,
                                         const std::wstring& source, bool playing,
                                         const std::wstring& appUserModelId) {
#ifdef _WIN32
  return ResolveMediaRate(title, artist, source, playing, appUserModelId).view;
#else
  (void)title;
  (void)artist;
  (void)source;
  (void)playing;
  (void)appUserModelId;
  return {};
#endif
}

bool MediaLikeBridgeEnqueue(const std::wstring& title, const std::wstring& artist,
                            const std::wstring& source, bool playing, RateAction action,
                            const std::wstring& appUserModelId) {
#ifdef _WIN32
  const ResolvedRate resolved = ResolveMediaRate(title, artist, source, playing, appUserModelId);
  if (resolved.backend == DesktopRateBackend::None || !resolved.view.connected) {
    return false;
  }
  if (action == RateAction::Like && !resolved.view.canLike) {
    return false;
  }
  if (action == RateAction::Dislike && !resolved.view.canDislike) {
    return false;
  }
  if (resolved.backend == DesktopRateBackend::BrowserBridge) {
    std::string tabId;
    if (!g_registry.EnqueueBest(resolved.hint, action, GetTickCount64(), kMediaLikeBridgeStaleMs,
                                &tabId, nullptr)) {
      return false;
    }
    MediaRateNotePending(action);
    NotifyUi();
    return true;
  }
  const uint64_t generation = MediaRateNotePending(action);
  if (!DesktopRateEnqueue(resolved.hint, resolved.aumid, action, resolved.backend, generation)) {
    MediaRateNoteResult(generation, false, action);
    return false;
  }
  NotifyUi();
  return true;
#else
  (void)title;
  (void)artist;
  (void)source;
  (void)playing;
  (void)action;
  (void)appUserModelId;
  return false;
#endif
}

namespace {

bool Fail(const std::string& message) {
  std::fprintf(stderr, "media-like-bridge self-test: %s\n", message.c_str());
#ifdef _WIN32
  LuxLog(LuxLogLevel::Error, L"media-like-bridge self-test: %S", message.c_str());
#endif
  return false;
}

TabState MakeTab(const char* id, BridgeSite site, const char* title, const char* artist, bool playing,
                 BridgeLikeState like, bool canRate, uint64_t tick) {
  TabState state;
  state.tabId = id;
  state.site = site;
  state.videoId = "video";
  state.title = title;
  state.artist = artist;
  state.playing = playing;
  state.like = like;
  state.canRate = canRate;
  state.updatedTick = tick;
  return state;
}

}  // namespace

bool MediaLikeBridgeSelfTest() {
#define BRIDGE_CHECK(cond)                                                                 \
  do {                                                                                     \
    if (!(cond)) {                                                                         \
      return Fail(std::string("line ") + std::to_string(__LINE__) + ": " #cond);          \
    }                                                                                      \
  } while (0)

  BRIDGE_CHECK(EvaluateBridgeAccess("127.0.0.1:18473",
                                    "chrome-extension://abcdefghijklmnopabcdefghijklmnop", "1",
                                    18473) == BridgeAccess::AllowExtension);
  BRIDGE_CHECK(EvaluateBridgeAccess("localhost:18473",
                                    "moz-extension://12345678-abcd-abcd-abcd-1234567890ab", "",
                                    18473) == BridgeAccess::AllowExtension);
  BRIDGE_CHECK(EvaluateBridgeAccess("LOCALHOST:18473", "extension://my-extension", "1", 18473) ==
               BridgeAccess::AllowExtension);
  BRIDGE_CHECK(EvaluateBridgeAccess("127.0.0.1:18473", "Chrome-Extension://AbcdEfgh1234", "",
                                    18473) == BridgeAccess::AllowExtension);
  BRIDGE_CHECK(EvaluateBridgeAccess("127.0.0.1:18473", "", "1", 18473) ==
               BridgeAccess::AllowLoopbackHeader);
  BRIDGE_CHECK(EvaluateBridgeAccess("127.0.0.1:18473", "", " 1 ", 18473) ==
               BridgeAccess::AllowLoopbackHeader);
  BRIDGE_CHECK(EvaluateBridgeAccess("127.0.0.1:18473", "https://evil.example", "1", 18473) ==
               BridgeAccess::Reject);
  BRIDGE_CHECK(EvaluateBridgeAccess("127.0.0.1:18473", "https://www.youtube.com", "1", 18473) ==
               BridgeAccess::Reject);
  BRIDGE_CHECK(EvaluateBridgeAccess("evil.example:18473",
                                    "chrome-extension://abcdefghijklmnopabcdefghijklmnop", "1",
                                    18473) == BridgeAccess::Reject);
  BRIDGE_CHECK(EvaluateBridgeAccess("127.0.0.1:18474",
                                    "chrome-extension://abcdefghijklmnopabcdefghijklmnop", "1",
                                    18473) == BridgeAccess::Reject);
  BRIDGE_CHECK(EvaluateBridgeAccess("127.0.0.1", "chrome-extension://abcdefghijklmnopabcdefghijklmnop",
                                    "1", 18473) == BridgeAccess::Reject);
  BRIDGE_CHECK(EvaluateBridgeAccess("[::1]:18473", "chrome-extension://abcdefghijklmnopabcdefghijklmnop",
                                    "1", 18473) == BridgeAccess::Reject);
  BRIDGE_CHECK(EvaluateBridgeAccess("8.8.8.8:18473", "", "1", 18473) == BridgeAccess::Reject);
  BRIDGE_CHECK(EvaluateBridgeAccess("127.0.0.1:18473", "chrome-extension://short", "1", 18473) ==
               BridgeAccess::Reject);
  BRIDGE_CHECK(EvaluateBridgeAccess("127.0.0.1:18473", "chrome-extension://abcdefgh/extra", "1",
                                    18473) == BridgeAccess::Reject);
  BRIDGE_CHECK(EvaluateBridgeAccess("127.0.0.1:18473", "", "", 18473) == BridgeAccess::Reject);
  BRIDGE_CHECK(EvaluateBridgeAccess("127.0.0.1:18473", "null", "1", 18473) == BridgeAccess::Reject);

  const char* allowed =
      "POST /v1/state HTTP/1.1\r\n"
      "Host: 127.0.0.1:18473\r\n"
      "Origin: chrome-extension://abcdefghijklmnopabcdefghijklmnop\r\n"
      "X-Dynamic-Island-Lux: 1\r\n"
      "Content-Type: application/json\r\n"
      "Content-Length: 2\r\n"
      "\r\n";
  HttpHeaderSet headers;
  BRIDGE_CHECK(ParseHttpHeaderBlock(allowed, &headers));
  BRIDGE_CHECK(headers.method == "POST");
  BRIDGE_CHECK(headers.path == "/v1/state");
  BRIDGE_CHECK(headers.contentLength == 2);
  BRIDGE_CHECK(EvaluateBridgeAccess(headers.host, headers.origin, headers.bridgeHeader, 18473) ==
               BridgeAccess::AllowExtension);

  const char* rebinding =
      "POST /v1/state HTTP/1.1\r\n"
      "host: rebind.attacker:18473\r\n"
      "origin: chrome-extension://abcdefghijklmnopabcdefghijklmnop\r\n"
      "x-dynamic-island-lux: 1\r\n"
      "\r\n";
  BRIDGE_CHECK(ParseHttpHeaderBlock(rebinding, &headers));
  BRIDGE_CHECK(EvaluateBridgeAccess(headers.host, headers.origin, headers.bridgeHeader, 18473) ==
               BridgeAccess::Reject);

  const char* evilPage =
      "POST /v1/state?x=1 HTTP/1.0\n"
      "Host: 127.0.0.1:18473\n"
      "Origin: https://evil.example\n"
      "X-Dynamic-Island-Lux: 1\n"
      "Content-Length: 2\n"
      "\n";
  BRIDGE_CHECK(ParseHttpHeaderBlock(evilPage, &headers));
  BRIDGE_CHECK(headers.path == "/v1/state");
  BRIDGE_CHECK(headers.method == "POST");
  BRIDGE_CHECK(EvaluateBridgeAccess(headers.host, headers.origin, headers.bridgeHeader, 18473) ==
               BridgeAccess::Reject);

  ParsedTabUpdate update;
  const char* stateJson =
      "{\"tabId\":\"t1\",\"site\":\"youtube_music\",\"videoId\":\"abc\",\"title\":\"Say \\\"Hi\\\"\","
      "\"artist\":\"Ann\",\"playing\":false,\"likeState\":\"disliked\",\"canRate\":true,"
      "\"timestamp\":42,\"ack\":[7,8],\"extra\":{\"skip\":true}}";
  BRIDGE_CHECK(ParseTabStateJson(stateJson, &update));
  BRIDGE_CHECK(update.state.tabId == "t1");
  BRIDGE_CHECK(update.state.site == BridgeSite::YouTubeMusic);
  BRIDGE_CHECK(update.state.title == "Say \"Hi\"");
  BRIDGE_CHECK(update.state.artist == "Ann");
  BRIDGE_CHECK(!update.state.playing);
  BRIDGE_CHECK(update.state.like == BridgeLikeState::Disliked);
  BRIDGE_CHECK(update.state.canRate);
  BRIDGE_CHECK(update.state.clientTimestamp == 42);
  BRIDGE_CHECK(update.ack.size() == 2 && update.ack[0] == 7 && update.ack[1] == 8);

  const char* emojiJson =
      "{\"tabId\":\"e\",\"site\":\"youtube\",\"title\":\"\\uD83D\\uDE00\",\"artist\":\"A\","
      "\"playing\":1,\"likeState\":\"none\",\"canRate\":0}";
  BRIDGE_CHECK(ParseTabStateJson(emojiJson, &update));
  BRIDGE_CHECK(update.state.title.size() == 4);
  BRIDGE_CHECK(static_cast<unsigned char>(update.state.title[0]) == 0xF0);
  BRIDGE_CHECK(static_cast<unsigned char>(update.state.title[1]) == 0x9F);
  BRIDGE_CHECK(static_cast<unsigned char>(update.state.title[2]) == 0x98);
  BRIDGE_CHECK(static_cast<unsigned char>(update.state.title[3]) == 0x80);
  BRIDGE_CHECK(update.state.playing);
  BRIDGE_CHECK(!update.state.canRate);

  BRIDGE_CHECK(!ParseTabStateJson("{\"tabId\":\"t\",\"site\":\"spotify\",\"likeState\":\"none\"}",
                                  &update));
  BRIDGE_CHECK(!ParseTabStateJson("{\"site\":\"youtube\",\"likeState\":\"none\"}", &update));
  BRIDGE_CHECK(!ParseTabStateJson(
      "{\"tabId\":\"t\",\"site\":\"youtube\",\"likeState\":\"love\",\"playing\":true}", &update));
  BRIDGE_CHECK(!ParseTabStateJson("{", &update));

  TabRegistry registry;
  registry.Upsert(MakeTab("alpha", BridgeSite::YouTube, "Alpha Song", "Ada", true,
                          BridgeLikeState::None, true, 1000),
                  1000);
  registry.Upsert(MakeTab("beta", BridgeSite::YouTube, "Beta Song", "Bea", true,
                          BridgeLikeState::Liked, true, 1000),
                  1000);
  registry.Upsert(MakeTab("music", BridgeSite::YouTubeMusic, "Alpha Song", "Ada", true,
                          BridgeLikeState::None, true, 1000),
                  1000);
  BRIDGE_CHECK(registry.size() == 3);

  SmtcHint chromeAlpha;
  chromeAlpha.title = "Alpha Song";
  chromeAlpha.artist = "Ada";
  chromeAlpha.source = "Google Chrome";
  chromeAlpha.playing = true;
  TabState matched;
  BRIDGE_CHECK(registry.BestMatch(chromeAlpha, 1000, 8000, &matched));
  BRIDGE_CHECK(matched.tabId == "alpha");

  SmtcHint decorated;
  decorated.title = "Alpha Song";
  decorated.artist = "Ada";
  decorated.source = "Microsoft Edge";
  decorated.playing = true;
  registry.Upsert(MakeTab("alpha", BridgeSite::YouTube, "Alpha Song (Official Video)", "Ada", true,
                          BridgeLikeState::Liked, true, 2000),
                  2000);
  BRIDGE_CHECK(registry.BestMatch(decorated, 2000, 8000, &matched));
  BRIDGE_CHECK(matched.tabId == "alpha");
  BRIDGE_CHECK(matched.like == BridgeLikeState::Liked);

  SmtcHint musicHint = chromeAlpha;
  musicHint.source = "YouTube Music";
  BRIDGE_CHECK(registry.BestMatch(musicHint, 2000, 8000, &matched));
  BRIDGE_CHECK(matched.tabId == "music");

  SmtcHint spotify = chromeAlpha;
  spotify.source = "Spotify";
  BRIDGE_CHECK(!registry.BestMatch(spotify, 2000, 8000, &matched));

  BRIDGE_CHECK(registry.BestMatch(chromeAlpha, 2000 + 8000, 8000, &matched));
  BRIDGE_CHECK(!registry.BestMatch(chromeAlpha, 2000 + 8001, 8000, &matched));
  BRIDGE_CHECK(registry.ExpireStale(2000 + 8001, 8000) == 3);
  BRIDGE_CHECK(registry.size() == 0);

  registry.Upsert(MakeTab("alpha", BridgeSite::YouTube, "Alpha Song", "Ada", true,
                          BridgeLikeState::None, true, 5000),
                  5000);
  registry.Upsert(MakeTab("paused", BridgeSite::YouTube, "Alpha Song", "Ada", false,
                          BridgeLikeState::None, true, 5000),
                  5000);
  BRIDGE_CHECK(registry.BestMatch(chromeAlpha, 5000, 8000, &matched));
  BRIDGE_CHECK(matched.tabId == "alpha");
  registry.Upsert(MakeTab("impostor", BridgeSite::YouTube, "Alpha Song", "Not Ada", true,
                          BridgeLikeState::None, true, 5000),
                  5000);
  BRIDGE_CHECK(registry.BestMatch(chromeAlpha, 5000, 8000, &matched));
  BRIDGE_CHECK(matched.tabId == "alpha");
  SmtcHint blankSource = chromeAlpha;
  blankSource.source.clear();
  BRIDGE_CHECK(!registry.BestMatch(blankSource, 5000, 8000, &matched));
  SmtcHint otherArtist = chromeAlpha;
  otherArtist.artist = "Not Ada";
  BRIDGE_CHECK(registry.BestMatch(otherArtist, 5000, 8000, &matched));
  BRIDGE_CHECK(matched.tabId == "impostor");

  registry.Upsert(MakeTab("locked", BridgeSite::YouTube, "Locked Song", "Ada", true,
                          BridgeLikeState::None, false, 5000),
                  5000);
  SmtcHint locked = chromeAlpha;
  locked.title = "Locked Song";
  std::string queuedTab;
  QueuedCommand queued{};
  BRIDGE_CHECK(!registry.EnqueueBest(locked, RateAction::Like, 5000, 8000, &queuedTab, &queued));

  BRIDGE_CHECK(registry.EnqueueBest(chromeAlpha, RateAction::Like, 5000, 8000, &queuedTab, &queued));
  BRIDGE_CHECK(queuedTab == "alpha");
  BRIDGE_CHECK(queued.action == RateAction::Like);
  BRIDGE_CHECK(registry.EnqueueBest(chromeAlpha, RateAction::Dislike, 5000, 8000, &queuedTab,
                                    &queued));
  const std::vector<QueuedCommand> pending = registry.Peek("alpha");
  BRIDGE_CHECK(pending.size() == 2);
  BRIDGE_CHECK(pending[0].action == RateAction::Like);
  BRIDGE_CHECK(pending[0].videoId == "video");
  BRIDGE_CHECK(pending[1].action == RateAction::Dislike);
  BRIDGE_CHECK(registry.Peek("beta").empty());
  registry.Upsert(MakeTab("alpha", BridgeSite::YouTube, "Alpha Song", "Ada", true,
                          BridgeLikeState::Liked, true, 5100),
                  5100);
  BRIDGE_CHECK(registry.Peek("alpha").size() == 2);
  registry.Ack("alpha", {pending[0].id});
  const std::vector<QueuedCommand> afterAck = registry.Peek("alpha");
  BRIDGE_CHECK(afterAck.size() == 1);
  BRIDGE_CHECK(afterAck[0].id == pending[1].id);
  registry.Ack("alpha", {afterAck[0].id});
  BRIDGE_CHECK(registry.Peek("alpha").empty());

  BRIDGE_CHECK(registry.EnqueueBest(chromeAlpha, RateAction::Like, 5200, 8000, &queuedTab, &queued));
  TabState moved = MakeTab("alpha", BridgeSite::YouTube, "Next Song", "Ada", true, BridgeLikeState::None,
                           true, 5300);
  moved.videoId = "next-video";
  moved.clientTimestamp = 80;
  BRIDGE_CHECK(registry.Upsert(moved, 5300));
  BRIDGE_CHECK(registry.Peek("alpha").empty());
  moved.clientTimestamp = 70;
  moved.like = BridgeLikeState::Liked;
  BRIDGE_CHECK(!registry.Upsert(moved, 5400));
  SmtcHint nextHint = chromeAlpha;
  nextHint.title = "Next Song";
  BRIDGE_CHECK(registry.BestMatch(nextHint, 5400, 8000, &matched));
  BRIDGE_CHECK(matched.like == BridgeLikeState::None);
  BRIDGE_CHECK(matched.clientTimestamp == 80);
  moved.clientTimestamp = 90;
  moved.title = "Alpha Song";
  moved.like = BridgeLikeState::Disliked;
  BRIDGE_CHECK(registry.Upsert(moved, 5500));
  BRIDGE_CHECK(registry.BestMatch(chromeAlpha, 5500, 8000, &matched));
  BRIDGE_CHECK(matched.like == BridgeLikeState::Disliked);
  BRIDGE_CHECK(matched.videoId == "next-video");

  BRIDGE_CHECK(std::string(FormatCommandsJson({})) ==
               "{\"ok\":true,\"service\":\"dynamic-island-lux\",\"commands\":[]}");
  BRIDGE_CHECK(FormatCommandsJson({{7, RateAction::Like, "vid"}, {8, RateAction::Dislike, "v2"}}) ==
               "{\"ok\":true,\"service\":\"dynamic-island-lux\",\"commands\":[{\"id\":7,\"action\":"
               "\"like\",\"videoId\":\"vid\"},{\"id\":8,\"action\":\"dislike\",\"videoId\":\"v2\"}]}");
  BRIDGE_CHECK(std::string(RateActionJson(RateAction::Like)) == "like");
  BRIDGE_CHECK(std::string(RateActionJson(RateAction::Dislike)) == "dislike");
  BRIDGE_CHECK(DesktopRateSelfTest());

#undef BRIDGE_CHECK
#ifdef _WIN32
  LUX_LOGI(L"media-like-bridge self-test PASS");
#else
  std::fprintf(stderr, "media-like-bridge self-test PASS\n");
#endif
  return true;
}

#ifdef MEDIA_LIKE_BRIDGE_SELFTEST_MAIN
int main() { return MediaLikeBridgeSelfTest() ? 0 : 1; }
#endif
