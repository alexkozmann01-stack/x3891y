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
// reboot), network stack tweaks (system-wide, easy to make worse), and
// timeBeginPeriod — Microsoft documents that since Windows 10 2004 it only
// affects the calling process, so it cannot help a game in another process,
// and that raising it can reduce overall performance and block CPU power
// saving.
namespace WinTweaks
{
    // ---- session-scoped ----

    // Switches to the High Performance power scheme, remembering the one
    // that was active. On a laptop this is the single biggest win: Balanced
    // parks cores and caps clocks during gameplay.
    bool BeginHighPerformancePower();
    void EndHighPerformancePower();

    // Game DVR and Game Mode moved into the optimization catalog
    // (optim/Catalog.cpp), which gives them backup, verification and
    // rollback instead of a bare registry write.
}
