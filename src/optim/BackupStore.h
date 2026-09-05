#pragma once

#include "RegistryValue.h"

#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <optional>

// Persistent record of what we changed and what was there before, written
// to %APPDATA%\Nasaki\backups.json.
//
// Two rules the rest of the code depends on:
//   * A backup is written once, before the first write, and never
//     overwritten by a later apply. Otherwise applying twice would record
//     our own value as the "original" and rollback would be a no-op.
//   * A snapshot records whether the value existed at all, so restoring can
//     delete it rather than inventing a default.
namespace optim
{
    struct BackupEntry
    {
        std::string optimizationId;
        std::string valueKey;  // which value within the optimization (one optimization may touch several)
        RegSnapshot original;
        std::string appliedAtUtc;
        std::string note;      // free text shown in history, e.g. the title at time of apply
    };

    class BackupStore
    {
    public:
        // Loads the journal from disk; a missing or corrupt file starts empty
        // rather than failing (and the corrupt file is left alone, not
        // truncated, so it can be inspected).
        void Load();

        // Records `original` for (id, valueKey) unless one is already
        // recorded. Returns true if this call is what created the entry.
        bool CaptureIfAbsent(const std::string& id, const std::string& valueKey,
                             const RegSnapshot& original, const std::string& note);

        std::optional<RegSnapshot> Find(const std::string& id, const std::string& valueKey) const;
        bool Has(const std::string& id) const;

        // Drops the entries for an optimization — called after a verified
        // restore, so the next apply captures fresh.
        void Forget(const std::string& id);

        std::vector<BackupEntry> Entries() const;

        // Journal of everything applied/restored, newest last. Kept separate
        // from the backup entries so history survives a restore.
        struct HistoryRecord
        {
            std::string timestampUtc;
            std::string optimizationId;
            std::string action;   // "apply" | "restore"
            bool success = false;
            std::string message;
        };
        void RecordHistory(const std::string& id, const std::string& action, bool success, const std::string& message);
        std::vector<HistoryRecord> History() const;

        bool Save();

        // Redirects the journal to another file. Tests use this so they can
        // exercise the real save/load path without touching the user's
        // actual %APPDATA% journal.
        void SetStorePathForTesting(const std::wstring& path);

    private:
        std::string Key(const std::string& id, const std::string& valueKey) const;

        mutable std::mutex m_mutex;
        std::map<std::string, BackupEntry> m_entries; // keyed by id + '/' + valueKey
        std::vector<HistoryRecord> m_history;
        bool m_loaded = false;
        std::wstring m_pathOverride;
    };
}
