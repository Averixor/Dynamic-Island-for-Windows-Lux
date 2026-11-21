#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <appmodel.h>
#include <dwmapi.h>
#include <objbase.h>
#include <oleauto.h>
#include <UIAutomation.h>
#include <winhttp.h>
#include <wrl/client.h>

#include "app_context.h"
#include "logging.h"
#include "media_desktop_rate.h"

#ifndef LUX_APP_VERSION_STR
#define LUX_APP_VERSION_STR "1.0.10"
#endif

#include <shlobj.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace {

bool IsDigitChar(char ch) { return ch >= '0' && ch <= '9'; }

std::string LowerAscii(std::string value) {
  for (char& ch : value) {
    if (ch >= 'A' && ch <= 'Z') {
      ch = static_cast<char>(ch - 'A' + 'a');
    }
  }
  return value;
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

bool DecodeUtf8(const std::string& text, size_t* index, uint32_t* codePoint) {
  if (!index || !codePoint || *index >= text.size()) {
    return false;
  }
  const auto* bytes = reinterpret_cast<const unsigned char*>(text.data());
  const unsigned char lead = bytes[*index];
  if (lead < 0x80) {
    *codePoint = lead;
    *index += 1;
    return true;
  }
  int need = 0;
  uint32_t cp = 0;
  if ((lead & 0xE0) == 0xC0) {
    need = 1;
    cp = lead & 0x1F;
  } else if ((lead & 0xF0) == 0xE0) {
    need = 2;
    cp = lead & 0x0F;
  } else if ((lead & 0xF8) == 0xF0) {
    need = 3;
    cp = lead & 0x07;
  } else {
    return false;
  }
  if (*index + static_cast<size_t>(need) >= text.size()) {
    return false;
  }
  for (int i = 1; i <= need; ++i) {
    const unsigned char cont = bytes[*index + static_cast<size_t>(i)];
    if ((cont & 0xC0) != 0x80) {
      return false;
    }
    cp = (cp << 6) | (cont & 0x3F);
  }
  *codePoint = cp;
  *index += static_cast<size_t>(need) + 1;
  return true;
}

uint32_t FoldCodePoint(uint32_t codePoint) {
  if (codePoint >= 'A' && codePoint <= 'Z') {
    return codePoint - 'A' + 'a';
  }
  if ((codePoint >= 0xC0 && codePoint <= 0xD6) || (codePoint >= 0xD8 && codePoint <= 0xDE)) {
    return codePoint + 0x20;
  }
  if (codePoint >= 0x410 && codePoint <= 0x42F) {
    return codePoint + 0x20;
  }
  switch (codePoint) {
  case 0x401:
    return 0x451;
  case 0x404:
    return 0x454;
  case 0x406:
    return 0x456;
  case 0x407:
    return 0x457;
  case 0x490:
    return 0x491;
  default:
    return codePoint;
  }
}

std::string Utf8Lower(const std::string& text) {
  std::string out;
  out.reserve(text.size());
  size_t index = 0;
  while (index < text.size()) {
    const size_t mark = index;
    uint32_t codePoint = 0;
    if (!DecodeUtf8(text, &index, &codePoint)) {
      out.push_back(text[mark]);
      index = mark + 1;
      continue;
    }
    AppendUtf8(&out, FoldCodePoint(codePoint));
  }
  return out;
}

std::string NormalizeLabel(const std::string& raw) {
  const std::string lower = Utf8Lower(raw);
  std::string out;
  out.reserve(lower.size());
  bool pendingSpace = false;
  for (unsigned char ch : lower) {
    const bool keep = (ch >= 'a' && ch <= 'z') || IsDigitChar(static_cast<char>(ch)) || ch >= 0x80;
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

bool ContainsPhrase(const std::string& text, const char* phrase) {
  if (!phrase || phrase[0] == '\0' || text.empty()) {
    return false;
  }
  const std::string key(phrase);
  const std::string padded = std::string(" ") + text + " ";
  if (padded.find(std::string(" ") + key + " ") != std::string::npos) {
    return true;
  }
  bool nonAscii = false;
  for (unsigned char ch : key) {
    if (ch >= 0x80) {
      nonAscii = true;
      break;
    }
  }
  return nonAscii && key.find(' ') == std::string::npos && text.find(key) != std::string::npos;
}

bool HasRejectMarker(const std::string& text) {
  static const char* kWords[] = {"comment",    "comments", "reply",     "share",    "subscribe",
                                 "download",   "shuffle",  "repeat",    "lyrics",   "playlist",
                                 "queue",      "коментар", "комментар", "відповід", "ответ",
                                 "поділитися", "поделиться"};
  for (const char* word : kWords) {
    const std::string key(word);
    bool nonAscii = false;
    for (unsigned char ch : key) {
      if (ch >= 0x80) {
        nonAscii = true;
        break;
      }
    }
    if (nonAscii) {
      if (text.find(key) != std::string::npos) {
        return true;
      }
    } else if (ContainsPhrase(text, word)) {
      return true;
    }
  }
  return false;
}

UiaRateKind ClassifyNormalized(const std::string& text) {
  if (text.empty()) {
    return UiaRateKind::None;
  }
  static const char* kRemove[] = {
      "remove from your library", "remove from liked songs", "remove from my collection",
      "remove from favorites",    "remove from favourites",  "remove from favorite tracks",
      "saved to your library",    "видалити з бібліотеки",   "удалить из медиатеки",
      "убрать из медиатеки"};
  for (const char* phrase : kRemove) {
    if (ContainsPhrase(text, phrase)) {
      return UiaRateKind::LibraryRemove;
    }
  }
  static const char* kSave[] = {
      "save to your library", "save to library",         "add to your library",
      "add to library",       "add to liked songs",      "add to my collection",
      "add to favorites",     "add to favourites",       "add to favorite tracks",
      "add to favourite tracks", "зберегти в бібліотеці", "додати до бібліотеки",
      "сохранить в медиатеке", "добавить в медиатеку"};
  for (const char* phrase : kSave) {
    if (ContainsPhrase(text, phrase)) {
      return UiaRateKind::LibrarySave;
    }
  }
  if (text == "liked songs" || text == "liked videos" || text == "your likes") {
    return UiaRateKind::Rejected;
  }
  static const char* kDislike[] = {"thumbs down",     "dislike",        "disliked",
                                   "не подобається",  "не нравится",    "daumen runter",
                                   "gefällt mir nicht", "no me gusta",  "je n aime pas",
                                   "nie lubie",       "nie lubię",      "nao gostei",
                                   "não gostei"};
  for (const char* phrase : kDislike) {
    if (ContainsPhrase(text, phrase)) {
      return UiaRateKind::Dislike;
    }
  }
  if (text.find("低評価") != std::string::npos) {
    return UiaRateKind::Dislike;
  }
  static const char* kLike[] = {"thumbs up",    "unlike",     "unliked",     "liked",
                                "like",         "подобається", "нравится",   "daumen hoch",
                                "gefällt mir",  "me gusta",   "j aime",      "lubie",
                                "lubię",        "gostei",     "love",        "loved",
                                "unlove",       "favorite",   "favourite",   "unfavorite",
                                "unfavourite"};
  for (const char* phrase : kLike) {
    if (ContainsPhrase(text, phrase)) {
      return UiaRateKind::Like;
    }
  }
  if (text.find("高評価") != std::string::npos) {
    return UiaRateKind::Like;
  }
  return UiaRateKind::None;
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
    const bool keep = (ch >= 'a' && ch <= 'z') || IsDigitChar(static_cast<char>(ch)) || ch >= 0x80;
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
  std::vector<std::string> leftTokens;
  std::vector<std::string> rightTokens;
  auto collect = [](const std::string& text, std::vector<std::string>* tokens) {
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
        tokens->push_back(text.substr(start, index - start));
      }
    }
    std::sort(tokens->begin(), tokens->end());
    tokens->erase(std::unique(tokens->begin(), tokens->end()), tokens->end());
  };
  collect(left, &leftTokens);
  collect(right, &rightTokens);
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

std::string ClipText(std::string value) {
  if (value.size() > 500) {
    value.resize(500);
  }
  return value;
}

struct JVal {
  enum class Type { Null, Bool, Number, String, Array, Object };
  Type type = Type::Null;
  bool b = false;
  double n = 0;
  std::string s;
  std::vector<JVal> array;
  std::vector<std::pair<std::string, JVal>> object;
};

class JsonReader {
 public:
  explicit JsonReader(const std::string& text) : text_(text) {}

  bool Parse(JVal* out) {
    if (!out) {
      return false;
    }
    if (text_.size() >= 3 && static_cast<unsigned char>(text_[0]) == 0xEF &&
        static_cast<unsigned char>(text_[1]) == 0xBB &&
        static_cast<unsigned char>(text_[2]) == 0xBF) {
      index_ = 3;
    }
    SkipWs();
    if (!ParseValue(out, 0)) {
      return false;
    }
    SkipWs();
    return index_ == text_.size();
  }

 private:
  const std::string& text_;
  size_t index_ = 0;
  int nodes_ = 0;

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

  bool Has(size_t count) const { return index_ + count <= text_.size(); }

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

  bool ParseString(std::string* out) {
    if (!out || !Consume('"')) {
      return false;
    }
    out->clear();
    while (index_ < text_.size()) {
      const char ch = text_[index_++];
      if (ch == '"') {
        return true;
      }
      if (static_cast<unsigned char>(ch) < 0x20) {
        return false;
      }
      if (ch != '\\') {
        if (out->size() < 2000) {
          out->push_back(ch);
        }
        continue;
      }
      if (index_ >= text_.size()) {
        return false;
      }
      const char esc = text_[index_++];
      char decoded = 0;
      bool useDecoded = true;
      switch (esc) {
      case '"':
      case '\\':
      case '/':
        decoded = esc;
        break;
      case 'b':
        decoded = '\b';
        break;
      case 'f':
        decoded = '\f';
        break;
      case 'n':
        decoded = '\n';
        break;
      case 'r':
        decoded = '\r';
        break;
      case 't':
        decoded = '\t';
        break;
      case 'u': {
        uint32_t unit = 0;
        if (!ParseHex4(&unit)) {
          return false;
        }
        if (unit >= 0xD800 && unit <= 0xDBFF && Has(2) && text_[index_] == '\\' &&
            text_[index_ + 1] == 'u') {
          index_ += 2;
          uint32_t low = 0;
          if (!ParseHex4(&low) || low < 0xDC00 || low > 0xDFFF) {
            unit = 0xFFFD;
          } else {
            unit = 0x10000 + (((unit - 0xD800) << 10) | (low - 0xDC00));
          }
        } else if (unit >= 0xDC00 && unit <= 0xDFFF) {
          unit = 0xFFFD;
        }
        if (out->size() < 2000) {
          AppendUtf8(out, unit);
        }
        useDecoded = false;
        break;
      }
      default:
        return false;
      }
      if (useDecoded && out->size() < 2000) {
        out->push_back(decoded);
      }
    }
    return false;
  }

  bool ParseNumber(double* out) {
    if (!out) {
      return false;
    }
    const bool neg = Consume('-');
    if (!IsDigitChar(Peek())) {
      return false;
    }
    double value = 0;
    while (IsDigitChar(Peek())) {
      value = value * 10.0 + static_cast<double>(Peek() - '0');
      ++index_;
    }
    if (Consume('.')) {
      if (!IsDigitChar(Peek())) {
        return false;
      }
      double place = 0.1;
      while (IsDigitChar(Peek())) {
        value += place * static_cast<double>(Peek() - '0');
        place *= 0.1;
        ++index_;
      }
    }
    if (Peek() == 'e' || Peek() == 'E') {
      ++index_;
      if (Peek() == '+' || Peek() == '-') {
        ++index_;
      }
      if (!IsDigitChar(Peek())) {
        return false;
      }
      while (IsDigitChar(Peek())) {
        ++index_;
      }
    }
    *out = neg ? -value : value;
    return true;
  }

  bool SkipValue(int depth) {
    if (depth > 16) {
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
        if (!Consume(':')) {
          return false;
        }
        SkipWs();
        if (!SkipValue(depth + 1)) {
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
    if (Peek() == 't') {
      return Consume('t') && Consume('r') && Consume('u') && Consume('e');
    }
    if (Peek() == 'f') {
      return Consume('f') && Consume('a') && Consume('l') && Consume('s') && Consume('e');
    }
    if (Peek() == 'n') {
      return Consume('n') && Consume('u') && Consume('l') && Consume('l');
    }
    double ignored = 0;
    return ParseNumber(&ignored);
  }

  bool HeavyKey(const std::string& key) const {
    return key == "queue" || key == "thumbnails" || key == "automixItems" || key == "items" ||
           key == "counterparts" || key == "lyrics";
  }

  bool ParseValue(JVal* out, int depth) {
    if (!out || depth > 16 || ++nodes_ > 8000) {
      return false;
    }
    if (Peek() == '{') {
      out->type = JVal::Type::Object;
      ++index_;
      SkipWs();
      if (Consume('}')) {
        return true;
      }
      for (;;) {
        std::string key;
        SkipWs();
        if (!ParseString(&key)) {
          return false;
        }
        SkipWs();
        if (!Consume(':')) {
          return false;
        }
        SkipWs();
        if (HeavyKey(key)) {
          if (!SkipValue(depth + 1)) {
            return false;
          }
        } else {
          JVal child;
          if (!ParseValue(&child, depth + 1)) {
            return false;
          }
          if (out->object.size() < 64) {
            out->object.emplace_back(std::move(key), std::move(child));
          }
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
      out->type = JVal::Type::Array;
      ++index_;
      SkipWs();
      if (Consume(']')) {
        return true;
      }
      for (;;) {
        JVal child;
        if (!ParseValue(&child, depth + 1)) {
          return false;
        }
        if (out->array.size() < 32) {
          out->array.push_back(std::move(child));
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
    if (Peek() == '"') {
      out->type = JVal::Type::String;
      return ParseString(&out->s);
    }
    if (Peek() == 't' || Peek() == 'f') {
      out->type = JVal::Type::Bool;
      if (Peek() == 't') {
        if (!(Consume('t') && Consume('r') && Consume('u') && Consume('e'))) {
          return false;
        }
        out->b = true;
        return true;
      }
      if (!(Consume('f') && Consume('a') && Consume('l') && Consume('s') && Consume('e'))) {
        return false;
      }
      out->b = false;
      return true;
    }
    if (Peek() == 'n') {
      out->type = JVal::Type::Null;
      return Consume('n') && Consume('u') && Consume('l') && Consume('l');
    }
    out->type = JVal::Type::Number;
    return ParseNumber(&out->n);
  }
};

bool ParseJson(const std::string& json, JVal* out) {
  if (!out || json.size() > 512 * 1024) {
    return false;
  }
  JsonReader reader(json);
  return reader.Parse(out);
}

const JVal* FindField(const JVal& object, const char* key) {
  if (object.type != JVal::Type::Object || !key) {
    return nullptr;
  }
  for (const auto& field : object.object) {
    if (field.first == key) {
      return &field.second;
    }
  }
  return nullptr;
}

bool NearNumber(double value, double target) { return value > target - 0.01 && value < target + 0.01; }

ParsedLike ParseLikeToken(const JVal& value) {
  if (value.type == JVal::Type::Null) {
    return ParsedLike::Unknown;
  }
  if (value.type != JVal::Type::String) {
    return ParsedLike::Unknown;
  }
  const std::string token = LowerAscii(value.s);
  if (token == "like" || token == "liked" || token == "thumbsup" || token == "thumbs_up" ||
      token == "thumbs-up") {
    return ParsedLike::Liked;
  }
  if (token == "dislike" || token == "disliked" || token == "thumbsdown" || token == "thumbs_down" ||
      token == "thumbs-down") {
    return ParsedLike::Disliked;
  }
  if (token == "none" || token == "indifferent" || token == "neutral" || token == "unrated") {
    return ParsedLike::None;
  }
  return ParsedLike::Unknown;
}

ParsedLike ParseV1LikeNumber(double value) {
  if (NearNumber(value, -1)) {
    return ParsedLike::Unknown;
  }
  if (NearNumber(value, 0)) {
    return ParsedLike::Disliked;
  }
  if (NearNumber(value, 1)) {
    return ParsedLike::None;
  }
  if (NearNumber(value, 2)) {
    return ParsedLike::Liked;
  }
  return ParsedLike::Unknown;
}

std::string ReadArtist(const JVal& object) {
  static const char* kKeys[] = {"artist", "artists", "author"};
  for (const char* key : kKeys) {
    const JVal* value = FindField(object, key);
    if (!value) {
      continue;
    }
    if (value->type == JVal::Type::String && !value->s.empty()) {
      return ClipText(value->s);
    }
    if (value->type != JVal::Type::Array) {
      continue;
    }
    std::string joined;
    for (const JVal& item : value->array) {
      std::string name;
      if (item.type == JVal::Type::String) {
        name = item.s;
      } else if (item.type == JVal::Type::Object) {
        if (const JVal* field = FindField(item, "name")) {
          if (field->type == JVal::Type::String) {
            name = field->s;
          }
        }
      }
      if (name.empty()) {
        continue;
      }
      if (!joined.empty()) {
        joined += ", ";
      }
      joined += name;
      if (joined.size() > 500) {
        break;
      }
    }
    if (!joined.empty()) {
      return ClipText(joined);
    }
  }
  return {};
}

bool ReadBool(const JVal& object, const char* key, bool* out) {
  const JVal* value = FindField(object, key);
  if (!value || value->type != JVal::Type::Bool || !out) {
    return false;
  }
  *out = value->b;
  return true;
}

bool ReadNumber(const JVal& object, const char* key, double* out) {
  const JVal* value = FindField(object, key);
  if (!value || value->type != JVal::Type::Number || !out) {
    return false;
  }
  *out = value->n;
  return true;
}

void ApplyLikeField(const JVal& object, const char* key, ParsedLike* like, bool v1Number) {
  const JVal* value = FindField(object, key);
  if (!value || !like) {
    return;
  }
  if (v1Number && value->type == JVal::Type::Number) {
    *like = ParseV1LikeNumber(value->n);
    return;
  }
  if (value->type == JVal::Type::String || value->type == JVal::Type::Null) {
    const ParsedLike parsed = ParseLikeToken(*value);
    if (parsed != ParsedLike::Unknown || value->type == JVal::Type::Null) {
      *like = parsed;
    }
  }
}

std::string WordBlob(std::string text) {
  for (char& ch : text) {
    if (ch == '\\' || ch == '/' || ch == '.' || ch == '_' || ch == '-' || ch == '!' || ch == ':' ||
        ch == '\n' || ch == '\r') {
      ch = ' ';
    }
  }
  return text;
}

enum class BrowserProduct { None, Chrome, Edge, Firefox, Brave, Opera, Vivaldi };

BrowserProduct BrowserFromImage(const std::string& baseLower) {
  if (baseLower == "chrome.exe" || baseLower == "chromium.exe") {
    return BrowserProduct::Chrome;
  }
  if (baseLower == "msedge.exe" || baseLower == "microsoftedge.exe") {
    return BrowserProduct::Edge;
  }
  if (baseLower == "firefox.exe") {
    return BrowserProduct::Firefox;
  }
  if (baseLower == "brave.exe") {
    return BrowserProduct::Brave;
  }
  if (baseLower == "opera.exe") {
    return BrowserProduct::Opera;
  }
  if (baseLower == "vivaldi.exe") {
    return BrowserProduct::Vivaldi;
  }
  return BrowserProduct::None;
}

BrowserProduct BrowserFromText(const std::string& lowerBlob) {
  const std::string words = WordBlob(lowerBlob);
  if (ContainsPhrase(words, "chrome") || ContainsPhrase(words, "chromium") ||
      lowerBlob.find("chrome.exe") != std::string::npos) {
    return BrowserProduct::Chrome;
  }
  if (ContainsPhrase(words, "msedge") || ContainsPhrase(words, "microsoftedge") ||
      ContainsPhrase(words, "edge") || lowerBlob.find("msedge.exe") != std::string::npos) {
    return BrowserProduct::Edge;
  }
  if (ContainsPhrase(words, "firefox") || lowerBlob.find("firefox.exe") != std::string::npos) {
    return BrowserProduct::Firefox;
  }
  if (ContainsPhrase(words, "brave") || lowerBlob.find("brave.exe") != std::string::npos) {
    return BrowserProduct::Brave;
  }
  if (ContainsPhrase(words, "opera") || lowerBlob.find("opera.exe") != std::string::npos) {
    return BrowserProduct::Opera;
  }
  if (ContainsPhrase(words, "vivaldi") || lowerBlob.find("vivaldi.exe") != std::string::npos) {
    return BrowserProduct::Vivaldi;
  }
  return BrowserProduct::None;
}

bool IsYtmImage(const std::string& baseLower) {
  return baseLower == "youtube music.exe" || baseLower == "youtube-music.exe" ||
         baseLower == "youtubemusic.exe" || baseLower == "ytmdesktop.exe" ||
         baseLower == "youtube-music-desktop-app.exe" ||
         baseLower == "youtube music desktop app.exe" || baseLower == "pear desktop.exe" ||
         baseLower == "peardesktop.exe";
}

std::string BaseNameLower(const std::string& path) {
  const size_t slash = path.find_last_of("\\/");
  return LowerAscii(slash == std::string::npos ? path : path.substr(slash + 1));
}

SourceRoute RouteFor(PlayerFamily family) {
  SourceRoute route;
  route.family = family;
  switch (family) {
  case PlayerFamily::None:
    break;
  case PlayerFamily::Browser:
    route.allowBrowserBridge = true;
    route.allowUiAutomation = true;
    break;
  case PlayerFamily::YouTubeMusicDesktop:
    route.allowPear = true;
    route.allowYtmDesktop = true;
    route.allowUiAutomation = true;
    route.preferDesktopApi = true;
    break;
  case PlayerFamily::YouTubeAmbiguous:
    route.allowBrowserBridge = true;
    route.allowPear = true;
    route.allowYtmDesktop = true;
    route.allowUiAutomation = true;
    route.preferDesktopApi = true;
    break;
  case PlayerFamily::Spotify:
  case PlayerFamily::Tidal:
  case PlayerFamily::Deezer:
  case PlayerFamily::AppleMusic:
    route.allowUiAutomation = true;
    break;
  }
  return route;
}

bool DesktopBlob(const std::string& raw) {
  return raw.find("th-ch") != std::string::npos || raw.find("ytmdesktop") != std::string::npos ||
         raw.find("pear-desktop") != std::string::npos || raw.find("pear desktop") != std::string::npos ||
         raw.find("peardesktop") != std::string::npos || raw.find("youtube-music") != std::string::npos ||
         raw.find("youtube_music_desktop") != std::string::npos ||
         raw.find("youtubemusic") != std::string::npos ||
         raw.find("youtube music.exe") != std::string::npos ||
         raw.find("youtube music desktop") != std::string::npos ||
         raw.find("youtube-music-desktop-app") != std::string::npos;
}

BackendChoice ChoiceFrom(DesktopRateBackend backend, bool canLike, bool canDislike, ParsedLike like) {
  BackendChoice choice;
  choice.backend = backend;
  choice.canLike = canLike;
  choice.canDislike = canDislike;
  choice.like = like;
  return choice;
}

bool IsUiaControlActive(const UiaCandidate& control, UiaRateKind kind) {
  const std::string name = NormalizeLabel(control.name);
  if (kind == UiaRateKind::LibraryRemove) {
    if (control.toggleState == 0 && !control.pressed) {
      return false;
    }
    return true;
  }
  if (control.toggleState == 1 || control.pressed) {
    return true;
  }
  if (control.toggleState == 0) {
    return false;
  }
  if (kind == UiaRateKind::Like &&
      (ContainsPhrase(name, "unlike") || ContainsPhrase(name, "unliked"))) {
    return true;
  }
  return false;
}

std::wstring WidenAscii(const std::string& value) {
  std::wstring out;
  out.reserve(value.size());
  for (unsigned char ch : value) {
    out.push_back(static_cast<wchar_t>(ch));
  }
  return out;
}

std::string WideToUtf8(const wchar_t* value, int chars) {
  if (!value || chars == 0) {
    return {};
  }
  if (chars < 0) {
    chars = static_cast<int>(wcslen(value));
  }
  if (chars <= 0) {
    return {};
  }
  const int bytes =
      WideCharToMultiByte(CP_UTF8, 0, value, chars, nullptr, 0, nullptr, nullptr);
  if (bytes <= 0) {
    return {};
  }
  std::string out(static_cast<size_t>(bytes), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value, chars, out.data(), bytes, nullptr, nullptr);
  return out;
}

std::string WideToUtf8(const std::wstring& value) {
  return WideToUtf8(value.c_str(), static_cast<int>(value.size()));
}

struct HttpResult {
  bool transportOk = false;
  int status = 0;
  std::string body;
};

struct InflightRequest {
  std::mutex mutex;
  HINTERNET handle = nullptr;
  bool cancelled = false;

  void Reset() {
    std::lock_guard<std::mutex> lock(mutex);
    cancelled = false;
    handle = nullptr;
  }

  bool Arm(HINTERNET request) {
    std::lock_guard<std::mutex> lock(mutex);
    if (cancelled) {
      return false;
    }
    handle = request;
    return true;
  }

  bool Disarm(HINTERNET request) {
    std::lock_guard<std::mutex> lock(mutex);
    if (handle == request) {
      handle = nullptr;
      return false;
    }
    return true;
  }

  void Cancel() {
    std::lock_guard<std::mutex> lock(mutex);
    cancelled = true;
    if (handle) {
      WinHttpCloseHandle(handle);
      handle = nullptr;
    }
  }
};

struct CommandSlot {
  bool has = false;
  RateAction action = RateAction::Like;
  DesktopRateBackend backend = DesktopRateBackend::None;
  SmtcHint hint;
  std::string aumid;
  uint64_t generation = 0;
  uint64_t serial = 0;
};

struct SessionState {
  std::string title;
  std::string artist;
  std::string source;
  std::string aumid;
  bool playing = false;
  bool valid = false;
  bool browserCovers = false;
  uint64_t serial = 0;
};

struct SnapshotState {
  bool fresh = false;
  uint64_t serial = 0;
  uint64_t tick = 0;
  BackendOffer offer;
};

InflightRequest g_inflight;
std::mutex g_mu;
std::mutex g_life;
SessionState g_session;
SnapshotState g_snapshot;
CommandSlot g_command;
std::atomic<bool> g_stop{false};
std::atomic<bool> g_running{false};
HANDLE g_event = nullptr;
HANDLE g_thread = nullptr;

bool SaneToken(const std::string& token) {
  if (token.size() < 8 || token.size() > 4096) {
    return false;
  }
  for (unsigned char ch : token) {
    if (ch <= 32 || ch >= 127) {
      return false;
    }
  }
  return true;
}

void ReadHttpBody(HINTERNET request, std::string* body) {
  if (!request || !body) {
    return;
  }
  for (;;) {
    if (g_stop.load(std::memory_order_acquire)) {
      return;
    }
    DWORD available = 0;
    if (!WinHttpQueryDataAvailable(request, &available)) {
      return;
    }
    if (available == 0) {
      return;
    }
    if (body->size() >= 512 * 1024) {
      return;
    }
    const DWORD chunk = available > 16 * 1024 ? 16 * 1024 : available;
    const size_t offset = body->size();
    body->resize(offset + chunk);
    DWORD read = 0;
    if (!WinHttpReadData(request, body->data() + offset, chunk, &read)) {
      body->resize(offset);
      return;
    }
    body->resize(offset + read);
    if (read == 0) {
      return;
    }
  }
}

enum class LoopbackAuthMode {
  None,
  Bearer,
  RawToken,
};

HttpResult LoopbackRequest(HINTERNET session, int port, const wchar_t* method, const wchar_t* path,
                           const std::string* body, const std::string& authToken,
                           LoopbackAuthMode authMode, int timeoutMs) {
  HttpResult result;
  if (!session || !method || !path || path[0] != L'/' ||
      (port != kPearDesktopApiPort && port != kYtmDesktopApiPort) || timeoutMs < 50 ||
      timeoutMs > 35000) {
    return result;
  }
  if (!authToken.empty() && !SaneToken(authToken)) {
    return result;
  }
  if (g_stop.load(std::memory_order_acquire)) {
    return result;
  }
  HINTERNET connect =
      WinHttpConnect(session, L"127.0.0.1", static_cast<INTERNET_PORT>(port), 0);
  if (!connect) {
    return result;
  }
  HINTERNET request =
      WinHttpOpenRequest(connect, method, path, nullptr, WINHTTP_NO_REFERER,
                         WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_ESCAPE_DISABLE);
  if (!request) {
    WinHttpCloseHandle(connect);
    return result;
  }
  const DWORD timeout = static_cast<DWORD>(timeoutMs);
  WinHttpSetOption(request, WINHTTP_OPTION_CONNECT_TIMEOUT, const_cast<DWORD*>(&timeout),
                   sizeof(timeout));
  WinHttpSetOption(request, WINHTTP_OPTION_SEND_TIMEOUT, const_cast<DWORD*>(&timeout),
                   sizeof(timeout));
  WinHttpSetOption(request, WINHTTP_OPTION_RECEIVE_TIMEOUT, const_cast<DWORD*>(&timeout),
                   sizeof(timeout));
  DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
  WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect));

  std::wstring headers = L"Accept: application/json\r\n";
  if (body) {
    headers += L"Content-Type: application/json; charset=utf-8\r\n";
  }
  if (!authToken.empty() && authMode != LoopbackAuthMode::None) {
    headers += L"Authorization: ";
    if (authMode == LoopbackAuthMode::Bearer) {
      headers += L"Bearer ";
    }
    headers += WidenAscii(authToken);
    headers += L"\r\n";
  }
  if (!g_inflight.Arm(request)) {
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    return result;
  }
  const DWORD payloadLen = body ? static_cast<DWORD>(body->size()) : 0;
  void* payload = body && !body->empty() ? const_cast<char*>(body->data()) : WINHTTP_NO_REQUEST_DATA;
  BOOL sent = WinHttpSendRequest(request, headers.c_str(), static_cast<DWORD>(-1), payload, payloadLen,
                                 payloadLen, 0);
  if (sent) {
    sent = WinHttpReceiveResponse(request, nullptr);
  }
  const bool closedOutside = g_inflight.Disarm(request);
  if (!closedOutside) {
    if (sent) {
      result.transportOk = true;
      DWORD status = 0;
      DWORD size = sizeof(status);
      if (WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                              WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
                              WINHTTP_NO_HEADER_INDEX)) {
        result.status = static_cast<int>(status);
      }
      ReadHttpBody(request, &result.body);
    }
    WinHttpCloseHandle(request);
  }
  WinHttpCloseHandle(connect);
  return result;
}

constexpr const char* kYtmCompanionAppIdValue = "dynamicislandlux";
constexpr const char* kYtmCompanionAppNameValue = "Dynamic Island Lux";
constexpr int kYtmAuthCodeTimeoutMs = 2500;
constexpr int kYtmAuthRequestTimeoutMs = 32000;
constexpr uint64_t kYtmAuthDeniedBackoffMs = 600000;
constexpr uint64_t kYtmAuthFailBackoffMs = 180000;

std::wstring YtmTokenStorePath() {
  wchar_t base[MAX_PATH] = {};
  if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, base))) {
    return {};
  }
  std::wstring dir = base;
  dir += L"\\DynamicIslandLux";
  SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
  return dir + L"\\ytmdesktop_v2_token.txt";
}

std::string LoadYtmTokenFromDisk() {
  const std::wstring path = YtmTokenStorePath();
  if (path.empty()) {
    return {};
  }
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return {};
  }
  LARGE_INTEGER size = {};
  if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 || size.QuadPart > 8192) {
    CloseHandle(file);
    return {};
  }
  std::string raw(static_cast<size_t>(size.QuadPart), '\0');
  DWORD read = 0;
  const BOOL ok = ReadFile(file, raw.data(), static_cast<DWORD>(raw.size()), &read, nullptr);
  CloseHandle(file);
  if (!ok) {
    return {};
  }
  raw.resize(read);
  std::string token;
  if (!ParseYtmTokenStore(raw, &token) || !SaneToken(token)) {
    return {};
  }
  return token;
}

void SaveYtmTokenToDisk(const std::string& token) {
  if (LuxSmokeTestFlag() || !SaneToken(token)) {
    return;
  }
  const std::wstring path = YtmTokenStorePath();
  if (path.empty()) {
    return;
  }
  const std::string body = FormatYtmTokenStore(token);
  const std::wstring tmp = path + L".tmp";
  HANDLE file = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return;
  }
  DWORD written = 0;
  const BOOL ok =
      WriteFile(file, body.data(), static_cast<DWORD>(body.size()), &written, nullptr);
  CloseHandle(file);
  if (!ok || written != body.size()) {
    DeleteFileW(tmp.c_str());
    return;
  }
  if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    DeleteFileW(tmp.c_str());
  }
}

