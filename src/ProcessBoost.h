#pragma once

#include <string>

// "Prioritizácia procesov" / "Kontrola pozadia" from the landing page copy:
// while a session is active, give the game more CPU scheduling priority and
// (optionally) ease off a small, deliberately conservative allowlist of
// common consumer background apps (browsers, chat clients, launchers).
//
// There's no game-render hook in this build (see README), so "which process
// is the game" can't be known automatically the instant the user clicks
// Start — App.cpp handles that by running a short countdown after the click
// so the user can switch to the game first, then calls Begin(), which reads
// whatever process is in the foreground at that moment.
namespace ProcessBoost
{
    struct Result
    {
        bool foregroundFound = false;
        unsigned long targetPid = 0;
        std::string targetProcessName; // e.g. "cs2" (extension stripped) — used to prefill the session's game name
        int throttledCount = 0;
    };

    // Boosts the current foreground process (skipped if that's Nasaki
    // itself — nothing to boost) and, if throttleBackground is set, lowers
    // priority on any running process matching the background allowlist.
    // Safe to call repeatedly; always pair with a later End().
    Result Begin(bool throttleBackground);

    // Restores every priority Begin() changed back to what it was before.
    // Safe to call even if Begin() adjusted nothing.
    void End();
}
