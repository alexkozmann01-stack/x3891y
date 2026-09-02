#pragma once

#include <string>
#include <vector>

// Enumerates games actually installed on this PC by reading each launcher's
// own on-disk bookkeeping — Steam's appmanifest_*.acf files, Epic's
// .item manifests, GOG's registry entries. No guessing from process names
// (that's ProcessBoost's job, and only works while a game is running).
struct InstalledGame
{
    std::string name;
    std::string source;      // "Steam" | "Epic" | "GOG"
    std::string installPath; // may be empty if the launcher didn't record one
};

namespace GameLibrary
{
    // Touches the registry and walks a few directories, so this takes long
    // enough that App runs it on the worker thread rather than inline in a
    // frame. Sorted by name; duplicates across launchers are not merged.
    std::vector<InstalledGame> Scan();
}
