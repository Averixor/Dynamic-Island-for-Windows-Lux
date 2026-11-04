#pragma once

// Makes Win32 popup menus (TrackPopupMenu) follow AppsUseLightTheme.
namespace MenuTheme {

// Call once at process start (optional but recommended).
void EnableSystemMenus();

// Call immediately before CreatePopupMenu / TrackPopupMenu so live theme
// switches are picked up without restart.
void PreparePopup();

}  // namespace MenuTheme