void ClearYtmTokenOnDisk() {
  if (LuxSmokeTestFlag()) {
    return;
  }
  const std::wstring path = YtmTokenStorePath();
  if (!path.empty()) {
    DeleteFileW(path.c_str());
  }
}

struct ProbeCache {
  uint64_t pearRetryAt = 0;
  uint64_t ytmRetryAt = 0;
  uint64_t pearAuthRetryAt = 0;
  uint64_t ytmAuthRetryAt = 0;
  uint64_t uiaRetryAt = 0;
  bool pearAuthDenied = false;
  bool pearAuthLogged = false;
  bool ytmAuthDenied = false;
  bool ytmAuthLogged = false;
  bool ytmTokenLoaded = false;
  bool uiaCachedValid = false;
  std::string pearToken;
  std::string ytmToken;
  bool ytmLegacy = false;
  bool ytmV1 = false;
  bool uiaReady = false;
  bool uiaCanLike = false;
  bool uiaCanDislike = false;
  ParsedLike uiaLike = ParsedLike::Unknown;
};

bool HttpOk(const HttpResult& result) {
  return result.transportOk && result.status >= 200 && result.status < 300;
}

void ProbePear(HINTERNET session, const SmtcHint& hint, ProbeCache* cache, BackendOffer* offer) {
  if (!session || !cache || !offer || g_stop.load(std::memory_order_acquire)) {
    return;
  }
  const uint64_t now = GetTickCount64();
  if (now < cache->pearRetryAt) {
    return;
  }
  HttpResult song =
      LoopbackRequest(session, kPearDesktopApiPort, L"GET", L"/api/v1/song", nullptr, cache->pearToken,
                      cache->pearToken.empty() ? LoopbackAuthMode::None : LoopbackAuthMode::Bearer,
                      kDesktopRateProbeTimeoutMs);
  if (!song.transportOk) {
    cache->pearRetryAt = now + 2000;
    return;
  }
  if (song.status == 401 || song.status == 403) {
    cache->pearToken.clear();
    if (cache->pearAuthDenied || now < cache->pearAuthRetryAt) {
      cache->pearRetryAt = now + 3000;
      return;
    }
    if (!cache->pearAuthLogged) {
      cache->pearAuthLogged = true;
      LUX_LOGI(L"media-rate: Pear Desktop API requires approval for client dynamic-island-lux");
    }
    const HttpResult auth =
        LoopbackRequest(session, kPearDesktopApiPort, L"POST", L"/auth/dynamic-island-lux", nullptr,
                        "", LoopbackAuthMode::None, 15000);
    std::string token;
    if (HttpOk(auth) && ParsePearAuthJson(auth.body, &token) && SaneToken(token)) {
      cache->pearToken = token;
      song = LoopbackRequest(session, kPearDesktopApiPort, L"GET", L"/api/v1/song", nullptr,
                             cache->pearToken, LoopbackAuthMode::Bearer, kDesktopRateProbeTimeoutMs);
    } else {
      if (auth.status == 403) {
        cache->pearAuthDenied = true;
        cache->pearAuthRetryAt = now + 600000;
      } else {
        cache->pearAuthRetryAt = now + 180000;
      }
      cache->pearRetryAt = now + 3000;
      return;
    }
  }
  if (!song.transportOk) {
    cache->pearRetryAt = now + 2000;
    return;
  }
  if (song.status != 200) {
    cache->pearRetryAt = now + 1500;
    return;
  }
  DesktopApiSong parsed;
  if (!ParsePearSongJson(song.body, &parsed) || !parsed.ok) {
    cache->pearRetryAt = now + 700;
    return;
  }
  offer->pearReachable = true;
  offer->pearTrackMatch = TracksMatch(hint.title, hint.artist, parsed.title, parsed.artist);
  offer->pearCanLike = parsed.canLike;
  offer->pearCanDislike = parsed.canDislike;
  offer->pearLike = parsed.like;
  if (offer->pearTrackMatch) {
    const HttpResult like =
        LoopbackRequest(session, kPearDesktopApiPort, L"GET", L"/api/v1/like-state", nullptr,
                        cache->pearToken,
                        cache->pearToken.empty() ? LoopbackAuthMode::None : LoopbackAuthMode::Bearer,
                        kDesktopRateProbeTimeoutMs);
    ParsedLike state = ParsedLike::Unknown;
    if (like.status == 200 && ParsePearLikeStateJson(like.body, &state) &&
        state != ParsedLike::Unknown) {
      offer->pearLike = state;
    }
  }
  cache->pearRetryAt = now + 400;
}

