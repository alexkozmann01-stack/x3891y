#pragma once

#include "Optimization.h"
#include "Catalog.h"
#include "BackupStore.h"
#include "StartupEntries.h"
#include "PowerPlans.h"
#include "StorageCleanup.h"
#include "Profiles.h"
#include "../ApiWorker.h"

#include <memory>
#include <vector>
#include <mutex>
#include <atomic>
#include <string>

namespace optim
{
    // Owns the catalog and the backup journal, and is the only thing the UI
    // talks to. Every registry/API touch happens on the shared worker
    // thread; the render loop only ever reads a cached snapshot, so a slow
    // scan can never stall a frame.
    class Service
    {
    public:
        struct Row
        {
            const Info* info = nullptr;
            Status status;
            bool hasBackup = false;
            bool busy = false;      // an apply/restore is in flight
        };

        Service(ApiWorker* worker);

        // Kicks off a background read of every optimization's live state.
        void RefreshAsync();
        bool Scanning() const { return m_scanning.load(); }

        void ApplyAsync(const std::string& id);
        void RestoreAsync(const std::string& id);

        // Snapshot for this frame. Cheap copy; never touches the registry.
        std::vector<Row> Rows() const;

        // Drains results produced by the worker. Call once per frame from
        // the render thread.
        void Pump();

        int AppliedCount() const;
        std::vector<BackupStore::HistoryRecord> History() const { return m_backups.History(); }

        // What the catalog's recommendations were computed from. Read once at
        // construction and shown on the dashboard so the user can check that
        // Nasaki actually identified their machine correctly.
        const SystemInventory& Inventory() const { return m_inventory; }

        // How many entries this machine is actually advised to change.
        int RecommendedCount() const;

        // ---- startup programs -------------------------------------------
        // Kept here rather than in a separate service so startup changes land
        // in the same backup journal and history as everything else.

        void RefreshStartupAsync();
        std::vector<StartupEntry> StartupPrograms() const;
        bool StartupBusy() const { return m_startupBusy.load(); }

        // Both act on one named entry only; there is deliberately no
        // "remove everything" path.
        void RemoveStartupAsync(const std::string& entryId);
        void RestoreStartupAsync(const std::string& entryId);

        // ---- power plans -------------------------------------------------

        void RefreshPowerPlansAsync();
        std::vector<PowerPlan> PowerPlans() const;
        bool PowerBusy() const { return m_powerBusy.load(); }
        bool HasOriginalPowerPlan() const { return m_power.HasOriginal(); }
        std::string OriginalPowerPlanName() const { return m_power.OriginalPlanName(); }
        std::string PowerGuidance() const { return PowerPlanManager::GuidanceFor(m_inventory); }

        void ActivatePowerPlanAsync(const std::string& guid);
        void RestorePowerPlanAsync();

        // ---- storage -----------------------------------------------------

        void RefreshStorageAsync();
        std::vector<CleanupTarget> StorageTargets() const;
        bool StorageBusy() const { return m_storageBusy.load(); }

        // The exact file list a clean would remove. Deleting is only offered
        // once this has been produced for that target, so nothing is ever
        // removed sight unseen.
        struct StoragePreview
        {
            std::string targetId;
            std::vector<std::string> lines;
            bool truncated = false;
        };
        void RequestStoragePreviewAsync(const std::string& targetId);
        StoragePreview CurrentStoragePreview() const;
        void ClearStoragePreview();

        void CleanStorageAsync(const std::string& targetId);

        // ---- profiles ----------------------------------------------------

        const std::vector<Profile>& Profiles() const { return m_profiles; }

        // Exactly what applying a profile would change, one line per setting,
        // so the bundle is never a black box.
        struct ProfileStep
        {
            std::string title;
            std::string changeSummary;
            State currentState = State::Unknown;
            bool supported = true;
        };
        std::vector<ProfileStep> PreviewProfile(const std::string& profileId) const;

        // Applies each member setting through the normal capture/write/verify
        // path and reports how many actually landed.
        void ApplyProfileAsync(const std::string& profileId);

        // Restores everything Nasaki holds a backup for, one setting at a
        // time, and reports failures individually.
        void RestoreEverythingAsync();
        int BackedUpCount() const;

        std::vector<BackupEntry> BackupEntries() const { return m_backups.Entries(); }

        // Last operation's outcome, for the inline status line.
        struct Outcome
        {
            std::string id;
            std::string action;
            bool success = false;
            std::string message;
        };
        std::optional<Outcome> LastOutcome() const;

    private:
        Optimization* Find(const std::string& id);

        ApiWorker* m_worker;
        SystemInventory m_inventory;
        BackupStore m_backups;
        std::vector<std::unique_ptr<Optimization>> m_catalog;
        StartupManager m_startup;

        std::vector<StartupEntry> m_startupEntries;
        std::atomic<bool> m_startupBusy{ false };

        PowerPlanManager m_power;
        std::vector<PowerPlan> m_powerPlans;
        std::atomic<bool> m_powerBusy{ false };

        std::vector<Profile> m_profiles;

        StorageCleaner m_storage;
        std::vector<CleanupTarget> m_storageTargets;
        StoragePreview m_storagePreview;
        std::atomic<bool> m_storageBusy{ false };

        mutable std::mutex m_mutex;
        std::vector<Status> m_states;     // parallel to m_catalog
        std::vector<bool> m_busy;         // parallel to m_catalog
        std::vector<bool> m_hasBackup;
        std::atomic<bool> m_scanning{ false };

        // Worker -> UI mailbox, drained in Pump().
        struct PendingResult
        {
            size_t index = 0;
            Status status;
            bool clearBusy = true;
            std::optional<Outcome> outcome;
        };
        std::vector<PendingResult> m_pending;
        std::optional<Outcome> m_lastOutcome;
    };
}
