#pragma once

#include "main.h"

// Overlay owns its HWND and D2D resources. It does NOT own WinMain or the message loop.
// Create COM/D2D only here — never in static constructors.
bool LuxOverlay_Init(AppState& app);
void LuxOverlay_Uninit(AppState& app);
void LuxOverlay_OnSettingsChanged();

// Clear manual/auto hide and show the overlay (tray / second-instance hop).
void LuxOverlay_ShowIsland();

// Tray left-click: hide island if visible, show if hidden/parked.
void LuxOverlay_ToggleIsland();

// Reload settings from disk and re-apply on the render thread.
void LuxOverlay_Reload();

// Request one paint; returns true when renderedFrames > 0 and lastFrameHr == S_OK.
bool LuxOverlay_PumpFrame(AppState& app);

// Optional: start/stop background workers after Ready is signaled.
bool LuxOverlay_StartWorkers(AppState& app);
void LuxOverlay_StopWorkers(AppState& app);

// Legacy body entry (from .wh.cpp under LUX_STANDALONE). Step A: linked; Init not called from smoke.
BOOL WhTool_ModInit();
void WhTool_ModSettingsChanged();
void WhTool_ModUninit();