bool EnsureYtmToken(HINTERNET session, ProbeCache* cache, uint64_t now) {
  if (!session || !cache) {
    return false;
  }
  if (!cache->ytmTokenLoaded) {
    cache->ytmToken = LoadYtmTokenFromDisk();
    cache->ytmTokenLoaded = true;
  }
  if (SaneToken(cache->ytmToken)) {
    return true;
  }
  const YtmAuthAction action =
      DecideYtmAuth(false, true, 401, cache->ytmAuthDenied, now, cache->ytmAuthRetryAt);
  if (action == YtmAuthAction::Backoff || action == YtmAuthAction::Unavailable) {
    return false;
  }
  if (LuxSmokeTestFlag()) {
    cache->ytmAuthRetryAt = now + kYtmAuthFailBackoffMs;
    return false;
  }
  if (!cache->ytmAuthLogged) {
    cache->ytmAuthLogged = true;
    LUX_LOGI(L"media-rate: YTMDesktop Companion Server needs one-time approval "
             L"(Settings → Integrations → Companion Server)");
  }
  const std::string codeBody =
      YtmAuthRequestCodeBody(kYtmCompanionAppIdValue, kYtmCompanionAppNameValue, LUX_APP_VERSION_STR);
  const HttpResult codeResult =
      LoopbackRequest(session, kYtmDesktopApiPort, L"POST", L"/api/v1/auth/requestcode", &codeBody, "",
                      LoopbackAuthMode::None, kYtmAuthCodeTimeoutMs);
  std::string code;
  if (!HttpOk(codeResult) || !ParseYtmAuthCodeJson(codeResult.body, &code) || code.empty() ||
      code.size() > 8) {
    if (codeResult.status == 403 || codeResult.status == 429) {
      cache->ytmAuthDenied = codeResult.status == 403;
      cache->ytmAuthRetryAt = now + kYtmAuthDeniedBackoffMs;
    } else {
      cache->ytmAuthRetryAt = now + kYtmAuthFailBackoffMs;
    }
    return false;
  }
  if (g_stop.load(std::memory_order_acquire)) {
    return false;
  }
  const std::string tokenBody = YtmAuthRequestTokenBody(kYtmCompanionAppIdValue, code.c_str());
  const HttpResult tokenResult =
      LoopbackRequest(session, kYtmDesktopApiPort, L"POST", L"/api/v1/auth/request", &tokenBody, "",
                      LoopbackAuthMode::None, kYtmAuthRequestTimeoutMs);
  std::string token;
  if (HttpOk(tokenResult) && ParseYtmAuthTokenJson(tokenResult.body, &token) && SaneToken(token)) {
    cache->ytmToken = token;
    cache->ytmAuthDenied = false;
    SaveYtmTokenToDisk(token);
    return true;
  }
  if (tokenResult.status == 403) {
    cache->ytmAuthDenied = true;
    cache->ytmAuthRetryAt = now + kYtmAuthDeniedBackoffMs;
  } else {
    cache->ytmAuthRetryAt = now + kYtmAuthFailBackoffMs;
  }
  return false;
}

void ProbeYtm(HINTERNET session, const SmtcHint& hint, ProbeCache* cache, BackendOffer* offer) {
  if (!session || !cache || !offer || g_stop.load(std::memory_order_acquire)) {
    return;
  }
  const uint64_t now = GetTickCount64();
  if (now < cache->ytmRetryAt) {
    return;
  }
  if (!cache->ytmTokenLoaded) {
    cache->ytmToken = LoadYtmTokenFromDisk();
    cache->ytmTokenLoaded = true;
  }

  HttpResult state =
      LoopbackRequest(session, kYtmDesktopApiPort, L"GET", L"/api/v1/state", nullptr, cache->ytmToken,
                      cache->ytmToken.empty() ? LoopbackAuthMode::None : LoopbackAuthMode::RawToken,
                      kDesktopRateProbeTimeoutMs);
  if (!state.transportOk) {
    cache->ytmRetryAt = now + 2000;
    cache->ytmV1 = false;
    cache->ytmLegacy = false;
    return;
  }

  if (state.status == 401 || state.status == 403) {
    if (!cache->ytmToken.empty()) {
      cache->ytmToken.clear();
      ClearYtmTokenOnDisk();
    }
    const YtmAuthAction decision =
        DecideYtmAuth(false, true, state.status, cache->ytmAuthDenied, now, cache->ytmAuthRetryAt);
    if (decision == YtmAuthAction::BeginInteractiveAuth && EnsureYtmToken(session, cache, now)) {
      state = LoopbackRequest(session, kYtmDesktopApiPort, L"GET", L"/api/v1/state", nullptr,
                              cache->ytmToken, LoopbackAuthMode::RawToken, kDesktopRateProbeTimeoutMs);
    } else if (decision != YtmAuthAction::BeginInteractiveAuth || !SaneToken(cache->ytmToken)) {
      // Fall through to legacy /query (v1 Companion without auth).
      const HttpResult legacy =
          LoopbackRequest(session, kYtmDesktopApiPort, L"GET", L"/query", nullptr, "",
                          LoopbackAuthMode::None, kDesktopRateProbeTimeoutMs);
      DesktopApiSong parsed;
      if (legacy.status == 200 && ParseYtmLegacyQueryJson(legacy.body, &parsed) && parsed.ok) {
        offer->ytmReachable = true;
        offer->ytmLegacy = true;
        offer->ytmTrackMatch = TracksMatch(hint.title, hint.artist, parsed.title, parsed.artist);
        offer->ytmCanLike = parsed.canLike;
        offer->ytmCanDislike = parsed.canDislike;
        offer->ytmLike = parsed.like;
        cache->ytmLegacy = true;
        cache->ytmV1 = false;
        cache->ytmRetryAt = now + 400;
        return;
      }
      cache->ytmRetryAt = now + 2000;
      return;
    }
  }

  DesktopApiSong parsed;
  bool usedLegacy = false;
  if (state.transportOk && state.status == 200 && ParseYtmV1StateJson(state.body, &parsed) &&
      parsed.ok) {
    cache->ytmV1 = true;
    cache->ytmLegacy = false;
  } else {
    const HttpResult legacy =
        LoopbackRequest(session, kYtmDesktopApiPort, L"GET", L"/query", nullptr, "",
                        LoopbackAuthMode::None, kDesktopRateProbeTimeoutMs);
    if (!legacy.transportOk) {
      cache->ytmRetryAt = now + 2000;
      return;
    }
    if (legacy.status == 200 && ParseYtmLegacyQueryJson(legacy.body, &parsed) && parsed.ok) {
      usedLegacy = true;
      cache->ytmLegacy = true;
      cache->ytmV1 = false;
    } else {
      cache->ytmRetryAt = now + 1500;
      return;
    }
  }
  offer->ytmReachable = true;
  offer->ytmLegacy = usedLegacy || cache->ytmLegacy;
  offer->ytmTrackMatch = TracksMatch(hint.title, hint.artist, parsed.title, parsed.artist);
  offer->ytmCanLike = parsed.canLike;
  offer->ytmCanDislike = parsed.canDislike;
  offer->ytmLike = parsed.like;
  cache->ytmRetryAt = now + 400;
}

struct LiveWindow {
  HWND hwnd = nullptr;
  WindowIdentity id;
};

