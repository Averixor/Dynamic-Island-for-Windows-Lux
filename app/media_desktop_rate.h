#pragma once

#include "media_like_bridge.h"

#include <string>
#include <vector>

// Desktop Like / Dislike backends that sit beside the browser extension bridge.
// Routing, JSON, and UI Automation label rules are pure so --test-json can
// cover them. Network and IUIAutomation run on a background worker only.

constexpr int kPearDesktopApiPort = 26538;
constexpr int kYtmDesktopApiPort = 9863;
constexpr int kDesktopRateProbeTimeoutMs = 350;

enum class DesktopRateBackend {
  None,
  BrowserBridge,
  PearDesktop,
  YtmDesktop,
  UiAutomation,
};

enum class PlayerFamily {
  None,
  Browser,
  YouTubeMusicDesktop,
  YouTubeAmbiguous,
  Spotify,
  Tidal,
  Deezer,
  AppleMusic,
};

enum class ParsedLike {
  Unknown,
  None,
  Liked,
  Disliked,
};

enum class DesktopWindowKind {
  None,
  YouTubeMusicDesktop,
  Spotify,
  Tidal,
  Deezer,
  AppleMusic,
  BrowserYouTube,
};

enum class UiaRateKind {
  None,
  Like,
  Dislike,
  LibrarySave,
  LibraryRemove,
  Rejected,
};

struct SourceRoute {
  PlayerFamily family = PlayerFamily::None;
  bool allowBrowserBridge = false;
  bool allowPear = false;
  bool allowYtmDesktop = false;
  bool allowUiAutomation = false;
  bool preferDesktopApi = false;
};

struct BackendOffer {
  bool browserMatch = false;
  bool browserCanLike = false;
  bool browserCanDislike = false;
  ParsedLike browserLike = ParsedLike::Unknown;

  bool pearReachable = false;
  bool pearTrackMatch = false;
  bool pearCanLike = false;
  bool pearCanDislike = false;
  ParsedLike pearLike = ParsedLike::Unknown;

  bool ytmReachable = false;
  bool ytmTrackMatch = false;
  bool ytmCanLike = false;
  bool ytmCanDislike = false;
  ParsedLike ytmLike = ParsedLike::Unknown;
  bool ytmLegacy = false;

  bool uiaReady = false;
  bool uiaCanLike = false;
  bool uiaCanDislike = false;
  ParsedLike uiaLike = ParsedLike::Unknown;
};

struct BackendChoice {
  DesktopRateBackend backend = DesktopRateBackend::None;
  bool canLike = false;
  bool canDislike = false;
  ParsedLike like = ParsedLike::Unknown;
};

struct RateCapability {
  bool show = false;
  bool canLike = false;
  bool canDislike = false;
  bool likeOnly = false;
};

struct DesktopApiSong {
  bool ok = false;
  std::string title;
  std::string artist;
  bool playingKnown = false;
  bool playing = false;
  bool canLike = false;
  bool canDislike = false;
  ParsedLike like = ParsedLike::Unknown;
};

struct WindowIdentity {
  std::string imageBase;
  std::string title;
  std::string aumid;
  bool iconic = false;
  bool visible = false;
  int width = 0;
  int height = 0;
};

struct UiaCandidate {
  std::string name;
  std::string automationId;
  int controlType = 0;
  double left = 0;
  double top = 0;
  double width = 0;
  double height = 0;
  int toggleState = -1;
  bool pressed = false;
  bool offscreen = false;
  bool enabled = true;
  bool hasInvoke = false;
  bool hasToggle = false;
  bool hasLegacy = false;
};

struct UiaWindowBox {
  double left = 0;
  double top = 0;
  double width = 0;
  double height = 0;
};

struct UiaRateSelection {
  int likeIndex = -1;
  int dislikeIndex = -1;
  bool likeActive = false;
  bool dislikeActive = false;
};

SourceRoute ClassifyPlaybackSource(const std::string& sourceName, const std::string& appUserModelId);

BackendChoice ChooseRateBackend(const SourceRoute& route, const BackendOffer& offer);

RateCapability DescribeRateCapability(const BackendChoice& choice);

bool TracksMatch(const std::string& hintTitle, const std::string& hintArtist,
                 const std::string& remoteTitle, const std::string& remoteArtist);

bool ParsePearSongJson(const std::string& json, DesktopApiSong* out);
bool ParsePearLikeStateJson(const std::string& json, ParsedLike* out);
bool ParsePearAuthJson(const std::string& json, std::string* tokenOut);
bool ParseYtmLegacyQueryJson(const std::string& json, DesktopApiSong* out);
bool ParseYtmV1StateJson(const std::string& json, DesktopApiSong* out);

const char* YtmLegacyCommand(RateAction action);
std::string YtmV1CommandBody(RateAction action);

DesktopWindowKind ClassifyDesktopWindow(const std::string& imageBase, const std::string& title);

int ScorePlayerWindow(const SourceRoute& route, const std::string& sessionSource,
                      const std::string& sessionAumid, const std::string& trackTitle,
                      const WindowIdentity& window);

bool PreferPlayerBar(DesktopWindowKind kind, const std::string& title);

UiaRateKind ClassifyUiaRateControl(const std::string& name, const std::string& automationId);

UiaRateSelection SelectUiaRateControls(const std::vector<UiaCandidate>& controls,
                                       const UiaWindowBox& window, bool preferPlayerBar,
                                       bool trustGeometry);

BridgeLikeState ParsedLikeToBridge(ParsedLike like);

void DesktopRateStart();
void DesktopRateStop();
void DesktopRateNoteSession(const SmtcHint& hint, const std::string& appUserModelId,
                            bool browserCovers);
bool DesktopRateReadOffer(BackendOffer* offer);
bool DesktopRateEnqueue(const SmtcHint& hint, const std::string& appUserModelId, RateAction action,
                        DesktopRateBackend backend, uint64_t feedbackGeneration);

bool DesktopRateSelfTest();
