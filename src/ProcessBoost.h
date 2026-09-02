#pragma once

#include <string>
#include <optional>

// "Prioritizácia procesov" / "Kontrola pozadia" from the landing page copy:
// while a session is active, give the game more CPU scheduling priority and
// (optionally) ease off a small, deliberately conservative allowlist of
// common consumer background apps (browsers, chat clients, launchers).
namespace ProcessBoost
{
    struct Result
    {
        bool foregroundFound = false;
        unsigned long targetPid = 0;
        std::string targetProcessName; // e.g. "cs2" (extension stripped) — used to prefill the session's game name
        int throttledCount = 0;
    };

    struct KnownGameMatch
    {
        unsigned long pid;
        std::string displayName; // e.g. "Counter-Strike 2"
    };

    // Scans running processes for one matching the built-in known-game
    // executable list (see the table in ProcessBoost.cpp) and returns the
    // first hit, if any. This is what makes starting a session instant for
    // popular titles — no need to guess from the foreground window or make
    // the player wait through a countdown; App.cpp only falls back to that
    // when nothing here matches (an unlisted/indie game, most likely).
    std::optional<KnownGameMatch> FindRunningKnownGame();

    // Pid of a running process whose executable lives under `installDir`
    // (UTF-8, case-insensitive prefix match) — how the Hry view knows which
    // of the installed games is running right now, and which pid to boost
    // when the user starts a session from that card.
    std::optional<unsigned long> FindProcessUnderPath(const std::string& installDir);

    // Boosts a specific process id directly — used when FindRunningKnownGame()
    // already identified the target with certainty. Also, if
    // throttleBackground is set, lowers priority on any running process
    // matching the background allowlist. Safe to call repeatedly; always
    // pair with a later End().
    Result BeginForPid(unsigned long pid, bool throttleBackground);

    // Fallback for when no known game is running: boosts whatever process
    // currently owns the foreground window (skipped if that's Nasaki itself
    // — nothing to boost). Internally just resolves the pid and calls
    // BeginForPid.
    Result Begin(bool throttleBackground);

    // Restores every priority Begin()/BeginForPid() changed back to what it
    // was before. Safe to call even if nothing was adjusted.
    void End();
}
