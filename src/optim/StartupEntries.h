#pragma once

#include "Optimization.h"
#include "RegistryValue.h"
#include "BackupStore.h"

#include <string>
#include <vector>

// Programs that launch with Windows. This is the one area where a cleanup
// tool can genuinely shorten boot and free memory, so it is worth doing
// properly rather than with a blanket "disable everything" button.
//
// What this does and does not touch:
//   * HKCU\...\Run — enumerated and *reversibly removable*. Removing stores
//     the value's exact name, type and bytes in the backup journal, so
//     re-enabling puts back the original command byte-for-byte.
//   * HKLM\...\Run, the Startup folders, and scheduled tasks — enumerated
//     read-only. HKLM needs elevation, and Task Manager's own enable/disable
//     works through an undocumented StartupApproved blob that we won't write
//     blind. These are listed with a link to where Windows exposes them.
//
// Nothing is ever disabled in bulk, and nothing is disabled without the user
// picking that specific entry.
namespace optim
{
    struct StartupEntry
    {
        std::string id;         // stable key, e.g. "hkcu-run:Discord"
        std::string name;       // the Run value name, or the shortcut filename
        std::string command;    // full command line as stored
        std::string executable; // best-effort path of the exe, for display
        std::string location;   // human-readable origin
        bool removable = false; // true only for HKCU Run values
        bool removed = false;   // we removed it and hold the original
        bool needsAdmin = false;
    };

    class StartupManager
    {
    public:
        explicit StartupManager(BackupStore* backups);

        // Live scan: current Run values, Startup-folder shortcuts, and any
        // entries Nasaki previously removed (so they can be put back).
        std::vector<StartupEntry> Scan() const;

        // Captures the value exactly, then deletes it. Verified by reading
        // back: if the value is still there afterwards, this reports failure.
        Error Remove(const std::string& id);

        // Writes the captured value back and verifies it matches.
        Error Restore(const std::string& id);

        // Where Windows itself manages the entries we won't write.
        static void OpenWindowsStartupSettings();

        // Redirects the HKCU Run key so tests can exercise the real registry
        // code paths without touching the user's actual startup programs.
        static void SetRunSubKeyForTesting(const std::wstring& subKey);

    private:
        static const char* kJournalId; // BackupStore grouping key

        BackupStore* m_backups;
    };
}
