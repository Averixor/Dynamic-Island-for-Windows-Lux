#pragma once

#include <windows.h>

// Architecture (Step D — locked):
// Custom single-dialog UI driven by kSettingSchema — NOT Win32 PROPSHEETHEADER.
// Reasons: seed keys will grow past ~100; we need a search box, group headers,
// and non-standard controls later. Property sheets have no search and fight
// custom DPI/layout. Keep one scrollable dialog + schema loop.
void OpenSettingsDialog(HWND owner);
