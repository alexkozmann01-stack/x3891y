#include "OptimizationService.h"

namespace optim
{
    Service::Service(ApiWorker* worker) : m_worker(worker), m_startup(&m_backups), m_power(&m_backups), m_storage(&m_backups)
    {
        // Read the machine before building the catalog — the inventory is
        // what decides which entries are recommended here, so it cannot be
        // deferred to the first background scan.
        m_inventory = ReadSystemInventory();

        m_backups.Load();
        m_catalog = BuildCatalog(&m_backups, m_inventory);

        m_states.resize(m_catalog.size());
        m_busy.assign(m_catalog.size(), false);
        m_hasBackup.assign(m_catalog.size(), false);
    }

    Optimization* Service::Find(const std::string& id)
    {
        for (auto& entry : m_catalog)
        {
            if (entry->info().id == id) return entry.get();
        }
        return nullptr;
    }

    void Service::RefreshAsync()
    {
        if (m_scanning.exchange(true))
        {
            return; // a scan is already running
        }

        m_worker->Enqueue([this]() {
            for (size_t i = 0; i < m_catalog.size(); i++)
            {
                Status status = m_catalog[i]->Read();
                bool hasBackup = m_backups.Has(m_catalog[i]->info().id);

                std::lock_guard<std::mutex> lock(m_mutex);
                PendingResult result;
                result.index = i;
                result.status = status;
                result.clearBusy = false;
                m_pending.push_back(result);
                m_hasBackup[i] = hasBackup;
            }
            m_scanning.store(false);
        });
    }

