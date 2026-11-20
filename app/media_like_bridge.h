#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

// Local Like / Dislike for the expanded player.
// The browser extension bridge binds 127.0.0.1 only. Desktop players are
// handled beside it (Pear Desktop, ytmdesktop, and UI Automation) and never
// on the UI thread. These types are the testable core: origin/Host checks,
// JSON state, tab matching, and the command queue.

enum class BridgeSite { YouTube, YouTubeMusic };

enum class BridgeLikeState { None, Liked, Disliked };

enum class RateAction { Like, Dislike };

enum class BridgeAccess {
  AllowExtension,
  AllowLoopbackHeader,
  Reject,
};

constexpr uint64_t kMediaLikeBridgeStaleMs = 8000;
constexpr int kMediaLikeBridgeDefaultPort = 18473;
constexpr int kMediaLikeBridgeMinPort = 1024;
constexpr int kMediaLikeBridgeMaxPort = 65535;

struct TabState {
  std::string tabId;
  BridgeSite site = BridgeSite::YouTube;
  std::string videoId;
  std::string title;
  std::string artist;
  bool playing = false;
  BridgeLikeState like = BridgeLikeState::None;
  bool canRate = false;
  int64_t clientTimestamp = 0;
  uint64_t updatedTick = 0;
};

struct ParsedTabUpdate {
  TabState state;
  std::vector<uint64_t> ack;
};

struct SmtcHint {
  std::string title;
  std::string artist;
  std::string source;
  bool playing = false;
};

struct QueuedCommand {
  uint64_t id = 0;
  RateAction action = RateAction::Like;
  std::string videoId;
};

struct HttpHeaderSet {
  bool ok = false;
  std::string method;
  std::string path;
  std::string host;
  std::string origin;
  std::string bridgeHeader;
  bool contentLengthPresent = false;
  int contentLength = -1;
  bool chunked = false;
};

enum class RateFeedback { None, Pending, Error };

struct MediaLikeBridgeView {
  bool connected = false;
  bool canRate = false;
  bool canLike = false;
  bool canDislike = false;
  // Dim placeholder for a browser/YouTube session whose extension is not up yet.
  bool reserveButtons = false;
  BridgeLikeState like = BridgeLikeState::None;
  RateFeedback feedback = RateFeedback::None;
  RateAction feedbackAction = RateAction::Like;
};

inline BridgeLikeState OptimisticRate(BridgeLikeState current, RateAction action) {
  switch (action) {
  case RateAction::Like:
    return current == BridgeLikeState::Liked ? BridgeLikeState::None : BridgeLikeState::Liked;
  case RateAction::Dislike:
    return current == BridgeLikeState::Disliked ? BridgeLikeState::None : BridgeLikeState::Disliked;
  }
  const RateAction unhandled = action;
  (void)unhandled;
  return current;
}

// Host must be 127.0.0.1:<port> or localhost:<port>.
// Extension origins (chrome-extension://, moz-extension://, extension://) are
// allowed. A missing Origin is allowed only with X-Dynamic-Island-Lux: 1.
// Any other Origin is rejected, even when that header is present.
BridgeAccess EvaluateBridgeAccess(const std::string& host, const std::string& origin,
                                  const std::string& bridgeHeader, int listenPort);

bool ParseHttpHeaderBlock(const std::string& block, HttpHeaderSet* out);

bool ParseTabStateJson(const std::string& json, ParsedTabUpdate* out);

const char* RateActionJson(RateAction action);

std::string FormatCommandsJson(const std::vector<QueuedCommand>& commands);

class TabRegistry {
 public:
  bool Upsert(TabState state, uint64_t nowTick);
  int ExpireStale(uint64_t nowTick, uint64_t staleMs);
  int size() const;
  bool BestMatch(const SmtcHint& hint, uint64_t nowTick, uint64_t staleMs, TabState* out) const;
  bool EnqueueBest(const SmtcHint& hint, RateAction action, uint64_t nowTick, uint64_t staleMs,
                   std::string* tabIdOut, QueuedCommand* commandOut);
  std::vector<QueuedCommand> Peek(const std::string& tabId) const;
  void Ack(const std::string& tabId, const std::vector<uint64_t>& ids);

 private:
  struct Slot {
    TabState state;
    std::vector<QueuedCommand> commands;
  };
  bool Stale(const Slot& slot, uint64_t nowTick, uint64_t staleMs) const;
  int BestIndexLocked(const SmtcHint& hint, uint64_t nowTick, uint64_t staleMs) const;
  mutable std::mutex mutex_;
  std::vector<Slot> tabs_;
  uint64_t nextCommandId_ = 1;
};

// Process-wide server + registry used by the island UI.
// appUserModelId is the SMTC SourceAppUserModelId. Pass it so a desktop player
// is not confused with a browser tab that happens to show the same title.
void MediaLikeBridgeSetWake(void (*wake)());
void MediaLikeBridgeApply(bool enabled, int port);
void MediaLikeBridgeStop();
MediaLikeBridgeView MediaLikeBridgeQuery(const std::wstring& title, const std::wstring& artist,
                                         const std::wstring& source, bool playing,
                                         const std::wstring& appUserModelId = std::wstring());
bool MediaLikeBridgeEnqueue(const std::wstring& title, const std::wstring& artist,
                            const std::wstring& source, bool playing, RateAction action,
                            const std::wstring& appUserModelId = std::wstring());

uint64_t MediaRateNotePending(RateAction action);
void MediaRateNoteResult(uint64_t generation, bool ok, RateAction action);
void MediaRateNotifyUi();

bool MediaLikeBridgeSelfTest();