BOOL CALLBACK EnumPlayerWindows(HWND hwnd, LPARAM param) {
  auto* windows = reinterpret_cast<std::vector<LiveWindow>*>(param);
  if (!hwnd || !IsWindow(hwnd) || GetAncestor(hwnd, GA_ROOT) != hwnd) {
    return TRUE;
  }
  DWORD pid = 0;
  GetWindowThreadProcessId(hwnd, &pid);
  if (pid == 0 || pid == GetCurrentProcessId()) {
    return TRUE;
  }
  const bool iconic = IsIconic(hwnd) == TRUE;
  BOOL cloaked = FALSE;
  if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked &&
      !iconic) {
    return TRUE;
  }
  if (!iconic && !IsWindowVisible(hwnd)) {
    return TRUE;
  }
  wchar_t className[128] = {};
  GetClassNameW(hwnd, className, 128);
  const std::wstring klass = className;
  if (klass == L"Shell_TrayWnd" || klass == L"Progman" || klass == L"WorkerW") {
    return TRUE;
  }
  wchar_t title[512] = {};
  GetWindowTextW(hwnd, title, 512);
  RECT rect{};
  GetWindowRect(hwnd, &rect);
  const int width = rect.right - rect.left;
  const int height = rect.bottom - rect.top;
  if (!iconic && (width < 80 || height < 40)) {
    return TRUE;
  }
  HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!process) {
    return TRUE;
  }
  wchar_t image[1024] = {};
  DWORD imageChars = 1024;
  const BOOL haveImage = QueryFullProcessImageNameW(process, 0, image, &imageChars);
  UINT32 aumidChars = 0;
  std::wstring aumid;
  const LONG need = GetApplicationUserModelId(process, &aumidChars, nullptr);
  if (need == ERROR_INSUFFICIENT_BUFFER && aumidChars > 1 && aumidChars < 1024) {
    aumid.resize(aumidChars);
    if (GetApplicationUserModelId(process, &aumidChars, aumid.data()) == ERROR_SUCCESS) {
      while (!aumid.empty() && aumid.back() == L'\0') {
        aumid.pop_back();
      }
    } else {
      aumid.clear();
    }
  }
  CloseHandle(process);
  if (!haveImage) {
    return TRUE;
  }
  LiveWindow window;
  window.hwnd = hwnd;
  window.id.imageBase = BaseNameLower(WideToUtf8(image, static_cast<int>(imageChars)));
  window.id.title = WideToUtf8(title, -1);
  window.id.aumid = WideToUtf8(aumid);
  window.id.iconic = iconic;
  window.id.visible = !iconic;
  window.id.width = width;
  window.id.height = height;
  if (windows->size() < 64) {
    windows->push_back(std::move(window));
  }
  return TRUE;
}

bool TakePattern(IUIAutomationElement* element, PATTERNID id, REFIID iid, void** out) {
  if (!element || !out) {
    return false;
  }
  *out = nullptr;
  return SUCCEEDED(element->GetCurrentPatternAs(id, iid, out)) && *out != nullptr;
}

UiaCandidate ReadCandidate(IUIAutomationElement* element) {
  UiaCandidate candidate;
  if (!element) {
    return candidate;
  }
  BSTR name = nullptr;
  if (SUCCEEDED(element->get_CurrentName(&name)) && name) {
    candidate.name = WideToUtf8(name, static_cast<int>(SysStringLen(name)));
    SysFreeString(name);
  }
  BSTR automationId = nullptr;
  if (SUCCEEDED(element->get_CurrentAutomationId(&automationId)) && automationId) {
    candidate.automationId =
        WideToUtf8(automationId, static_cast<int>(SysStringLen(automationId)));
    SysFreeString(automationId);
  }
  CONTROLTYPEID controlType = 0;
  if (SUCCEEDED(element->get_CurrentControlType(&controlType))) {
    candidate.controlType = static_cast<int>(controlType);
  }
  BOOL offscreen = FALSE;
  if (SUCCEEDED(element->get_CurrentIsOffscreen(&offscreen))) {
    candidate.offscreen = offscreen == TRUE;
  }
  BOOL enabled = TRUE;
  if (SUCCEEDED(element->get_CurrentIsEnabled(&enabled))) {
    candidate.enabled = enabled != FALSE;
  }
  RECT rect{};
  if (SUCCEEDED(element->get_CurrentBoundingRectangle(&rect))) {
    candidate.left = rect.left;
    candidate.top = rect.top;
    candidate.width = static_cast<double>(rect.right) - rect.left;
    candidate.height = static_cast<double>(rect.bottom) - rect.top;
  }
  IUIAutomationTogglePattern* toggle = nullptr;
  if (TakePattern(element, UIA_TogglePatternId, IID_IUIAutomationTogglePattern,
                  reinterpret_cast<void**>(&toggle))) {
    candidate.hasToggle = true;
    ToggleState state = ToggleState_Indeterminate;
    if (SUCCEEDED(toggle->get_CurrentToggleState(&state))) {
      candidate.toggleState = static_cast<int>(state);
    }
    toggle->Release();
  }
  IUIAutomationInvokePattern* invoke = nullptr;
  if (TakePattern(element, UIA_InvokePatternId, IID_IUIAutomationInvokePattern,
                  reinterpret_cast<void**>(&invoke))) {
    candidate.hasInvoke = true;
    invoke->Release();
  }
  IUIAutomationLegacyIAccessiblePattern* legacy = nullptr;
  if (TakePattern(element, UIA_LegacyIAccessiblePatternId, IID_IUIAutomationLegacyIAccessiblePattern,
                  reinterpret_cast<void**>(&legacy))) {
    candidate.hasLegacy = true;
    DWORD state = 0;
    if (SUCCEEDED(legacy->get_CurrentState(&state))) {
      candidate.pressed = (state & 0x8) != 0;
    }
    legacy->Release();
  }
  return candidate;
}

bool InvokeElement(IUIAutomationElement* element) {
  if (!element) {
    return false;
  }
  IUIAutomationTogglePattern* toggle = nullptr;
  if (TakePattern(element, UIA_TogglePatternId, IID_IUIAutomationTogglePattern,
                  reinterpret_cast<void**>(&toggle))) {
    const HRESULT hr = toggle->Toggle();
    toggle->Release();
    if (SUCCEEDED(hr)) {
      return true;
    }
  }
  IUIAutomationInvokePattern* invoke = nullptr;
  if (TakePattern(element, UIA_InvokePatternId, IID_IUIAutomationInvokePattern,
                  reinterpret_cast<void**>(&invoke))) {
    const HRESULT hr = invoke->Invoke();
    invoke->Release();
    if (SUCCEEDED(hr)) {
      return true;
    }
  }
  IUIAutomationLegacyIAccessiblePattern* legacy = nullptr;
  if (TakePattern(element, UIA_LegacyIAccessiblePatternId, IID_IUIAutomationLegacyIAccessiblePattern,
                  reinterpret_cast<void**>(&legacy))) {
    const HRESULT hr = legacy->DoDefaultAction();
    legacy->Release();
    return SUCCEEDED(hr);
  }
  return false;
}

bool CollectControls(IUIAutomation* automation, IUIAutomationElement* root, bool includeCustom,
                     std::vector<UiaCandidate>* candidates,
                     std::vector<Microsoft::WRL::ComPtr<IUIAutomationElement>>* elements) {
  if (!automation || !root || !candidates || !elements) {
    return false;
  }
  VARIANT buttonValue;
  VariantInit(&buttonValue);
  buttonValue.vt = VT_I4;
  buttonValue.lVal = UIA_ButtonControlTypeId;
  VARIANT checkValue;
  VariantInit(&checkValue);
  checkValue.vt = VT_I4;
  checkValue.lVal = UIA_CheckBoxControlTypeId;
  VARIANT splitValue;
  VariantInit(&splitValue);
  splitValue.vt = VT_I4;
  splitValue.lVal = UIA_SplitButtonControlTypeId;
  Microsoft::WRL::ComPtr<IUIAutomationCondition> button;
  Microsoft::WRL::ComPtr<IUIAutomationCondition> check;
  Microsoft::WRL::ComPtr<IUIAutomationCondition> split;
  Microsoft::WRL::ComPtr<IUIAutomationCondition> either;
  Microsoft::WRL::ComPtr<IUIAutomationCondition> buttons;
  const bool created =
      SUCCEEDED(automation->CreatePropertyCondition(UIA_ControlTypePropertyId, buttonValue,
                                                    &button)) &&
      SUCCEEDED(automation->CreatePropertyCondition(UIA_ControlTypePropertyId, checkValue, &check)) &&
      SUCCEEDED(automation->CreatePropertyCondition(UIA_ControlTypePropertyId, splitValue, &split)) &&
      SUCCEEDED(automation->CreateOrCondition(button.Get(), check.Get(), &either)) &&
      SUCCEEDED(automation->CreateOrCondition(either.Get(), split.Get(), &buttons));
  VariantClear(&buttonValue);
  VariantClear(&checkValue);
  VariantClear(&splitValue);
  Microsoft::WRL::ComPtr<IUIAutomationCondition> filter = buttons;
  if (includeCustom) {
    VARIANT customValue;
    VariantInit(&customValue);
    customValue.vt = VT_I4;
    customValue.lVal = UIA_CustomControlTypeId;
    Microsoft::WRL::ComPtr<IUIAutomationCondition> custom;
    Microsoft::WRL::ComPtr<IUIAutomationCondition> withCustom;
    if (created &&
        SUCCEEDED(automation->CreatePropertyCondition(UIA_ControlTypePropertyId, customValue,
                                                      &custom)) &&
        SUCCEEDED(automation->CreateOrCondition(buttons.Get(), custom.Get(), &withCustom))) {
      filter = withCustom;
    }
    VariantClear(&customValue);
  }
  if (!created || !filter) {
    return false;
  }
  Microsoft::WRL::ComPtr<IUIAutomationElementArray> array;
  if (FAILED(root->FindAll(TreeScope_Descendants, filter.Get(), &array)) || !array) {
    return false;
  }
  int length = 0;
  array->get_Length(&length);
  if (length > 400) {
    length = 400;
  }
  for (int i = 0; i < length; ++i) {
    if (g_stop.load(std::memory_order_acquire)) {
      return false;
    }
    Microsoft::WRL::ComPtr<IUIAutomationElement> element;
    if (FAILED(array->GetElement(i, &element)) || !element) {
      continue;
    }
    UiaCandidate candidate = ReadCandidate(element.Get());
    const UiaRateKind kind = ClassifyUiaRateControl(candidate.name, candidate.automationId);
    if (kind == UiaRateKind::None || kind == UiaRateKind::Rejected) {
      continue;
    }
    candidates->push_back(std::move(candidate));
    elements->push_back(std::move(element));
  }
  return true;
}

void ApplySelection(const UiaRateSelection& pick, BackendOffer* offer) {
  if (!offer) {
    return;
  }
  offer->uiaCanLike = pick.likeIndex >= 0;
  offer->uiaCanDislike = pick.dislikeIndex >= 0;
  offer->uiaReady = offer->uiaCanLike || offer->uiaCanDislike;
  if (pick.likeActive) {
    offer->uiaLike = ParsedLike::Liked;
  } else if (pick.dislikeActive) {
    offer->uiaLike = ParsedLike::Disliked;
  } else if (offer->uiaReady) {
    offer->uiaLike = ParsedLike::None;
  }
}

bool ScanWindow(IUIAutomation* automation, HWND hwnd, const WindowIdentity& identity,
                BackendOffer* offer,
                std::vector<Microsoft::WRL::ComPtr<IUIAutomationElement>>* elements,
                UiaRateSelection* pickOut) {
  if (!automation || !hwnd || !offer) {
    return false;
  }
  Microsoft::WRL::ComPtr<IUIAutomationElement> root;
  if (FAILED(automation->ElementFromHandle(hwnd, &root)) || !root) {
    return false;
  }
  std::vector<UiaCandidate> candidates;
  std::vector<Microsoft::WRL::ComPtr<IUIAutomationElement>> found;
  if (!CollectControls(automation, root.Get(), false, &candidates, &found)) {
    return false;
  }
  const DesktopWindowKind kind = ClassifyDesktopWindow(identity.imageBase, identity.title);
  const bool preferBar = PreferPlayerBar(kind, identity.title);
  const bool trust = !identity.iconic && identity.width >= 80 && identity.height >= 40;
  UiaWindowBox box;
  box.left = 0;
  box.top = 0;
  box.width = identity.width;
  box.height = identity.height;
  RECT rect{};
  if (GetWindowRect(hwnd, &rect)) {
    box.left = rect.left;
    box.top = rect.top;
    box.width = static_cast<double>(rect.right) - rect.left;
    box.height = static_cast<double>(rect.bottom) - rect.top;
  }
  UiaRateSelection pick = SelectUiaRateControls(candidates, box, preferBar, trust);
  if (pick.likeIndex < 0 && pick.dislikeIndex < 0) {
    // YTMDesktop/Electron often exposes only window chrome. A second full-tree walk
    // with custom AutomationIds rarely helps and burns CPU — skip it.
    if (kind != DesktopWindowKind::YouTubeMusicDesktop) {
      candidates.clear();
      found.clear();
      if (!CollectControls(automation, root.Get(), true, &candidates, &found)) {
        return false;
      }
      pick = SelectUiaRateControls(candidates, box, preferBar, trust);
    }
  }
  ApplySelection(pick, offer);
  if (elements) {
    *elements = std::move(found);
  }
  if (pickOut) {
    *pickOut = pick;
  }
  return true;
}

void ProbeUia(IUIAutomation* automation, const SmtcHint& hint, const std::string& aumid,
              ProbeCache* cache, BackendOffer* offer) {
  if (!automation || !cache || !offer || g_stop.load(std::memory_order_acquire)) {
    return;
  }
  const SourceRoute route = ClassifyPlaybackSource(hint.source, aumid);
  if (!route.allowUiAutomation) {
    return;
  }
  const uint64_t now = GetTickCount64();
  if (now < cache->uiaRetryAt && cache->uiaCachedValid) {
    offer->uiaReady = cache->uiaReady;
    offer->uiaCanLike = cache->uiaCanLike;
    offer->uiaCanDislike = cache->uiaCanDislike;
    offer->uiaLike = cache->uiaLike;
    return;
  }
  std::vector<LiveWindow> windows;
  EnumWindows(EnumPlayerWindows, reinterpret_cast<LPARAM>(&windows));
  const LiveWindow* best = nullptr;
  int bestScore = 0;
  for (const LiveWindow& window : windows) {
    const int score = ScorePlayerWindow(route, hint.source, aumid, hint.title, window.id);
    if (score > bestScore) {
      bestScore = score;
      best = &window;
    }
  }
  if (!best || bestScore <= 0) {
    cache->uiaCachedValid = true;
    cache->uiaReady = false;
    cache->uiaCanLike = false;
    cache->uiaCanDislike = false;
    cache->uiaLike = ParsedLike::Unknown;
    cache->uiaRetryAt = now + 4000;
    return;
  }
  ScanWindow(automation, best->hwnd, best->id, offer, nullptr, nullptr);
  cache->uiaCachedValid = true;
  cache->uiaReady = offer->uiaReady;
  cache->uiaCanLike = offer->uiaCanLike;
  cache->uiaCanDislike = offer->uiaCanDislike;
  cache->uiaLike = offer->uiaLike;
  // Electron shells (YTMDesktop) often expose only chrome buttons — back off hard.
  const bool thinTree = !offer->uiaReady &&
                        ClassifyDesktopWindow(best->id.imageBase, best->id.title) ==
                            DesktopWindowKind::YouTubeMusicDesktop;
  cache->uiaRetryAt = now + (offer->uiaReady ? 2500 : (thinTree ? 8000 : 4500));
}

bool SendPear(HINTERNET session, ProbeCache* cache, RateAction action) {
  const wchar_t* path = action == RateAction::Dislike ? L"/api/v1/dislike" : L"/api/v1/like";
  HttpResult result =
      LoopbackRequest(session, kPearDesktopApiPort, L"POST", path, nullptr, cache->pearToken,
                      cache->pearToken.empty() ? LoopbackAuthMode::None : LoopbackAuthMode::Bearer,
                      kDesktopRateProbeTimeoutMs);
  if (result.status == 401 || result.status == 403) {
    cache->pearToken.clear();
    return false;
  }
  return HttpOk(result);
}

