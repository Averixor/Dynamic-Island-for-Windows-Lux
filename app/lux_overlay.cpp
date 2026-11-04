#include "lux_overlay.h"

#include "app_context.h"
#include "logging.h"
#include "settings_store.h"
#include "windhawk_compat.h"

#include <algorithm>
#include <d2d1.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace {
ComPtr<ID2D1Factory> g_d2d;
ComPtr<ID2D1HwndRenderTarget> g_rt;
bool g_workersStarted = false;
bool g_scaffoldActive = false;

void DestroyScaffold(AppState& app) {
  g_rt.Reset();
  g_d2d.Reset();
  if (app.overlayHwnd) {
    DestroyWindow(app.overlayHwnd);
    app.overlayHwnd = nullptr;
  }
  g_scaffoldActive = false;
}

void PaintPill(AppState& app) {
  if (!g_rt) {
    return;
  }
  g_rt->BeginDraw();
  g_rt->Clear(D2D1::ColorF(0.05f, 0.05f, 0.07f, 0.92f));
  ComPtr<ID2D1SolidColorBrush> brush;
  if (SUCCEEDED(g_rt->CreateSolidColorBrush(D2D1::ColorF(0.30f, 0.79f, 0.94f, 1.0f), &brush))) {
    const D2D1_SIZE_F size = g_rt->GetSize();
    const float w = 220.0f;
    const float h = 36.0f;
    const D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(
        D2D1::RectF((size.width - w) * 0.5f, 24.0f, (size.width + w) * 0.5f, 24.0f + h), h * 0.5f,
        h * 0.5f);
    g_rt->FillRoundedRectangle(rr, brush.Get());
  }
  const HRESULT hr = g_rt->EndDraw();
  InterlockedExchange(&app.lastFrameHr, static_cast<LONG>(hr));
  if (SUCCEEDED(hr)) {
    InterlockedIncrement(&app.renderedFrames);
  } else if (hr == D2DERR_RECREATE_TARGET) {
    g_rt.Reset();
  }
}

LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
  case WM_ERASEBKGND:
    return 1;
  case WM_PAINT: {
    PAINTSTRUCT ps = {};
    BeginPaint(hwnd, &ps);
    EndPaint(hwnd, &ps);
    PaintPill(GetApp());
    return 0;
  }
  case WM_DPICHANGED: {
    const RECT* suggested = reinterpret_cast<RECT*>(lParam);
    if (suggested) {
      SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                   suggested->right - suggested->left, suggested->bottom - suggested->top,
                   SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (g_rt) {
      RECT rc = {};
      GetClientRect(hwnd, &rc);
      g_rt->Resize(D2D1::SizeU(static_cast<UINT32>(std::max(1L, rc.right - rc.left)),
                               static_cast<UINT32>(std::max(1L, rc.bottom - rc.top))));
    }
    InvalidateRect(hwnd, nullptr, FALSE);
    return 0;
  }
  case WM_SIZE:
    if (g_rt) {
      const UINT w = LOWORD(lParam);
      const UINT h = HIWORD(lParam);
      if (w > 0 && h > 0) {
        g_rt->Resize(D2D1::SizeU(w, h));
      }
    }
    return 0;
  case WM_DESTROY:
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool CreateScaffoldPill(AppState& app) {
  WNDCLASSEXW wc = {};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = OverlayWndProc;
  wc.hInstance = app.instance;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.lpszClassName = kOverlayClassName;
  if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    LUX_LOGE(L"Overlay RegisterClassEx failed (%lu)", GetLastError());
    return false;
  }

  const int screenW = GetSystemMetrics(SM_CXSCREEN);
  app.overlayHwnd = CreateWindowExW(
      WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOREDIRECTIONBITMAP, kOverlayClassName,
      L"Dynamic Island Lux", WS_POPUP, (screenW - 480) / 2, 12, 480, 96, nullptr, nullptr,
      app.instance, nullptr);
  if (!app.overlayHwnd) {
    LUX_LOGE(L"Overlay CreateWindowEx failed (%lu)", GetLastError());
    return false;
  }

  HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, g_d2d.ReleaseAndGetAddressOf());
  if (FAILED(hr)) {
    LUX_LOGE(L"D2D1CreateFactory failed (0x%08lX)", static_cast<unsigned long>(hr));
    return false;
  }

  RECT rc = {};
  GetClientRect(app.overlayHwnd, &rc);
  const D2D1_SIZE_U size = D2D1::SizeU(static_cast<UINT32>(std::max(1L, rc.right - rc.left)),
                                       static_cast<UINT32>(std::max(1L, rc.bottom - rc.top)));
  hr = g_d2d->CreateHwndRenderTarget(
      D2D1::RenderTargetProperties(
          D2D1_RENDER_TARGET_TYPE_DEFAULT,
          D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED)),
      D2D1::HwndRenderTargetProperties(app.overlayHwnd, size), g_rt.ReleaseAndGetAddressOf());
  if (FAILED(hr)) {
    LUX_LOGE(L"CreateHwndRenderTarget failed (0x%08lX)", static_cast<unsigned long>(hr));
    return false;
  }

  ShowWindow(app.overlayHwnd, SW_SHOWNOACTIVATE);
  g_scaffoldActive = true;
  return true;
}
}  // namespace

