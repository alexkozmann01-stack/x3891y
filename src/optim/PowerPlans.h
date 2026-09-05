#pragma once

#include "Optimization.h"
#include "BackupStore.h"
#include "SystemInventory.h"

#include <string>
#include <vector>

// Windows power plans, read and switched through the documented Power
// Management API (PowerEnumerate / PowerReadFriendlyName /
// PowerGetActiveScheme / PowerSetActiveScheme).
//
// Only whole plans are switched. Individual plan settings are deliberately
// left alone: core parking and processor performance thresholds are the
// classic "universal CPU tweak", and applying one blindly across machines is
// exactly what the design forbids. Choosing a plan Windows itself ships is
// both reversible and something the OS is designed for.
namespace optim
{
    struct PowerPlan
    {
        std::string guid;        // canonical braces-less GUID string
        std::string name;        // friendly name as Windows reports it
        std::string description;
        bool active = false;
        // Windows' three shipped plans get a plain-language note; OEM plans
        // are listed as-is without us inventing claims about them.
        std::string note;
    };

    class PowerPlanManager
    {
    public:
        explicit PowerPlanManager(BackupStore* backups);

        std::vector<PowerPlan> Enumerate() const;

        // Switches the active plan, then re-reads it and reports a mismatch
        // as a failure. The plan that was active the first time we switch is
        // recorded so it can be put back exactly.
        Error Activate(const std::string& guid);

        // Returns to the plan that was active before Nasaki first changed it.
        Error RestoreOriginal();
        bool HasOriginal() const;
        std::string OriginalPlanName() const;

        // Battery-aware advice for the current machine. Empty when there is
        // nothing worth saying.
        static std::string GuidanceFor(const SystemInventory& inventory);

    private:
        static const char* kJournalId;

        BackupStore* m_backups;
    };
}