    void Service::ApplyAsync(const std::string& id)
    {
        Optimization* target = Find(id);
        if (!target) return;

        size_t index = 0;
        for (size_t i = 0; i < m_catalog.size(); i++)
        {
            if (m_catalog[i].get() == target) { index = i; break; }
        }

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_busy[index]) return; // already in flight
            m_busy[index] = true;
        }

        m_worker->Enqueue([this, target, index]() {
            Error error = target->Apply();
            // Always re-read: on failure the machine may be partially
            // changed, and the card must show what's actually true now.
            Status status = target->Read();
            if (!error.ok())
            {
                status.state = State::Failed;
                status.lastError = error;
            }
            // A manual-guide entry's "apply" only opens a Settings page, so
            // it must never be reported as a verified change.
            bool manual = status.state == State::Manual;
            std::string message = error.ok()
                ? (manual ? "Otvorené v Nastaveniach Windows — zmenu vykonáš tam."
                          : "Použité a overené.")
                : error.message;
            const char* action = manual ? "open-settings" : "apply";

            m_backups.RecordHistory(target->info().id, action, error.ok(), message);
            m_backups.Save();

            std::lock_guard<std::mutex> lock(m_mutex);
            PendingResult result;
            result.index = index;
            result.status = status;
            result.outcome = Outcome{ target->info().id, action, error.ok(), message };
            m_pending.push_back(result);
            m_hasBackup[index] = m_backups.Has(target->info().id);
        });
    }

    void Service::RestoreAsync(const std::string& id)
    {
        Optimization* target = Find(id);
        if (!target) return;

        size_t index = 0;
        for (size_t i = 0; i < m_catalog.size(); i++)
        {
            if (m_catalog[i].get() == target) { index = i; break; }
        }

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_busy[index]) return;
            m_busy[index] = true;
        }

        m_worker->Enqueue([this, target, index]() {
            Error error = target->Restore();
            Status status = target->Read();
            if (!error.ok())
            {
                status.state = State::Failed;
                status.lastError = error;
            }
            m_backups.RecordHistory(target->info().id, "restore", error.ok(),
                error.ok() ? "Obnovené na pôvodnú hodnotu." : error.message);
            m_backups.Save();

            std::lock_guard<std::mutex> lock(m_mutex);
            PendingResult result;
            result.index = index;
            result.status = status;
            result.outcome = Outcome{ target->info().id, "restore", error.ok(),
                error.ok() ? "Obnovené na pôvodnú hodnotu." : error.message };
            m_pending.push_back(result);
            m_hasBackup[index] = m_backups.Has(target->info().id);
        });
    }

    void Service::Pump()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const PendingResult& result : m_pending)
        {
            if (result.index < m_states.size())
            {
                m_states[result.index] = result.status;
                if (result.clearBusy)
                {
                    m_busy[result.index] = false;
                }
            }
            if (result.outcome.has_value())
            {
                m_lastOutcome = result.outcome;
            }
        }
        m_pending.clear();
    }

    std::vector<Service::Row> Service::Rows() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<Row> rows;
        rows.reserve(m_catalog.size());
        for (size_t i = 0; i < m_catalog.size(); i++)
        {
            Row row;
            row.info = &m_catalog[i]->info();
            row.status = m_states[i];
            row.hasBackup = m_hasBackup[i];
            row.busy = m_busy[i];
            rows.push_back(row);
        }
        return rows;
    }

    int Service::AppliedCount() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        int count = 0;
        for (const Status& status : m_states)
        {
            if (status.state == State::Applied) count++;
        }
        return count;
    }

    void Service::RefreshStartupAsync()
    {
        if (m_startupBusy.exchange(true))
        {
            return;
        }

        m_worker->Enqueue([this]() {
            std::vector<StartupEntry> entries = m_startup.Scan();
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_startupEntries = std::move(entries);
            }
            m_startupBusy.store(false);
        });
    }

    std::vector<StartupEntry> Service::StartupPrograms() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_startupEntries;
    }

    void Service::RemoveStartupAsync(const std::string& entryId)
    {
        if (m_startupBusy.exchange(true)) return;

        m_worker->Enqueue([this, entryId]() {
            Error error = m_startup.Remove(entryId);
            std::vector<StartupEntry> entries = m_startup.Scan();

            std::lock_guard<std::mutex> lock(m_mutex);
            m_startupEntries = std::move(entries);
            m_lastOutcome = Outcome{ entryId, "startup-remove", error.ok(),
                error.ok() ? "Odstránené zo spúšťania. Pôvodnú položku držíme v zálohe."
                           : error.message };
            m_startupBusy.store(false);
        });
    }

    void Service::RestoreStartupAsync(const std::string& entryId)
    {
        if (m_startupBusy.exchange(true)) return;

        m_worker->Enqueue([this, entryId]() {
            Error error = m_startup.Restore(entryId);
            std::vector<StartupEntry> entries = m_startup.Scan();

            std::lock_guard<std::mutex> lock(m_mutex);
            m_startupEntries = std::move(entries);
            m_lastOutcome = Outcome{ entryId, "startup-restore", error.ok(),
                error.ok() ? "Vrátené do spúšťania v pôvodnom znení." : error.message };
            m_startupBusy.store(false);
        });
    }

    void Service::RefreshPowerPlansAsync()
    {
        if (m_powerBusy.exchange(true)) return;

        m_worker->Enqueue([this]() {
            std::vector<PowerPlan> plans = m_power.Enumerate();
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_powerPlans = std::move(plans);
            }
            m_powerBusy.store(false);
        });
    }

    std::vector<PowerPlan> Service::PowerPlans() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_powerPlans;
    }

    void Service::ActivatePowerPlanAsync(const std::string& guid)
    {
        if (m_powerBusy.exchange(true)) return;

        m_worker->Enqueue([this, guid]() {
            Error error = m_power.Activate(guid);
            std::vector<PowerPlan> plans = m_power.Enumerate();

            std::lock_guard<std::mutex> lock(m_mutex);
            m_powerPlans = std::move(plans);
            m_lastOutcome = Outcome{ guid, "power-activate", error.ok(),
                error.ok() ? "Plán napájania prepnutý a overený."
                           : error.message };
            m_powerBusy.store(false);
        });
    }

    void Service::RestorePowerPlanAsync()
    {
        if (m_powerBusy.exchange(true)) return;

        m_worker->Enqueue([this]() {
            Error error = m_power.RestoreOriginal();
            std::vector<PowerPlan> plans = m_power.Enumerate();

            std::lock_guard<std::mutex> lock(m_mutex);
            m_powerPlans = std::move(plans);
            m_lastOutcome = Outcome{ "power.active_plan", "power-restore", error.ok(),
                error.ok() ? "Pôvodný plán napájania obnovený." : error.message };
            m_powerBusy.store(false);
        });
    }

    void Service::RefreshStorageAsync()
    {
        if (m_storageBusy.exchange(true)) return;

        m_worker->Enqueue([this]() {
            std::vector<CleanupTarget> targets = m_storage.Analyze(m_inventory);
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_storageTargets = std::move(targets);
            }
            m_storageBusy.store(false);
        });
    }

    std::vector<CleanupTarget> Service::StorageTargets() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_storageTargets;
    }

    void Service::RequestStoragePreviewAsync(const std::string& targetId)
    {
        if (m_storageBusy.exchange(true)) return;

        m_worker->Enqueue([this, targetId]() {
            bool truncated = false;
            std::vector<std::string> lines = m_storage.Preview(targetId, 200, &truncated);

            std::lock_guard<std::mutex> lock(m_mutex);
            m_storagePreview.targetId = targetId;
            m_storagePreview.lines = std::move(lines);
            m_storagePreview.truncated = truncated;
            m_storageBusy.store(false);
        });
    }

    Service::StoragePreview Service::CurrentStoragePreview() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_storagePreview;
    }

    void Service::ClearStoragePreview()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_storagePreview = StoragePreview{};
    }

    void Service::CleanStorageAsync(const std::string& targetId)
    {
        if (m_storageBusy.exchange(true)) return;

        m_worker->Enqueue([this, targetId]() {
            CleanupResult cleanup;
            Error error = m_storage.Clean(targetId, &cleanup);
            std::vector<CleanupTarget> targets = m_storage.Analyze(m_inventory);

            std::string message;
            if (error.ok())
            {
                message = "Uvoľnené " + FormatBytes(cleanup.bytesFreed) + " (" +
                          std::to_string(cleanup.filesDeleted) + " položiek).";
                if (cleanup.filesSkipped > 0)
                {
                    // Skipped files are part of the result, not an omission:
                    // reporting only the successes would overstate the work.
                    message += " " + std::to_string(cleanup.filesSkipped) +
                               " súborov sa práve používa, tie sme nechali tak.";
                }
            }
            else
            {
                message = error.message;
            }

            std::lock_guard<std::mutex> lock(m_mutex);
            m_storageTargets = std::move(targets);
            m_storagePreview = StoragePreview{};
            m_lastOutcome = Outcome{ targetId, "storage-clean", error.ok(), message };
            m_storageBusy.store(false);
        });
    }

    int Service::RecommendedCount() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        int count = 0;
        for (size_t i = 0; i < m_catalog.size(); i++)
        {
            // Only counts what is both advised here and not already in place;
            // an entry the system already satisfies is not outstanding work.
            if (m_catalog[i]->info().classification == Classification::Recommended &&
                m_states[i].state == State::NotApplied)
            {
                count++;
            }
        }
        return count;
    }

    std::optional<Service::Outcome> Service::LastOutcome() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_lastOutcome;
    }
}
