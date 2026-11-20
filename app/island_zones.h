#pragma once

// Standalone entry for `--test-json`. The classifier and 3-zone layout live in
// the island translation unit so the Windhawk single-file mod stays intact.
bool IslandZoneSelfTest(const wchar_t** failure);
