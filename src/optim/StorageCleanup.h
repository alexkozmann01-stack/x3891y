#pragma once

#include "Optimization.h"
#include "BackupStore.h"
#include "SystemInventory.h"

#include <string>
#include <vector>
#include <cstdint>

// Disk space. Deleting files is the one thing here that cannot be rolled
// back, so this module is built around that fact:
//
//   * Nothing is deleted without the user selecting that specific target.
//   * Every target can be previewed — the actual file list, before anything
//     happens — rather than a size and a promise.
//   * Personal folders (Downloads, Documents) are measured and reported but
//     never deletable from here. They are listed so the user can see where
//     the space went, and cleaned by the user in Explorer.
//   * Temporary files still in use are skipped, not forced. Files younger
//     than the age threshold are left alone because a running program is
//     probably still using them.
//   * There is no registry cleaning, no "junk file" heuristics, and no
//     system folders that need elevation — Windows' own Disk Cleanup is
//     linked instead.
namespace optim
{
    struct CleanupTarget
    {
        std::string id;
        std::string name;
        std::string path;          // shown so the user can check it themselves
        std::string description;
        std::string caution;       // what could go wrong; empty if nothing

        uint64_t bytes = 0;
        uint64_t deletableBytes = 0; // subset old enough / safe to remove
        int fileCount = 0;
        int deletableFileCount = 0;

        bool deletable = false;      // false = we only report the size
        bool scanned = false;        // false = the size is not known yet
    };

    struct CleanupResult
    {
        uint64_t bytesFreed = 0;
        int filesDeleted = 0;
        int filesSkipped = 0;       // in use or access denied — reported, not hidden
    };

    class StorageCleaner
    {
    public:
        explicit StorageCleaner(BackupStore* backups);

        // Walks the known locations and measures them. Slow enough to belong
        // on the worker thread.
        std::vector<CleanupTarget> Analyze(const SystemInventory& inventory) const;

        // The exact paths a clean would remove, capped so the UI stays
        // responsive. `truncated` says whether more exist than were returned.
        std::vector<std::string> Preview(const std::string& targetId, size_t maxItems,
                                         bool* truncated = nullptr) const;

        // Deletes only what Preview listed. Reports what it could not remove
        // instead of retrying with force.
        Error Clean(const std::string& targetId, CleanupResult* result);

        // Windows' own cleanup for the system locations that need elevation.
        static void OpenWindowsDiskCleanup();
        static void OpenStorageSettings();

        // Files younger than this are assumed to be in use by a running
        // program and are never touched.
        static constexpr int kMinimumAgeDays = 7;

    private:
        BackupStore* m_backups;
    };
}
