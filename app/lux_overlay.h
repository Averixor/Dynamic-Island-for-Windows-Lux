#pragma once

#include "main.h"

// Overlay bridge between WinMain and the island implementation.
// - Smoke / --no-workers: local D2D scaffold HWND (frame criterion).
// - Normal run: IslandRuntimeStart owns the real island HWND + workers.
// Create COM/D2D only here or inside the app runtime — never in static constructors.

bool LuxOverlay_Init(AppState& app);
void LuxOverlay_Uninit(AppState& app);
void LuxOverlay_OnSettingsChanged();

// Clear manual/auto hide and show the island (tray / second-instance / reload).
void LuxOverlay_ShowIsland();

// Hide the island (autostart --minimized / tray toggle).
void LuxOverlay_HideIsland();

// Tray left-click: hide if visible, show if hidden/parked.
void LuxOverlay_ToggleIsland();

// Reload settings from disk and re-apply on the render thread.
void LuxOverlay_Reload();

// Request one scaffold paint; true when renderedFrames > 0 and lastFrameHr == S_OK.
bool LuxOverlay_PumpFrame(AppState& app);

// Start/stop background workers after Ready is signaled.
bool LuxOverlay_StartWorkers(AppState& app);
void LuxOverlay_StopWorkers(AppState& app);

// Island runtime entry points.
BOOL IslandRuntimeStart();
void IslandRuntimeSettingsChanged();
void IslandRuntimeStop();