bool SendYtm(HINTERNET session, ProbeCache* cache, RateAction action) {
  if (!cache) {
    return false;
  }
  if (cache->ytmV1 && !cache->ytmLegacy) {
    const std::string body = YtmV1CommandBody(action);
    const HttpResult result = LoopbackRequest(
        session, kYtmDesktopApiPort, L"POST", L"/api/v1/command", &body, cache->ytmToken,
        cache->ytmToken.empty() ? LoopbackAuthMode::None : LoopbackAuthMode::RawToken,
        kDesktopRateProbeTimeoutMs);
    if (result.status == 401 || result.status == 403) {
      cache->ytmToken.clear();
      ClearYtmTokenOnDisk();
      return false;
    }
    return HttpOk(result);
  }
  const std::string legacyBody =
      std::string("{\"command\":\"") + YtmLegacyCommand(action) + "\"}";
  HttpResult result =
      LoopbackRequest(session, kYtmDesktopApiPort, L"POST", L"/query", &legacyBody, "",
                      LoopbackAuthMode::None, kDesktopRateProbeTimeoutMs);
  if (HttpOk(result)) {
    return true;
  }
  const wchar_t* path =
      action == RateAction::Dislike ? L"/query/track-thumbs-down" : L"/query/track-thumbs-up";
  result = LoopbackRequest(session, kYtmDesktopApiPort, L"GET", path, nullptr, "",
                           LoopbackAuthMode::None, kDesktopRateProbeTimeoutMs);
  return HttpOk(result);
}

bool PerformCommand(const CommandSlot& command, HINTERNET session, IUIAutomation* automation,
                    ProbeCache* cache) {
  if (!cache || g_stop.load(std::memory_order_acquire)) {
    return false;
  }
  SessionState latest;
  {
    std::lock_guard<std::mutex> lock(g_mu);
    latest = g_session;
  }
  if (!latest.valid || latest.serial != command.serial) {
    return false;
  }
  const SourceRoute route = ClassifyPlaybackSource(command.hint.source, command.aumid);
  switch (command.backend) {
  case DesktopRateBackend::None:
  case DesktopRateBackend::BrowserBridge:
    return false;
  case DesktopRateBackend::PearDesktop: {
    if (!route.allowPear) {
      return false;
    }
    BackendOffer offer;
    cache->pearRetryAt = 0;
    ProbePear(session, command.hint, cache, &offer);
    if (!offer.pearTrackMatch) {
      return false;
    }
    if (command.action == RateAction::Like && !offer.pearCanLike) {
      return false;
    }
    if (command.action == RateAction::Dislike && !offer.pearCanDislike) {
      return false;
    }
    return SendPear(session, cache, command.action);
  }
  case DesktopRateBackend::YtmDesktop: {
    if (!route.allowYtmDesktop) {
      return false;
    }
    BackendOffer offer;
    cache->ytmRetryAt = 0;
    ProbeYtm(session, command.hint, cache, &offer);
    if (!offer.ytmTrackMatch) {
      return false;
    }
    if (command.action == RateAction::Like && !offer.ytmCanLike) {
      return false;
    }
    if (command.action == RateAction::Dislike && !offer.ytmCanDislike) {
      return false;
    }
    return SendYtm(session, cache, command.action);
  }
  case DesktopRateBackend::UiAutomation: {
    if (!automation || !route.allowUiAutomation) {
      return false;
    }
    cache->uiaRetryAt = 0;
    cache->uiaCachedValid = false;
    std::vector<LiveWindow> windows;
    EnumWindows(EnumPlayerWindows, reinterpret_cast<LPARAM>(&windows));
    const LiveWindow* best = nullptr;
    int bestScore = 0;
    for (const LiveWindow& window : windows) {
      const int score =
          ScorePlayerWindow(route, command.hint.source, command.aumid, command.hint.title, window.id);
      if (score > bestScore) {
        bestScore = score;
        best = &window;
      }
    }
    if (!best) {
      return false;
    }
    BackendOffer offer;
    std::vector<Microsoft::WRL::ComPtr<IUIAutomationElement>> elements;
    UiaRateSelection pick;
    if (!ScanWindow(automation, best->hwnd, best->id, &offer, &elements, &pick)) {
      return false;
    }
    const int index = command.action == RateAction::Dislike ? pick.dislikeIndex : pick.likeIndex;
    if (index < 0 || index >= static_cast<int>(elements.size())) {
      return false;
    }
    if (!InvokeElement(elements[static_cast<size_t>(index)].Get())) {
      return false;
    }
    for (int i = 0; i < 12 && !g_stop.load(std::memory_order_acquire); ++i) {
      Sleep(10);
    }
    return true;
  }
  }
  const DesktopRateBackend unhandled = command.backend;
  (void)unhandled;
  return false;
}

bool OffersEqual(const BackendOffer& left, const BackendOffer& right) {
  return left.pearReachable == right.pearReachable && left.pearTrackMatch == right.pearTrackMatch &&
         left.pearCanLike == right.pearCanLike && left.pearCanDislike == right.pearCanDislike &&
         left.pearLike == right.pearLike && left.ytmReachable == right.ytmReachable &&
         left.ytmTrackMatch == right.ytmTrackMatch && left.ytmCanLike == right.ytmCanLike &&
         left.ytmCanDislike == right.ytmCanDislike && left.ytmLike == right.ytmLike &&
         left.ytmLegacy == right.ytmLegacy && left.uiaReady == right.uiaReady &&
         left.uiaCanLike == right.uiaCanLike && left.uiaCanDislike == right.uiaCanDislike &&
         left.uiaLike == right.uiaLike;
}

void PublishOffer(uint64_t serial, const BackendOffer& offer) {
  std::lock_guard<std::mutex> lock(g_mu);
  if (serial != g_session.serial) {
    return;
  }
  g_snapshot.offer = offer;
  g_snapshot.serial = serial;
  g_snapshot.tick = GetTickCount64();
  g_snapshot.fresh = true;
}

DWORD WINAPI DesktopRateThread(LPVOID) {
  const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  const bool uninit = com == S_OK || com == S_FALSE;
  Microsoft::WRL::ComPtr<IUIAutomation> automation;
  if (com == S_OK || com == S_FALSE) {
    CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation));
  }
  HINTERNET session = WinHttpOpen(L"DynamicIslandLux/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY,
                                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (session) {
    WinHttpSetTimeouts(session, kDesktopRateProbeTimeoutMs, kDesktopRateProbeTimeoutMs,
                       kDesktopRateProbeTimeoutMs, kDesktopRateProbeTimeoutMs);
  }
  ProbeCache cache;
  BackendOffer previous;
  while (!g_stop.load(std::memory_order_acquire)) {
    CommandSlot command;
    bool hasCommand = false;
    SessionState sessionState;
    bool hasSession = false;
    {
      std::lock_guard<std::mutex> lock(g_mu);
      if (g_command.has) {
        command = g_command;
        g_command.has = false;
        hasCommand = true;
      }
      if (g_session.valid) {
        sessionState = g_session;
        hasSession = true;
      }
    }
    bool performed = false;
    if (hasCommand) {
      performed = PerformCommand(command, session, automation.Get(), &cache);
    }
    BackendOffer offer;
    if (hasSession && !g_stop.load(std::memory_order_acquire)) {
      const SourceRoute route = ClassifyPlaybackSource(sessionState.source, sessionState.aumid);
      const bool skipDesktop = sessionState.browserCovers && !route.preferDesktopApi;
      SmtcHint hint;
      hint.title = sessionState.title;
      hint.artist = sessionState.artist;
      hint.source = sessionState.source;
      hint.playing = sessionState.playing;
      // One successful desktop API match is enough — skip the rest this cycle.
      if (!skipDesktop && route.allowPear) {
        ProbePear(session, hint, &cache, &offer);
      }
      if (!skipDesktop && route.allowYtmDesktop && !offer.pearTrackMatch &&
          !g_stop.load(std::memory_order_acquire)) {
        ProbeYtm(session, hint, &cache, &offer);
      }
      const bool apiMatched = offer.pearTrackMatch || offer.ytmTrackMatch;
      if (!skipDesktop && route.allowUiAutomation && !apiMatched &&
          !g_stop.load(std::memory_order_acquire)) {
        ProbeUia(automation.Get(), hint, sessionState.aumid, &cache, &offer);
      }
      PublishOffer(sessionState.serial, offer);
    }
    if (hasCommand) {
      MediaRateNoteResult(command.generation, performed, command.action);
      MediaRateNotifyUi();
    } else if (hasSession && !OffersEqual(previous, offer)) {
      previous = offer;
      MediaRateNotifyUi();
    }
    if (g_stop.load(std::memory_order_acquire)) {
      break;
    }
    // Prefer long sleeps: HTTP/UIA probes are the expensive part, not latency to Like.
    DWORD waitMs = 1500;
    if (hasSession) {
      waitMs = sessionState.playing ? 900 : 1200;
    }
    if (hasCommand) {
      waitMs = 200;
    }
    if (g_event) {
      WaitForSingleObject(g_event, waitMs);
    } else {
      Sleep(waitMs);
    }
  }
  if (session) {
    WinHttpCloseHandle(session);
  }
  automation.Reset();
  if (uninit) {
    CoUninitialize();
  }
  return 0;
}

}  // namespace

SourceRoute ClassifyPlaybackSource(const std::string& sourceName, const std::string& appUserModelId) {
  const std::string raw = LowerAscii(sourceName) + "\n" + LowerAscii(appUserModelId);
  const std::string words = WordBlob(raw);
  if (DesktopBlob(raw)) {
    return RouteFor(PlayerFamily::YouTubeMusicDesktop);
  }
  if (ContainsPhrase(words, "spotify")) {
    return RouteFor(PlayerFamily::Spotify);
  }
  if (ContainsPhrase(words, "tidal")) {
    return RouteFor(PlayerFamily::Tidal);
  }
  if (ContainsPhrase(words, "deezer")) {
    return RouteFor(PlayerFamily::Deezer);
  }
  if (raw.find("applemusic") != std::string::npos || raw.find("apple music") != std::string::npos) {
    return RouteFor(PlayerFamily::AppleMusic);
  }
  if (BrowserFromText(raw) != BrowserProduct::None) {
    return RouteFor(PlayerFamily::Browser);
  }
  if (raw.find("youtube") != std::string::npos) {
    return RouteFor(PlayerFamily::YouTubeAmbiguous);
  }
  return RouteFor(PlayerFamily::None);
}

BackendChoice ChooseRateBackend(const SourceRoute& route, const BackendOffer& offer) {
  const bool pearOk = route.allowPear && offer.pearReachable && offer.pearTrackMatch &&
                      (offer.pearCanLike || offer.pearCanDislike);
  const bool ytmOk = route.allowYtmDesktop && offer.ytmReachable && offer.ytmTrackMatch &&
                     (offer.ytmCanLike || offer.ytmCanDislike);
  const bool browserOk = route.allowBrowserBridge && offer.browserMatch;
  const bool uiaOk = route.allowUiAutomation && offer.uiaReady &&
                     (offer.uiaCanLike || offer.uiaCanDislike);
  if (route.preferDesktopApi) {
    if (pearOk) {
      return ChoiceFrom(DesktopRateBackend::PearDesktop, offer.pearCanLike, offer.pearCanDislike,
                        offer.pearLike);
    }
    if (ytmOk) {
      return ChoiceFrom(DesktopRateBackend::YtmDesktop, offer.ytmCanLike, offer.ytmCanDislike,
                        offer.ytmLike);
    }
    if (browserOk) {
      return ChoiceFrom(DesktopRateBackend::BrowserBridge, offer.browserCanLike,
                        offer.browserCanDislike, offer.browserLike);
    }
    if (uiaOk) {
      return ChoiceFrom(DesktopRateBackend::UiAutomation, offer.uiaCanLike, offer.uiaCanDislike,
                        offer.uiaLike);
    }
  } else {
    if (browserOk) {
      return ChoiceFrom(DesktopRateBackend::BrowserBridge, offer.browserCanLike,
                        offer.browserCanDislike, offer.browserLike);
    }
    if (pearOk) {
      return ChoiceFrom(DesktopRateBackend::PearDesktop, offer.pearCanLike, offer.pearCanDislike,
                        offer.pearLike);
    }
    if (ytmOk) {
      return ChoiceFrom(DesktopRateBackend::YtmDesktop, offer.ytmCanLike, offer.ytmCanDislike,
                        offer.ytmLike);
    }
    if (uiaOk) {
      return ChoiceFrom(DesktopRateBackend::UiAutomation, offer.uiaCanLike, offer.uiaCanDislike,
                        offer.uiaLike);
    }
  }
  return {};
}

RateCapability DescribeRateCapability(const BackendChoice& choice) {
  RateCapability capability;
  capability.canLike = choice.backend != DesktopRateBackend::None && choice.canLike;
  capability.canDislike = choice.backend != DesktopRateBackend::None && choice.canDislike;
  capability.likeOnly = capability.canLike && !capability.canDislike;
  capability.show = capability.canLike || capability.canDislike;
  return capability;
}

bool TracksMatch(const std::string& hintTitle, const std::string& hintArtist,
                 const std::string& remoteTitle, const std::string& remoteArtist) {
  if (NormalizeMatch(hintTitle).empty() || NormalizeMatch(remoteTitle).empty()) {
    return false;
  }
  const int title = Similarity(hintTitle, remoteTitle);
  const int artist = Similarity(hintArtist, remoteArtist);
  const bool artistKnown =
      !NormalizeMatch(hintArtist).empty() && !NormalizeMatch(remoteArtist).empty();
  if (artistKnown && artist < 40) {
    return false;
  }
  if (title >= 86) {
    return true;
  }
  return title >= 70 && (!artistKnown || artist >= 40);
}

bool ParsePearSongJson(const std::string& json, DesktopApiSong* out) {
  if (!out) {
    return false;
  }
  *out = {};
  JVal root;
  if (!ParseJson(json, &root) || root.type != JVal::Type::Object) {
    return false;
  }
  const JVal* title = FindField(root, "title");
  if (!title || title->type != JVal::Type::String || title->s.empty()) {
    return false;
  }
  out->title = ClipText(title->s);
  out->artist = ReadArtist(root);
  bool paused = false;
  if (ReadBool(root, "isPaused", &paused)) {
    out->playingKnown = true;
    out->playing = !paused;
  }
  ApplyLikeField(root, "likeStatus", &out->like, false);
  out->canLike = true;
  out->canDislike = true;
  out->ok = true;
  return true;
}

bool ParsePearLikeStateJson(const std::string& json, ParsedLike* out) {
  if (!out) {
    return false;
  }
  *out = ParsedLike::Unknown;
  JVal root;
  if (!ParseJson(json, &root) || root.type != JVal::Type::Object) {
    return false;
  }
  const JVal* state = FindField(root, "state");
  if (!state) {
    return false;
  }
  if (state->type == JVal::Type::Null) {
    *out = ParsedLike::Unknown;
    return true;
  }
  if (state->type != JVal::Type::String) {
    return false;
  }
  *out = ParseLikeToken(*state);
  return *out != ParsedLike::Unknown;
}

bool ParsePearAuthJson(const std::string& json, std::string* tokenOut) {
  if (!tokenOut) {
    return false;
  }
  tokenOut->clear();
  JVal root;
  if (!ParseJson(json, &root) || root.type != JVal::Type::Object) {
    return false;
  }
  const JVal* token = FindField(root, "accessToken");
  if (!token || token->type != JVal::Type::String || token->s.size() < 8 || token->s.size() > 4096) {
    return false;
  }
  *tokenOut = token->s;
  return true;
}

