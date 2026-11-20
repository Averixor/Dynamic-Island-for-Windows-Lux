#pragma once

// Makes Win32 popup menus (TrackPopupMenu) follow the island theme's
// background luminance (light presets such as Porcelain use a light menu)
// via undocumented uxtheme ordinals (Win10 1903+ / Win11). Older builds no-op.
namespace MenuTheme {

// Call once at process start (optional but recommended).
void EnableSystemMenus();

// Call immediately before CreatePopupMenu / TrackPopupMenu so live theme
// switches are picked up without restart.
void PreparePopup();

}  // namespace MenuTheme
