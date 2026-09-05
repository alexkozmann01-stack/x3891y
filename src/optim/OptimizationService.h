#pragma once

#include "Optimization.h"
#include "Catalog.h"
#include "BackupStore.h"
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
        BackupStore m_backups;
        std::vector<std::unique_ptr<Optimization>> m_catalog;

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
