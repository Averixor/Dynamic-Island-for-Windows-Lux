#pragma once

// Drop-in for `#include <windhawk_api.h>` from the archived Windhawk mod.
// This tree only builds the standalone app (CMake defines LUX_STANDALONE=1);
// all Wh_* symbols resolve through windhawk_compat.h → SettingsStore / LuxLog.
//
// Prefer `#include "windhawk_compat.h"` at new call sites.

#include "windhawk_compat.h"
