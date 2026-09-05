#include "RegistryOptimization.h"

#include <windows.h>

namespace optim
{
    // ---------------------------------------------------------------- registry

    RegistryOptimization::RegistryOptimization(Info info, std::vector<Target> targets, BackupStore* backups)
        : m_info(std::move(info)), m_targets(std::move(targets)), m_backups(backups)
    {
    }

    bool RegistryOptimization::Supported() const
    {
        // The parent key existing is what tells us the feature exists on this
        // build; the value itself is often absent until first changed.
        for (const Target& target : m_targets)
        {
            if (reg::KeyExists(target.path.root, target.path.subKey))
            {
                return true;
            }
        }
        return false;
    }

    Status RegistryOptimization::Read() const
    {
        Status status;
        if (!Supported())
        {
            status.state = State::Unsupported;
            status.detail = "Táto verzia Windows kľúč nemá.";
            return status;
        }

        bool allApplied = true;
        std::string detail;
        for (const Target& target : m_targets)
        {
            long systemError = 0;
            RegSnapshot snapshot = reg::Read(target.path, &systemError);
            if (systemError != 0)
            {
                status.state = State::Failed;
                status.lastError = Error::Make(Error::Code::ReadFailed,
                    "Hodnotu sa nepodarilo prečítať.", systemError);
                return status;
            }

            bool matches = snapshot.existed && snapshot.type == REG_DWORD &&
                           snapshot.dword == target.appliedValue;
            allApplied = allApplied && matches;

            if (!detail.empty()) detail += "  •  ";
            detail += target.key + ": " +
                (snapshot.existed ? std::to_string(snapshot.dword) : std::string("nenastavené"));
        }

        status.state = allApplied ? State::Applied : State::NotApplied;
        status.detail = detail;
        return status;
    }

    Error RegistryOptimization::Apply()
    {
        if (!Supported())
        {
            return Error::Make(Error::Code::NotSupported, "Nastavenie nie je na tomto systéme dostupné.");
        }

        // Capture originals for everything first. If a later write fails we
        // still hold a complete, accurate backup for the rollback.
        for (const Target& target : m_targets)
        {
            long systemError = 0;
            RegSnapshot before = reg::Read(target.path, &systemError);
            if (systemError != 0)
            {
                return Error::Make(Error::Code::ReadFailed,
                    "Pôvodnú hodnotu sa nepodarilo prečítať, nič sa nezmenilo.", systemError);
            }
            m_backups->CaptureIfAbsent(m_info.id, target.key, before, m_info.title);
        }
        m_backups->Save();

        for (const Target& target : m_targets)
        {
            long systemError = 0;
            if (!reg::WriteDword(target.path, target.appliedValue, &systemError))
            {
                return Error::Make(
                    systemError == ERROR_ACCESS_DENIED ? Error::Code::AccessDenied : Error::Code::WriteFailed,
                    "Zápis do registry zlyhal.", systemError);
            }
        }

        // Verify by reading back — a successful RegSetValueEx is not proof
        // the effective value is what we asked for (policy can override it).
        Status after = Read();
        if (after.state != State::Applied)
        {
            return Error::Make(Error::Code::VerifyMismatch,
                "Zápis prešiel, ale kontrola ukázala inú hodnotu — možno ju prepisuje politika.");
        }
        return Error::Ok();
    }

    Error RegistryOptimization::Restore()
    {
        bool restoredAny = false;
        for (const Target& target : m_targets)
        {
            std::optional<RegSnapshot> original = m_backups->Find(m_info.id, target.key);
            if (!original.has_value())
            {
                continue; // nothing of ours to undo for this value
            }

            long systemError = 0;
            if (!reg::WriteSnapshot(target.path, *original, &systemError))
            {
                return Error::Make(
                    systemError == ERROR_ACCESS_DENIED ? Error::Code::AccessDenied : Error::Code::WriteFailed,
                    "Obnovenie pôvodnej hodnoty zlyhalo.", systemError);
            }
            restoredAny = true;
        }

        if (!restoredAny)
        {
            return Error::Make(Error::Code::NoBackup, "Nie je čo obnoviť — hodnotu sme nemenili.");
        }

        // Confirm the live values now match what we put back.
        for (const Target& target : m_targets)
        {
            std::optional<RegSnapshot> original = m_backups->Find(m_info.id, target.key);
            if (!original.has_value()) continue;

            long systemError = 0;
            RegSnapshot now = reg::Read(target.path, &systemError);
            if (!(now == *original))
            {
                return Error::Make(Error::Code::VerifyMismatch,
                    "Obnovenie prebehlo, ale kontrola nesedí.");
            }
        }

        m_backups->Forget(m_info.id);
        m_backups->Save();
        return Error::Ok();
    }

