#pragma once

// Standalone entry for `--test-json`. The classifier and 3-zone layout live in
// the island translation unit stays in one place.
bool IslandZoneSelfTest(const wchar_t** failure);