bool ParseYtmAuthCodeJson(const std::string& json, std::string* codeOut) {
  if (!codeOut) {
    return false;
  }
  codeOut->clear();
  JVal root;
  if (!ParseJson(json, &root) || root.type != JVal::Type::Object) {
    return false;
  }
  const JVal* code = FindField(root, "code");
  if (!code || code->type != JVal::Type::String || code->s.empty() || code->s.size() > 8) {
    return false;
  }
  for (unsigned char ch : code->s) {
    if (ch < '0' || ch > '9') {
      return false;
    }
  }
  *codeOut = code->s;
  return true;
}

bool ParseYtmAuthTokenJson(const std::string& json, std::string* tokenOut) {
  if (!tokenOut) {
    return false;
  }
  tokenOut->clear();
  JVal root;
  if (!ParseJson(json, &root) || root.type != JVal::Type::Object) {
    return false;
  }
  const JVal* token = FindField(root, "token");
  if (!token || token->type != JVal::Type::String || token->s.size() < 8 || token->s.size() > 4096) {
    return false;
  }
  *tokenOut = token->s;
  return true;
}

bool ParseYtmTokenStore(const std::string& text, std::string* tokenOut) {
  if (!tokenOut) {
    return false;
  }
  tokenOut->clear();
  size_t begin = 0;
  while (begin < text.size() &&
         (text[begin] == ' ' || text[begin] == '\t' || text[begin] == '\r' || text[begin] == '\n')) {
    ++begin;
  }
  size_t end = text.size();
  while (end > begin &&
         (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r' ||
          text[end - 1] == '\n')) {
    --end;
  }
  if (end <= begin) {
    return false;
  }
  *tokenOut = text.substr(begin, end - begin);
  return true;
}

std::string FormatYtmTokenStore(const std::string& token) { return token + "\n"; }

std::string YtmAuthRequestCodeBody(const char* appId, const char* appName, const char* appVersion) {
  if (!appId || !appName || !appVersion) {
    return {};
  }
  return std::string("{\"appId\":\"") + appId + "\",\"appName\":\"" + appName +
         "\",\"appVersion\":\"" + appVersion + "\"}";
}

std::string YtmAuthRequestTokenBody(const char* appId, const char* code) {
  if (!appId || !code) {
    return {};
  }
  return std::string("{\"appId\":\"") + appId + "\",\"code\":\"" + code + "\"}";
}

YtmAuthAction DecideYtmAuth(bool haveToken, bool transportOk, int httpStatus, bool authDenied,
                            uint64_t nowMs, uint64_t authRetryAtMs) {
  if (!transportOk) {
    return YtmAuthAction::Unavailable;
  }
  if (haveToken && httpStatus != 401 && httpStatus != 403) {
    return YtmAuthAction::ProbeWithToken;
  }
  if (authDenied || nowMs < authRetryAtMs) {
    return YtmAuthAction::Backoff;
  }
  return YtmAuthAction::BeginInteractiveAuth;
}

const char* YtmCompanionAppId() { return kYtmCompanionAppIdValue; }

const char* YtmCompanionAppName() { return kYtmCompanionAppNameValue; }

bool ParseYtmLegacyQueryJson(const std::string& json, DesktopApiSong* out) {
  if (!out) {
    return false;
  }
  *out = {};
  JVal root;
  if (!ParseJson(json, &root) || root.type != JVal::Type::Object) {
    return false;
  }
  const JVal* player = FindField(root, "player");
  const JVal* track = FindField(root, "track");
  if (!player || !track || player->type != JVal::Type::Object || track->type != JVal::Type::Object) {
    return false;
  }
  bool hasSong = true;
  if (ReadBool(*player, "hasSong", &hasSong) && !hasSong) {
    return true;
  }
  bool advertisement = false;
  if (ReadBool(*track, "isAdvertisement", &advertisement) && advertisement) {
    return true;
  }
  const JVal* title = FindField(*track, "title");
  if (!title || title->type != JVal::Type::String || title->s.empty()) {
    return true;
  }
  out->title = ClipText(title->s);
  out->artist = ReadArtist(*track);
  bool paused = false;
  if (ReadBool(*player, "isPaused", &paused)) {
    out->playingKnown = true;
    out->playing = !paused;
  }
  ApplyLikeField(*player, "likeStatus", &out->like, false);
  out->canLike = true;
  out->canDislike = true;
  out->ok = true;
  return true;
}

bool ParseYtmV1StateJson(const std::string& json, DesktopApiSong* out) {
  if (!out) {
    return false;
  }
  *out = {};
  JVal root;
  if (!ParseJson(json, &root) || root.type != JVal::Type::Object) {
    return false;
  }
  const JVal* video = FindField(root, "video");
  if (!video || video->type == JVal::Type::Null) {
    return true;
  }
  if (video->type != JVal::Type::Object) {
    return false;
  }
  bool adPlaying = false;
  if (const JVal* player = FindField(root, "player")) {
    if (player->type == JVal::Type::Object) {
      ReadBool(*player, "adPlaying", &adPlaying);
      double trackState = 0;
      if (ReadNumber(*player, "trackState", &trackState)) {
        out->playingKnown = true;
        out->playing = NearNumber(trackState, 1) || NearNumber(trackState, 2);
      }
    }
  }
  if (adPlaying) {
    return true;
  }
  const JVal* title = FindField(*video, "title");
  if (!title || title->type != JVal::Type::String || title->s.empty()) {
    return true;
  }
  out->title = ClipText(title->s);
  out->artist = ReadArtist(*video);
  if (const JVal* like = FindField(*video, "likeStatus")) {
    if (like->type == JVal::Type::Number) {
      out->like = ParseV1LikeNumber(like->n);
    } else {
      ApplyLikeField(*video, "likeStatus", &out->like, false);
    }
  }
  out->canLike = true;
  out->canDislike = true;
  out->ok = true;
  return true;
}

const char* YtmLegacyCommand(RateAction action) {
  switch (action) {
  case RateAction::Like:
    return "track-thumbs-up";
  case RateAction::Dislike:
    return "track-thumbs-down";
  }
  const RateAction unhandled = action;
  (void)unhandled;
  return "track-thumbs-up";
}

std::string YtmV1CommandBody(RateAction action) {
  switch (action) {
  case RateAction::Like:
    return "{\"command\":\"toggleLike\"}";
  case RateAction::Dislike:
    return "{\"command\":\"toggleDislike\"}";
  }
  const RateAction unhandled = action;
  (void)unhandled;
  return "{\"command\":\"toggleLike\"}";
}

DesktopWindowKind ClassifyDesktopWindow(const std::string& imageBase, const std::string& title) {
  const std::string base = BaseNameLower(imageBase);
  const std::string lowerTitle = LowerAscii(title);
  if (IsYtmImage(base)) {
    return DesktopWindowKind::YouTubeMusicDesktop;
  }
  if (base == "spotify.exe") {
    return DesktopWindowKind::Spotify;
  }
  if (base == "tidal.exe") {
    return DesktopWindowKind::Tidal;
  }
  if (base == "deezer.exe") {
    return DesktopWindowKind::Deezer;
  }
  if (base == "applemusic.exe" || base == "apple music.exe") {
    return DesktopWindowKind::AppleMusic;
  }
  if (BrowserFromImage(base) != BrowserProduct::None &&
      lowerTitle.find("youtube") != std::string::npos) {
    return DesktopWindowKind::BrowserYouTube;
  }
  return DesktopWindowKind::None;
}

bool PreferPlayerBar(DesktopWindowKind kind, const std::string& title) {
  switch (kind) {
  case DesktopWindowKind::YouTubeMusicDesktop:
  case DesktopWindowKind::Spotify:
  case DesktopWindowKind::Tidal:
  case DesktopWindowKind::Deezer:
  case DesktopWindowKind::AppleMusic:
    return true;
  case DesktopWindowKind::BrowserYouTube: {
    const std::string lower = LowerAscii(title);
    return lower.find("youtube music") != std::string::npos ||
           lower.find("music.youtube") != std::string::npos;
  }
  case DesktopWindowKind::None:
    return false;
  }
  const DesktopWindowKind unhandled = kind;
  (void)unhandled;
  return false;
}

int ScorePlayerWindow(const SourceRoute& route, const std::string& sessionSource,
                      const std::string& sessionAumid, const std::string& trackTitle,
                      const WindowIdentity& window) {
  const DesktopWindowKind kind = ClassifyDesktopWindow(window.imageBase, window.title);
  if (kind == DesktopWindowKind::None) {
    return 0;
  }
  bool allowed = false;
  switch (route.family) {
  case PlayerFamily::Spotify:
    allowed = kind == DesktopWindowKind::Spotify;
    break;
  case PlayerFamily::Tidal:
    allowed = kind == DesktopWindowKind::Tidal;
    break;
  case PlayerFamily::Deezer:
    allowed = kind == DesktopWindowKind::Deezer;
    break;
  case PlayerFamily::AppleMusic:
    allowed = kind == DesktopWindowKind::AppleMusic;
    break;
  case PlayerFamily::YouTubeMusicDesktop:
    allowed = kind == DesktopWindowKind::YouTubeMusicDesktop;
    break;
  case PlayerFamily::YouTubeAmbiguous:
    allowed = kind == DesktopWindowKind::YouTubeMusicDesktop ||
              kind == DesktopWindowKind::BrowserYouTube;
    break;
  case PlayerFamily::Browser:
    allowed = kind == DesktopWindowKind::BrowserYouTube;
    break;
  case PlayerFamily::None:
    allowed = false;
    break;
  }
  if (!allowed) {
    return 0;
  }
  const std::string titleNorm = NormalizeMatch(window.title);
  const std::string track = NormalizeMatch(trackTitle);
  if (kind == DesktopWindowKind::BrowserYouTube) {
    const BrowserProduct windowProduct = BrowserFromImage(BaseNameLower(window.imageBase));
    const BrowserProduct sessionProduct =
        BrowserFromText(LowerAscii(sessionSource + " " + sessionAumid));
    if (windowProduct != BrowserProduct::None && sessionProduct != BrowserProduct::None &&
        windowProduct != sessionProduct) {
      return 0;
    }
    const std::string lowerTitle = LowerAscii(window.title);
    const bool musicApp = lowerTitle.find("youtube music") != std::string::npos ||
                          lowerTitle.find("music.youtube") != std::string::npos;
    const bool trackHit = track.size() >= 4 && titleNorm.find(track) != std::string::npos;
    if (!musicApp && !trackHit) {
      return 0;
    }
  }
  int score = 20;
  if (route.preferDesktopApi && kind == DesktopWindowKind::YouTubeMusicDesktop) {
    score += 50;
  }
  if (window.visible && !window.iconic) {
    score += 8;
  } else if (window.iconic) {
    score += 4;
  }
  const std::string session = LowerAscii(sessionAumid);
  const std::string windowAumid = LowerAscii(window.aumid);
  const std::string image = LowerAscii(window.imageBase);
  if (!windowAumid.empty() && !session.empty() && windowAumid == session) {
    score += 80;
  } else if (!image.empty() && session.find(image) != std::string::npos) {
    score += 40;
  }
  if (track.size() >= 4 && titleNorm.find(track) != std::string::npos) {
    score += 35;
  }
  if (window.width > 0) {
    const int bonus = window.width / 200;
    score += bonus > 8 ? 8 : bonus;
  }
  return score;
}

UiaRateKind ClassifyUiaRateControl(const std::string& name, const std::string& automationId) {
  const std::string normalizedName = NormalizeLabel(name);
  const std::string normalizedId = NormalizeLabel(automationId);
  if (HasRejectMarker(normalizedName) || HasRejectMarker(normalizedId)) {
    return UiaRateKind::Rejected;
  }
  const UiaRateKind fromName = ClassifyNormalized(normalizedName);
  if (fromName == UiaRateKind::Rejected) {
    return UiaRateKind::Rejected;
  }
  if (fromName != UiaRateKind::None) {
    return fromName;
  }
  return ClassifyNormalized(normalizedId);
}

UiaRateSelection SelectUiaRateControls(const std::vector<UiaCandidate>& controls,
                                       const UiaWindowBox& window, bool preferPlayerBar,
                                       bool trustGeometry) {
  struct Item {
    int index = -1;
    UiaRateKind kind = UiaRateKind::None;
    double cx = 0;
    double cy = 0;
    bool bottom = false;
    bool likeSide = false;
  };
  std::vector<Item> items;
  const bool tall = window.height >= 40;
  for (int i = 0; i < static_cast<int>(controls.size()); ++i) {
    const UiaCandidate& control = controls[static_cast<size_t>(i)];
    const UiaRateKind kind = ClassifyUiaRateControl(control.name, control.automationId);
    const bool likeSide = kind == UiaRateKind::Like || kind == UiaRateKind::LibrarySave ||
                          kind == UiaRateKind::LibraryRemove;
    if (!likeSide && kind != UiaRateKind::Dislike) {
      continue;
    }
    if (control.offscreen || !control.enabled) {
      continue;
    }
    if (trustGeometry && (control.width < 8 || control.height < 8 || control.width > 240 ||
                          control.height > 96)) {
      continue;
    }
    Item item;
    item.index = i;
    item.kind = kind;
    item.cx = control.left + control.width * 0.5;
    item.cy = control.top + control.height * 0.5;
    item.bottom = tall && item.cy >= window.top + window.height * 0.68;
    item.likeSide = likeSide;
    items.push_back(item);
  }
  auto finish = [&](int likeIndex, int dislikeIndex) {
    UiaRateSelection pick;
    pick.likeIndex = likeIndex;
    pick.dislikeIndex = dislikeIndex;
    if (likeIndex >= 0 && likeIndex < static_cast<int>(controls.size())) {
      const UiaCandidate& control = controls[static_cast<size_t>(likeIndex)];
      pick.likeActive =
          IsUiaControlActive(control, ClassifyUiaRateControl(control.name, control.automationId));
    }
    if (dislikeIndex >= 0 && dislikeIndex < static_cast<int>(controls.size())) {
      const UiaCandidate& control = controls[static_cast<size_t>(dislikeIndex)];
      pick.dislikeActive = IsUiaControlActive(control, UiaRateKind::Dislike);
    }
    return pick;
  };
  if (!trustGeometry) {
    int likes = 0;
    int dislikes = 0;
    int libraries = 0;
    int likeIndex = -1;
    int dislikeIndex = -1;
    int libraryIndex = -1;
    for (const Item& item : items) {
      if (item.likeSide) {
        ++likes;
        likeIndex = item.index;
        if (item.kind == UiaRateKind::LibrarySave || item.kind == UiaRateKind::LibraryRemove) {
          ++libraries;
          libraryIndex = item.index;
        }
      } else {
        ++dislikes;
        dislikeIndex = item.index;
      }
    }
    if (libraries == 1 && dislikes == 0) {
      return finish(libraryIndex, -1);
    }
    if (likes == 1 && dislikes <= 1) {
      return finish(likeIndex, dislikes == 1 ? dislikeIndex : -1);
    }
    return {};
  }
  int bestScore = -100000;
  int bestLike = -1;
  int bestDislike = -1;
  for (const Item& like : items) {
    if (!like.likeSide) {
      continue;
    }
    const Item* partner = nullptr;
    double bestDx = 1.0e9;
    for (const Item& dislike : items) {
      if (dislike.likeSide) {
        continue;
      }
      const double dx = dislike.cx - like.cx;
      const double dy = dislike.cy > like.cy ? dislike.cy - like.cy : like.cy - dislike.cy;
      if (dx < 8 || dx > 180 || dy > 36) {
        continue;
      }
      if (dx < bestDx) {
        bestDx = dx;
        partner = &dislike;
      }
    }
    int score = partner ? 120 : 0;
    if (preferPlayerBar && like.bottom) {
      score += 80;
    }
    if (preferPlayerBar && partner && partner->bottom) {
      score += 30;
    }
    if (!preferPlayerBar && like.bottom && !partner) {
      score -= 40;
    }
    if (!preferPlayerBar && partner && like.bottom) {
      score -= 80;
    }
    if (like.kind == UiaRateKind::LibrarySave || like.kind == UiaRateKind::LibraryRemove) {
      score += 30;
    }
    score -= static_cast<int>(like.cy / 80.0);
    if (score > bestScore) {
      bestScore = score;
      bestLike = like.index;
      bestDislike = partner ? partner->index : -1;
    }
  }
  if (bestScore >= 50) {
    return finish(bestLike, bestDislike);
  }
  int likes = 0;
  int dislikes = 0;
  int libraries = 0;
  int likeIndex = -1;
  int dislikeIndex = -1;
  int libraryIndex = -1;
  bool likeBottom = false;
  for (const Item& item : items) {
    if (item.likeSide) {
      ++likes;
      likeIndex = item.index;
      likeBottom = item.bottom;
      if (item.kind == UiaRateKind::LibrarySave || item.kind == UiaRateKind::LibraryRemove) {
        ++libraries;
        libraryIndex = item.index;
      }
    } else {
      ++dislikes;
      dislikeIndex = item.index;
    }
  }
  if (libraries == 1 && dislikes == 0) {
    return finish(libraryIndex, -1);
  }
  if (likes == 1 && dislikes == 1 && (preferPlayerBar || !likeBottom)) {
    return finish(likeIndex, dislikeIndex);
  }
  if (likes == 1 && dislikes == 0 && preferPlayerBar) {
    return finish(likeIndex, -1);
  }
  return {};
}

