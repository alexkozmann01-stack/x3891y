#pragma once

#include "Optimization.h"

#include <string>
#include <vector>
#include <cstdint>

// Installed programs, read from the Uninstall keys Windows itself uses.
//
// This is an inventory, not a remover. Nasaki never deletes a program's
// files, never runs a silent mass uninstall, and never "cleans up leftovers".
// The only action is launching the publisher's own uninstaller for one
// program the user picked — the same thing Settings does.
//
// Runtime dependencies (Visual C++ redistributables, .NET, WebView2, driver
// packages) are flagged as protected and offered no uninstall button: they
// are usually required by something else on the machine, and removing them
// breaks programs the user never associated with the entry.
namespace optim
{
    struct InstalledApp
    {
        std::string id;            // registry key name; stable
        std::string name;
        std::string version;
        std::string publisher;
        std::string installDate;   // as reported, "YYYYMMDD"
        uint64_t estimatedBytes = 0; // 0 when the installer didn't record it

        std::string uninstallCommand;
        bool uninstallable = false;
        bool protectedComponent = false;
        std::string protectionReason;
        bool perMachine = false;   // HKLM entries usually need admin to remove
    };

    class AppInventory
    {
    public:
        // Reads all three Uninstall locations. Skips system components and
        // update entries, which are not user-facing programs.
        std::vector<InstalledApp> Scan() const;

        // Launches the publisher's uninstaller for one program. Returns an
        // error if the entry is protected or has no usable command.
        Error LaunchUninstaller(const InstalledApp& app) const;

        static void OpenWindowsAppsSettings();
    };
}
