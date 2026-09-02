#pragma once

// Windows-level optimizations that are genuinely deliverable without admin
// rights, a kernel driver, or a reboot — and that can all be undone.
//
// Two kinds live here:
//   * Session-scoped (Begin/End): applied when a session starts, reverted
//     when it ends, so the machine is never left in a changed state.
//   * Persistent toggles: real HKCU settings the user opts into. Each one
//     reads its actual current state, so the UI reflects the system rather
//     than a value we made up, and flipping the toggle back restores it.
//
// Deliberately excluded: anything under HKLM (needs admin), HAGS (needs a
// reboot), and network stack tweaks (system-wide, easy to make worse).
namespace WinTweaks
{
    // ---- session-scoped ----

    // Switches to the High Performance power scheme, remembering the one
    // that was active. On a laptop this is the single biggest win: Balanced
    // parks cores and caps clocks during gameplay.
    bool BeginHighPerformancePower();
    void EndHighPerformancePower();

    // timeBeginPeriod(1): finer scheduler/sleep granularity, which some
    // engines' frame pacing depends on.
    void BeginHighResolutionTimer();
    void EndHighResolutionTimer();

    // ---- persistent HKCU toggles ----

    // Xbox Game DVR background recording — a well-known source of overhead
    // during gameplay.
    bool IsGameDvrDisabled();
    void SetGameDvrDisabled(bool disabled);

    // Windows' own Game Mode.
    bool IsGameModeEnabled();
    void SetGameModeEnabled(bool enabled);
}