BridgeLikeState ParsedLikeToBridge(ParsedLike like) {
  switch (like) {
  case ParsedLike::Liked:
    return BridgeLikeState::Liked;
  case ParsedLike::Disliked:
    return BridgeLikeState::Disliked;
  case ParsedLike::None:
  case ParsedLike::Unknown:
    return BridgeLikeState::None;
  }
  const ParsedLike unhandled = like;
  (void)unhandled;
  return BridgeLikeState::None;
}

void DesktopRateStart() {
  std::lock_guard<std::mutex> lock(g_life);
  if (g_thread) {
    return;
  }
  g_inflight.Reset();
  g_stop.store(false, std::memory_order_release);
  g_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (!g_event) {
    LUX_LOGE(L"media-rate: CreateEvent failed (%lu)", GetLastError());
    return;
  }
  g_running.store(true, std::memory_order_release);
  g_thread = CreateThread(nullptr, 0, DesktopRateThread, nullptr, 0, nullptr);
  if (!g_thread) {
    g_running.store(false, std::memory_order_release);
    CloseHandle(g_event);
    g_event = nullptr;
    LUX_LOGE(L"media-rate: CreateThread failed (%lu)", GetLastError());
  }
}

void DesktopRateStop() {
  std::lock_guard<std::mutex> lock(g_life);
  if (!g_thread && !g_event) {
    return;
  }
  g_stop.store(true, std::memory_order_release);
  g_running.store(false, std::memory_order_release);
  g_inflight.Cancel();
  if (g_event) {
    SetEvent(g_event);
  }
  if (g_thread) {
    if (WaitForSingleObject(g_thread, 3000) == WAIT_TIMEOUT) {
      LUX_LOGE(L"media-rate: worker did not stop within 3000 ms");
      return;
    }
    CloseHandle(g_thread);
    g_thread = nullptr;
  }
  if (g_event) {
    CloseHandle(g_event);
    g_event = nullptr;
  }
  {
    std::lock_guard<std::mutex> stateLock(g_mu);
    g_snapshot.fresh = false;
    g_command.has = false;
  }
  g_stop.store(false, std::memory_order_release);
}

void DesktopRateNoteSession(const SmtcHint& hint, const std::string& appUserModelId,
                            bool browserCovers) {
  bool wake = false;
  {
    std::lock_guard<std::mutex> lock(g_mu);
    const bool identityChanged = !g_session.valid || g_session.title != hint.title ||
                                 g_session.artist != hint.artist || g_session.source != hint.source ||
                                 g_session.aumid != appUserModelId;
    g_session.title = hint.title;
    g_session.artist = hint.artist;
    g_session.source = hint.source;
    g_session.aumid = appUserModelId;
    g_session.playing = hint.playing;
    g_session.valid = true;
    if (identityChanged) {
      ++g_session.serial;
      if (g_session.serial == 0) {
        g_session.serial = 1;
      }
      wake = true;
    }
    if (g_session.browserCovers != browserCovers) {
      g_session.browserCovers = browserCovers;
      wake = true;
    }
  }
  if (wake && g_event) {
    SetEvent(g_event);
  }
}

bool DesktopRateReadOffer(BackendOffer* offer) {
  if (!offer) {
    return false;
  }
  std::lock_guard<std::mutex> lock(g_mu);
  if (!g_snapshot.fresh || g_snapshot.serial != g_session.serial) {
    return false;
  }
  const uint64_t now = GetTickCount64();
  if (now < g_snapshot.tick || now - g_snapshot.tick > 5000) {
    return false;
  }
  *offer = g_snapshot.offer;
  return true;
}

bool DesktopRateEnqueue(const SmtcHint& hint, const std::string& appUserModelId, RateAction action,
                        DesktopRateBackend backend, uint64_t feedbackGeneration) {
  if (!g_running.load(std::memory_order_acquire) || backend == DesktopRateBackend::None ||
      backend == DesktopRateBackend::BrowserBridge) {
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(g_mu);
    g_command.has = true;
    g_command.action = action;
    g_command.backend = backend;
    g_command.hint = hint;
    g_command.aumid = appUserModelId;
    g_command.generation = feedbackGeneration;
    g_command.serial = g_session.serial;
  }
  if (g_event) {
    SetEvent(g_event);
  }
  return true;
}