bool LuxOverlay_Init(AppState& app) {
  // Smoke / --no-workers: local scaffold pill (no WhTool render thread).
  // Normal run: island HWND comes solely from WhTool_ModInit → StartThreads.
  if (LuxSmokeTestFlag() || !LuxShouldStartWorkers()) {
    if (!CreateScaffoldPill(app)) {
      return false;
    }
    const bool lookup = Wh_GetIntSetting(L"Modules.QuickLookup") != 0;
    Wh_Log(L"LuxOverlay_Init scaffold ready (QuickLookup=%d)", lookup ? 1 : 0);
    return true;
  }

  LUX_LOGI(L"LuxOverlay_Init deferred UI to WhTool_ModInit (workers path)");
  return true;
}

void LuxOverlay_Uninit(AppState& app) {
  // Order: stop workers (wait threads) → then destroy scaffold D2D/HWND.
  LuxOverlay_StopWorkers(app);
  DestroyScaffold(app);
}

bool LuxOverlay_PumpFrame(AppState& app) {
  if (!app.overlayHwnd) {
    // Workers path: no scaffold; smoke should use --no-workers for frame criterion.
    return false;
  }
  InvalidateRect(app.overlayHwnd, nullptr, FALSE);
  UpdateWindow(app.overlayHwnd);
  return InterlockedCompareExchange(&app.renderedFrames, 0, 0) > 0 &&
         SUCCEEDED(static_cast<HRESULT>(InterlockedCompareExchange(&app.lastFrameHr, 0, 0)));
}

void LuxOverlay_OnSettingsChanged() {
  // Store reload on main (fast). WhTool_ModSettingsChanged posts LoadSettings to the
  // render thread when the island HWND exists — OK click must not stall the island.
  SettingsStoreReload();
  WhTool_ModSettingsChanged();
  LUX_LOGI(L"LuxOverlay_OnSettingsChanged queued/applied");
}

bool LuxOverlay_StartWorkers(AppState& app) {
  (void)app;
  if (!LuxShouldStartWorkers()) {
    // After Step D: autostart/dialog are sync on main — no extra threads here either.
    LUX_LOGI(L"LuxOverlay_StartWorkers skipped (--no-workers) — no CreateThread/hooks/hotkeys");
    return true;
  }
  if (g_workersStarted) {
    LUX_LOGW(L"LuxOverlay_StartWorkers: already started (ignore duplicate)");
    return true;
  }

  // Exactly one lifecycle entry: WhTool_ModInit (NOT Wh_ModInit — gated out under LUX_STANDALONE).
  LUX_LOGI(L"worker bootstrap: calling WhTool_ModInit");
  if (!WhTool_ModInit()) {
    LUX_LOGE(L"WhTool_ModInit failed");
    return false;
  }
  g_workersStarted = true;
  LUX_LOGI(L"worker bootstrap: WhTool_ModInit OK (render/media/audio/weather/...)");
  return true;
}

void LuxOverlay_StopWorkers(AppState& app) {
  (void)app;
  if (!g_workersStarted) {
    return;
  }
  LUX_LOGI(L"worker shutdown: calling WhTool_ModUninit");
  WhTool_ModUninit();
  g_workersStarted = false;
  LUX_LOGI(L"worker shutdown: WhTool_ModUninit returned");
}