    // -------------------------------------------------------------------- SPI

    SpiBoolOptimization::SpiBoolOptimization(Info info, unsigned int getAction, unsigned int setAction,
                                             bool appliedValue, BackupStore* backups)
        : m_info(std::move(info)), m_getAction(getAction), m_setAction(setAction),
          m_appliedValue(appliedValue), m_backups(backups)
    {
    }

    std::optional<bool> SpiBoolOptimization::ReadRaw(long* systemError) const
    {
        BOOL value = FALSE;
        if (!SystemParametersInfoW(m_getAction, 0, &value, 0))
        {
            if (systemError) *systemError = (long)GetLastError();
            return std::nullopt;
        }
        return value != FALSE;
    }

    Status SpiBoolOptimization::Read() const
    {
        Status status;
        long systemError = 0;
        std::optional<bool> current = ReadRaw(&systemError);
        if (!current.has_value())
        {
            status.state = State::Unsupported;
            status.lastError = Error::Make(Error::Code::NotSupported,
                "Systém toto nastavenie nehlási.", systemError);
            return status;
        }

        status.state = (*current == m_appliedValue) ? State::Applied : State::NotApplied;
        status.detail = std::string("aktuálne: ") + (*current ? "zapnuté" : "vypnuté");
        return status;
    }

    Error SpiBoolOptimization::Write(bool value)
    {
        // SET takes the BOOL by value in pvParam, unlike GET which takes a
        // pointer. SPIF_UPDATEINIFILE persists it; SPIF_SENDCHANGE tells
        // running apps to pick it up without a sign-out.
        if (!SystemParametersInfoW(m_setAction, 0, (PVOID)(INT_PTR)(value ? TRUE : FALSE),
                SPIF_UPDATEINIFILE | SPIF_SENDCHANGE))
        {
            return Error::Make(Error::Code::WriteFailed, "Systém zmenu odmietol.", (long)GetLastError());
        }
        return Error::Ok();
    }

    Error SpiBoolOptimization::Apply()
    {
        long systemError = 0;
        std::optional<bool> before = ReadRaw(&systemError);
        if (!before.has_value())
        {
            return Error::Make(Error::Code::NotSupported, "Nastavenie nie je dostupné.", systemError);
        }

        // Reuse the registry snapshot shape so one journal covers both kinds:
        // a "existed, REG_DWORD, 0/1" record round-trips a bool exactly.
        RegSnapshot snapshot;
        snapshot.existed = true;
        snapshot.type = REG_DWORD;
        snapshot.dword = *before ? 1u : 0u;
        m_backups->CaptureIfAbsent(m_info.id, "spi", snapshot, m_info.title);
        m_backups->Save();

        Error error = Write(m_appliedValue);
        if (!error.ok()) return error;

        std::optional<bool> after = ReadRaw(nullptr);
        if (!after.has_value() || *after != m_appliedValue)
        {
            return Error::Make(Error::Code::VerifyMismatch,
                "Zmena prešla, ale systém stále hlási pôvodnú hodnotu.");
        }
        return Error::Ok();
    }

    Error SpiBoolOptimization::Restore()
    {
        std::optional<RegSnapshot> original = m_backups->Find(m_info.id, "spi");
        if (!original.has_value())
        {
            return Error::Make(Error::Code::NoBackup, "Nie je čo obnoviť — nastavenie sme nemenili.");
        }

        bool value = original->dword != 0;
        Error error = Write(value);
        if (!error.ok()) return error;

        std::optional<bool> after = ReadRaw(nullptr);
        if (!after.has_value() || *after != value)
        {
            return Error::Make(Error::Code::VerifyMismatch, "Obnovenie prebehlo, ale kontrola nesedí.");
        }

        m_backups->Forget(m_info.id);
        m_backups->Save();
        return Error::Ok();
    }
}