bool DesktopRateSelfTest() {
#define RATE_CHECK(cond)                                                                 \
  do {                                                                                   \
    if (!(cond)) {                                                                       \
      std::fprintf(stderr, "desktop-rate self-test line %d: %s\n", __LINE__, #cond);     \
      LUX_LOGE(L"desktop-rate self-test line %d failed", __LINE__);                      \
      return false;                                                                      \
    }                                                                                    \
  } while (0)

  const SourceRoute spotify = ClassifyPlaybackSource("Spotify", "Spotify.exe");
  RATE_CHECK(spotify.family == PlayerFamily::Spotify);
  RATE_CHECK(!spotify.allowBrowserBridge);
  RATE_CHECK(!spotify.allowPear);
  RATE_CHECK(!spotify.allowYtmDesktop);
  RATE_CHECK(spotify.allowUiAutomation);

  const SourceRoute tidal = ClassifyPlaybackSource("TIDAL", "C:\\Program Files\\TIDAL\\TIDAL.exe");
  RATE_CHECK(tidal.family == PlayerFamily::Tidal);
  const SourceRoute deezer = ClassifyPlaybackSource("Deezer", "Deezer.exe");
  RATE_CHECK(deezer.family == PlayerFamily::Deezer);
  const SourceRoute apple = ClassifyPlaybackSource(
      "Apple Music", "AppleInc.AppleMusicWin_nzyj5cx40ttqa!AppleMusic");
  RATE_CHECK(apple.family == PlayerFamily::AppleMusic);
  RATE_CHECK(apple.allowUiAutomation);
  RATE_CHECK(!apple.allowBrowserBridge);

  const SourceRoute chrome =
      ClassifyPlaybackSource("Chrome", "C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe");
  RATE_CHECK(chrome.family == PlayerFamily::Browser);
  RATE_CHECK(chrome.allowBrowserBridge);
  RATE_CHECK(!chrome.allowPear);
  RATE_CHECK(chrome.allowUiAutomation);
  RATE_CHECK(!chrome.preferDesktopApi);
  const SourceRoute edge = ClassifyPlaybackSource("Microsoft Edge", "MSEdge.exe");
  RATE_CHECK(edge.family == PlayerFamily::Browser);

  const SourceRoute pear = ClassifyPlaybackSource("YouTube", "com.github.th-ch.youtube-music");
  RATE_CHECK(pear.family == PlayerFamily::YouTubeMusicDesktop);
  RATE_CHECK(pear.preferDesktopApi);
  RATE_CHECK(!pear.allowBrowserBridge);
  RATE_CHECK(pear.allowPear && pear.allowYtmDesktop && pear.allowUiAutomation);
  const SourceRoute pearExe = ClassifyPlaybackSource(
      "YouTube", "C:\\Users\\Ada\\AppData\\Local\\Programs\\YouTube Music\\YouTube Music.exe");
  RATE_CHECK(pearExe.family == PlayerFamily::YouTubeMusicDesktop);
  const SourceRoute ytmExe = ClassifyPlaybackSource(
      "YouTube",
      "C:\\Program Files\\YouTube Music Desktop App\\YouTube Music Desktop App.exe");
  RATE_CHECK(ytmExe.family == PlayerFamily::YouTubeMusicDesktop);
  const SourceRoute ytmV2Exe = ClassifyPlaybackSource(
      "YouTube",
      "C:\\Users\\w\\AppData\\Local\\youtube_music_desktop_app\\app-2.1.0\\"
      "youtube-music-desktop-app.exe");
  RATE_CHECK(ytmV2Exe.family == PlayerFamily::YouTubeMusicDesktop);
  RATE_CHECK(ClassifyDesktopWindow("youtube-music-desktop-app.exe",
                                   "Night Drive - Ada Lore | YTMDesktop") ==
             DesktopWindowKind::YouTubeMusicDesktop);
  RATE_CHECK(ClassifyDesktopWindow("ytmdesktop.exe", "YTMDesktop") ==
             DesktopWindowKind::YouTubeMusicDesktop);

  const SourceRoute ambiguous = ClassifyPlaybackSource("YouTube", "");
  RATE_CHECK(ambiguous.family == PlayerFamily::YouTubeAmbiguous);
  RATE_CHECK(ambiguous.allowBrowserBridge && ambiguous.allowPear && ambiguous.preferDesktopApi);
  const SourceRoute vlc =
      ClassifyPlaybackSource("VLC", "C:\\Program Files\\VideoLAN\\VLC\\vlc.exe");
  RATE_CHECK(vlc.family == PlayerFamily::None);
  RATE_CHECK(!vlc.allowBrowserBridge && !vlc.allowUiAutomation && !vlc.allowPear);

  BackendOffer offer;
  offer.browserMatch = true;
  offer.browserCanLike = true;
  offer.browserCanDislike = true;
  offer.browserLike = ParsedLike::Liked;
  offer.pearReachable = true;
  offer.pearTrackMatch = true;
  offer.pearCanLike = true;
  offer.pearCanDislike = true;
  offer.pearLike = ParsedLike::Disliked;
  offer.uiaReady = true;
  offer.uiaCanLike = true;
  offer.uiaLike = ParsedLike::Liked;
  RATE_CHECK(ChooseRateBackend(spotify, offer).backend == DesktopRateBackend::UiAutomation);
  RATE_CHECK(!ChooseRateBackend(spotify, offer).canDislike);
  RATE_CHECK(ChooseRateBackend(chrome, offer).backend == DesktopRateBackend::BrowserBridge);
  RATE_CHECK(ChooseRateBackend(pear, offer).backend == DesktopRateBackend::PearDesktop);
  RATE_CHECK(ChooseRateBackend(ambiguous, offer).backend == DesktopRateBackend::PearDesktop);

  offer.pearTrackMatch = false;
  RATE_CHECK(ChooseRateBackend(ambiguous, offer).backend == DesktopRateBackend::BrowserBridge);
  offer.browserMatch = false;
  offer.ytmReachable = true;
  offer.ytmTrackMatch = true;
  offer.ytmCanLike = true;
  offer.ytmCanDislike = true;
  offer.ytmLike = ParsedLike::None;
  RATE_CHECK(ChooseRateBackend(pear, offer).backend == DesktopRateBackend::YtmDesktop);
  offer.ytmTrackMatch = false;
  RATE_CHECK(ChooseRateBackend(pear, offer).backend == DesktopRateBackend::UiAutomation);
  offer.uiaCanLike = true;
  offer.uiaCanDislike = false;
  const BackendChoice likeOnly = ChooseRateBackend(spotify, offer);
  const RateCapability capability = DescribeRateCapability(likeOnly);
  RATE_CHECK(capability.likeOnly && capability.show && capability.canLike && !capability.canDislike);
  offer.uiaReady = false;
  RATE_CHECK(ChooseRateBackend(spotify, offer).backend == DesktopRateBackend::None);
  RATE_CHECK(!DescribeRateCapability(ChooseRateBackend(spotify, offer)).show);

  RATE_CHECK(TracksMatch("Night Drive", "Ada Lore", "Night Drive (Official Video)", "Ada Lore"));
  RATE_CHECK(!TracksMatch("Night Drive", "Ada Lore", "Morning Light", "Ada Lore"));
  RATE_CHECK(!TracksMatch("Night Drive", "Ada Lore", "Night Drive", "Someone Else"));
  RATE_CHECK(!TracksMatch("", "Ada", "Night Drive", "Ada"));

  DesktopApiSong song;
  const char* pearSong =
      "{\"title\":\"Night Drive\",\"artist\":\"Ada Lore\",\"views\":10,\"isPaused\":false,"
      "\"songDuration\":200,\"videoId\":\"abc\",\"album\":null,\"mediaType\":\"AUDIO\"}";
  RATE_CHECK(ParsePearSongJson(pearSong, &song));
  RATE_CHECK(song.ok && song.title == "Night Drive" && song.artist == "Ada Lore" && song.playing);
  RATE_CHECK(song.canLike && song.canDislike);
  const char* pearArtists =
      "{\"title\":\"Night \\\"Drive\\\"\",\"artist\":[{\"name\":\"Ada Lore\"},{\"name\":\"Bea\"}],"
      "\"isPaused\":true,\"videoId\":\"abc\",\"songDuration\":10,\"mediaType\":\"AUDIO\"}";
  RATE_CHECK(ParsePearSongJson(pearArtists, &song));
  RATE_CHECK(song.title == "Night \"Drive\"" && song.artist == "Ada Lore, Bea" && !song.playing);
  RATE_CHECK(!ParsePearSongJson("{\"title\":", &song));
  RATE_CHECK(!ParsePearSongJson("{\"artist\":\"Ada\"}", &song));

  ParsedLike like = ParsedLike::Unknown;
  RATE_CHECK(ParsePearLikeStateJson("{\"state\":\"LIKE\"}", &like) && like == ParsedLike::Liked);
  RATE_CHECK(ParsePearLikeStateJson("{\"state\":\"DISLIKE\"}", &like) && like == ParsedLike::Disliked);
  RATE_CHECK(ParsePearLikeStateJson("{\"state\":\"INDIFFERENT\"}", &like) && like == ParsedLike::None);
  RATE_CHECK(ParsePearLikeStateJson("{\"state\":null}", &like) && like == ParsedLike::Unknown);
  RATE_CHECK(!ParsePearLikeStateJson("{\"state\":\"love\"}", &like));
  std::string token;
  RATE_CHECK(ParsePearAuthJson("{\"accessToken\":\"eyJhbGciOi.abc\"}", &token) &&
             token == "eyJhbGciOi.abc");
  RATE_CHECK(!ParsePearAuthJson("{\"accessToken\":\"short\"}", &token));

  const char* legacy =
      "{\"player\":{\"hasSong\":true,\"isPaused\":false,\"likeStatus\":\"DISLIKE\"},"
      "\"track\":{\"author\":\"Ada Lore\",\"title\":\"Night Drive\",\"id\":\"abc\","
      "\"isAdvertisement\":false}}";
  RATE_CHECK(ParseYtmLegacyQueryJson(legacy, &song));
  RATE_CHECK(song.ok && song.playing && song.like == ParsedLike::Disliked &&
             song.artist == "Ada Lore");
  RATE_CHECK(ParseYtmLegacyQueryJson(
      "{\"player\":{\"hasSong\":false,\"likeStatus\":\"LIKE\"},\"track\":{\"title\":\"\"}}", &song));
  RATE_CHECK(!song.ok);
  const char* v1 =
      "{\"player\":{\"trackState\":1,\"adPlaying\":false,\"queue\":{\"items\":[1,2,3]}},"
      "\"video\":{\"author\":\"Ada Lore\",\"title\":\"Night Drive\",\"likeStatus\":2,\"id\":\"abc\"}}";
  RATE_CHECK(ParseYtmV1StateJson(v1, &song));
  RATE_CHECK(song.ok && song.playing && song.like == ParsedLike::Liked && song.title == "Night Drive");
  RATE_CHECK(ParseYtmV1StateJson(
                 "{\"player\":{\"trackState\":0},\"video\":{\"title\":\"Night Drive\",\"author\":\"Ada\","
                 "\"likeStatus\":0}}",
                 &song) &&
             song.ok && !song.playing && song.like == ParsedLike::Disliked);
  RATE_CHECK(ParseYtmV1StateJson(
                 "{\"player\":{\"trackState\":2},\"video\":{\"title\":\"Night Drive\",\"likeStatus\":1}}",
                 &song) &&
             song.playing && song.like == ParsedLike::None);
  RATE_CHECK(ParseYtmV1StateJson("{\"player\":{\"trackState\":1},\"video\":null}", &song) && !song.ok);
  RATE_CHECK(ParseYtmV1StateJson(
                 "{\"player\":{\"adPlaying\":true},\"video\":{\"title\":\"Ad\",\"likeStatus\":2}}", &song) &&
             !song.ok);
  RATE_CHECK(!ParseYtmV1StateJson("{", &song));

  RATE_CHECK(std::string(YtmLegacyCommand(RateAction::Like)) == "track-thumbs-up");
  RATE_CHECK(std::string(YtmLegacyCommand(RateAction::Dislike)) == "track-thumbs-down");
  RATE_CHECK(YtmV1CommandBody(RateAction::Like) == "{\"command\":\"toggleLike\"}");
  RATE_CHECK(YtmV1CommandBody(RateAction::Dislike) == "{\"command\":\"toggleDislike\"}");
  RATE_CHECK(std::string(YtmCompanionAppId()) == "dynamicislandlux");
  RATE_CHECK(std::string(YtmCompanionAppName()) == "Dynamic Island Lux");
  {
    const std::string expectedAuth =
        std::string("{\"appId\":\"dynamicislandlux\",\"appName\":\"Dynamic Island Lux\","
                    "\"appVersion\":\"") +
        LUX_APP_VERSION_STR + "\"}";
    RATE_CHECK(YtmAuthRequestCodeBody("dynamicislandlux", "Dynamic Island Lux",
                                      LUX_APP_VERSION_STR) == expectedAuth);
  }
  RATE_CHECK(YtmAuthRequestTokenBody("dynamicislandlux", "1337") ==
             "{\"appId\":\"dynamicislandlux\",\"code\":\"1337\"}");
  std::string ytmCode;
  RATE_CHECK(ParseYtmAuthCodeJson("{\"code\":\"1337\"}", &ytmCode) && ytmCode == "1337");
  RATE_CHECK(!ParseYtmAuthCodeJson("{\"code\":\"ab\"}", &ytmCode));
  RATE_CHECK(!ParseYtmAuthCodeJson("{\"code\":\"\"}", &ytmCode));
  std::string ytmTok;
  RATE_CHECK(ParseYtmAuthTokenJson(
                 "{\"token\":\"ea845423977fcbe9535547db2c09134b5087c352\"}", &ytmTok) &&
             ytmTok == "ea845423977fcbe9535547db2c09134b5087c352");
  RATE_CHECK(!ParseYtmAuthTokenJson("{\"token\":\"short\"}", &ytmTok));
  RATE_CHECK(ParseYtmTokenStore("  abcdefghijklmnop\r\n", &ytmTok) && ytmTok == "abcdefghijklmnop");
  RATE_CHECK(FormatYtmTokenStore("abcdefghijklmnop") == "abcdefghijklmnop\n");
  RATE_CHECK(DecideYtmAuth(true, true, 200, false, 1000, 0) == YtmAuthAction::ProbeWithToken);
  RATE_CHECK(DecideYtmAuth(false, false, 0, false, 1000, 0) == YtmAuthAction::Unavailable);
  RATE_CHECK(DecideYtmAuth(false, true, 401, false, 1000, 0) ==
             YtmAuthAction::BeginInteractiveAuth);
  RATE_CHECK(DecideYtmAuth(false, true, 401, true, 1000, 0) == YtmAuthAction::Backoff);
  RATE_CHECK(DecideYtmAuth(false, true, 401, false, 1000, 5000) == YtmAuthAction::Backoff);
  RATE_CHECK(OptimisticRate(BridgeLikeState::None, RateAction::Like) == BridgeLikeState::Liked);
  RATE_CHECK(OptimisticRate(BridgeLikeState::Liked, RateAction::Like) == BridgeLikeState::None);
  RATE_CHECK(OptimisticRate(BridgeLikeState::Liked, RateAction::Dislike) == BridgeLikeState::Disliked);
  RATE_CHECK(OptimisticRate(BridgeLikeState::Disliked, RateAction::Dislike) == BridgeLikeState::None);

  RATE_CHECK(ClassifyUiaRateControl("Like", "") == UiaRateKind::Like);
  RATE_CHECK(ClassifyUiaRateControl("Dislike", "") == UiaRateKind::Dislike);
  RATE_CHECK(ClassifyUiaRateControl("Thumbs up", "") == UiaRateKind::Like);
  RATE_CHECK(ClassifyUiaRateControl("Thumbs-down", "") == UiaRateKind::Dislike);
  RATE_CHECK(ClassifyUiaRateControl("Save to Your Library", "") == UiaRateKind::LibrarySave);
  RATE_CHECK(ClassifyUiaRateControl("Remove from Your Library", "") == UiaRateKind::LibraryRemove);
  RATE_CHECK(ClassifyUiaRateControl("Add to Liked Songs", "") == UiaRateKind::LibrarySave);
  RATE_CHECK(ClassifyUiaRateControl("Liked Songs", "") == UiaRateKind::Rejected);
  RATE_CHECK(ClassifyUiaRateControl("Подобається", "") == UiaRateKind::Like);
  RATE_CHECK(ClassifyUiaRateControl("Не подобається", "") == UiaRateKind::Dislike);
  RATE_CHECK(ClassifyUiaRateControl("Нравится", "") == UiaRateKind::Like);
  RATE_CHECK(ClassifyUiaRateControl("Не нравится", "") == UiaRateKind::Dislike);
  RATE_CHECK(ClassifyUiaRateControl("Gefällt mir nicht", "") == UiaRateKind::Dislike);
  RATE_CHECK(ClassifyUiaRateControl("Gefällt mir", "") == UiaRateKind::Like);
  RATE_CHECK(ClassifyUiaRateControl("高評価", "") == UiaRateKind::Like);
  RATE_CHECK(ClassifyUiaRateControl("低評価", "") == UiaRateKind::Dislike);
  RATE_CHECK(ClassifyUiaRateControl("Like this comment", "") == UiaRateKind::Rejected);
  RATE_CHECK(ClassifyUiaRateControl("likelihood", "") == UiaRateKind::None);
  RATE_CHECK(ClassifyUiaRateControl("", "segmented-dislike-button") == UiaRateKind::Dislike);
  RATE_CHECK(ClassifyUiaRateControl("", "like-button-renderer") == UiaRateKind::Like);
  RATE_CHECK(ClassifyUiaRateControl("Like", "comment-like-button") == UiaRateKind::Rejected);
  RATE_CHECK(ClassifyUiaRateControl("Зберегти в бібліотеці", "") == UiaRateKind::LibrarySave);
  RATE_CHECK(ClassifyUiaRateControl("Сохранить в медиатеке", "") == UiaRateKind::LibrarySave);

  auto candidate = [](const char* name, const char* automationId, double x, double y, double w,
                      double h, int toggle) {
    UiaCandidate control;
    control.name = name;
    control.automationId = automationId ? automationId : "";
    control.left = x;
    control.top = y;
    control.width = w;
    control.height = h;
    control.toggleState = toggle;
    control.enabled = true;
    return control;
  };
  UiaWindowBox page;
  page.width = 1280;
  page.height = 1000;
  std::vector<UiaCandidate> watch;
  watch.push_back(candidate("Like", "like-button", 100, 400, 36, 36, 1));
  watch.push_back(candidate("Dislike", "dislike-button", 140, 400, 36, 36, 0));
  watch.push_back(candidate("Like", "comment-like", 100, 860, 28, 28, 0));
  watch.push_back(candidate("Like this comment", "", 100, 920, 28, 28, 0));
  UiaRateSelection selected = SelectUiaRateControls(watch, page, false, true);
  RATE_CHECK(selected.likeIndex == 0 && selected.dislikeIndex == 1 && selected.likeActive &&
             !selected.dislikeActive);

  UiaWindowBox player;
  player.width = 1280;
  player.height = 760;
  std::vector<UiaCandidate> ytm;
  ytm.push_back(candidate("Like", "", 100, 180, 32, 32, 0));
  ytm.push_back(candidate("Подобається", "", 400, 700, 36, 36, 0));
  ytm.push_back(candidate("Не подобається", "", 444, 700, 36, 36, 1));
  selected = SelectUiaRateControls(ytm, player, true, true);
  RATE_CHECK(selected.likeIndex == 1 && selected.dislikeIndex == 2 && !selected.likeActive &&
             selected.dislikeActive);

  std::vector<UiaCandidate> save;
  save.push_back(candidate("Save to Your Library", "save-button", 500, 700, 40, 40, -1));
  selected = SelectUiaRateControls(save, player, true, true);
  RATE_CHECK(selected.likeIndex == 0 && selected.dislikeIndex < 0 && !selected.likeActive);
  save[0] = candidate("Remove from Your Library", "", 500, 700, 40, 40, -1);
  selected = SelectUiaRateControls(save, player, true, true);
  RATE_CHECK(selected.likeIndex == 0 && selected.likeActive && selected.dislikeIndex < 0);

  std::vector<UiaCandidate> comments;
  comments.push_back(candidate("Like", "", 80, 800, 24, 24, 0));
  comments.push_back(candidate("Like", "", 80, 860, 24, 24, 0));
  comments.push_back(candidate("Like", "", 80, 920, 24, 24, 0));
  selected = SelectUiaRateControls(comments, page, false, true);
  RATE_CHECK(selected.likeIndex < 0 && selected.dislikeIndex < 0);
  selected = SelectUiaRateControls(comments, page, false, false);
  RATE_CHECK(selected.likeIndex < 0);

  std::vector<UiaCandidate> minimized;
  minimized.push_back(candidate("Like", "", 0, 0, 0, 0, 0));
  minimized.push_back(candidate("Dislike", "", 0, 0, 0, 0, 1));
  selected = SelectUiaRateControls(minimized, page, false, false);
  RATE_CHECK(selected.likeIndex == 0 && selected.dislikeIndex == 1 && selected.dislikeActive);

  WindowIdentity chromeWindow;
  chromeWindow.imageBase = "chrome.exe";
  chromeWindow.title = "Night Drive - YouTube";
  chromeWindow.visible = true;
  chromeWindow.width = 1200;
  chromeWindow.height = 800;
  WindowIdentity musicWindow = chromeWindow;
  musicWindow.title = "YouTube Music";
  WindowIdentity gmail = chromeWindow;
  gmail.title = "Inbox - Gmail";
  WindowIdentity edgeWindow = chromeWindow;
  edgeWindow.imageBase = "msedge.exe";
  edgeWindow.title = "Night Drive - YouTube Music";
  WindowIdentity desktopWindow;
  desktopWindow.imageBase = "YouTube Music.exe";
  desktopWindow.title = "YouTube Music";
  desktopWindow.visible = true;
  desktopWindow.width = 1100;
  desktopWindow.height = 700;
  WindowIdentity spotifyWindow;
  spotifyWindow.imageBase = "Spotify.exe";
  spotifyWindow.title = "Spotify";
  spotifyWindow.visible = true;
  spotifyWindow.width = 1000;
  spotifyWindow.height = 640;
  RATE_CHECK(ScorePlayerWindow(chrome, "Chrome", "chrome.exe", "Night Drive", chromeWindow) >
             ScorePlayerWindow(chrome, "Chrome", "chrome.exe", "Night Drive", musicWindow));
  RATE_CHECK(ScorePlayerWindow(chrome, "Chrome", "chrome.exe", "Night Drive", musicWindow) > 0);
  RATE_CHECK(ScorePlayerWindow(chrome, "Chrome", "chrome.exe", "Night Drive", gmail) == 0);
  RATE_CHECK(ScorePlayerWindow(chrome, "Chrome", "C:\\chrome.exe", "Night Drive", edgeWindow) == 0);
  RATE_CHECK(ScorePlayerWindow(chrome, "Chrome", "chrome.exe", "Night Drive", desktopWindow) == 0);
  RATE_CHECK(ScorePlayerWindow(pear, "YouTube", "YouTube Music.exe", "Night Drive", desktopWindow) >
             0);
  RATE_CHECK(ScorePlayerWindow(pear, "YouTube", "YouTube Music.exe", "Night Drive", chromeWindow) ==
             0);
  RATE_CHECK(ScorePlayerWindow(spotify, "Spotify", "Spotify.exe", "Night Drive", spotifyWindow) > 0);
  RATE_CHECK(ScorePlayerWindow(spotify, "Spotify", "Spotify.exe", "Night Drive", chromeWindow) == 0);
  RATE_CHECK(ClassifyDesktopWindow("TIDAL.exe", "TIDAL") == DesktopWindowKind::Tidal);
  RATE_CHECK(PreferPlayerBar(DesktopWindowKind::Spotify, "Spotify"));
  RATE_CHECK(!PreferPlayerBar(DesktopWindowKind::BrowserYouTube, "Night Drive - YouTube"));
  RATE_CHECK(PreferPlayerBar(DesktopWindowKind::BrowserYouTube, "YouTube Music"));

#undef RATE_CHECK
  return true;
}
