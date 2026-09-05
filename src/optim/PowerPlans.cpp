#include "PowerPlans.h"
#include "../WinStr.h"

#include <windows.h>
#include <powrprof.h>
#include <cstring>
#include <cstdio>
#include <vector>

namespace
{
    // The three plans Windows ships. Anything else is an OEM or user plan and
    // is listed without a claim attached to it.
    struct KnownPlan
    {
        const char* guid;
        const char* note;
    };

    const KnownPlan kKnownPlans[] = {
        { "8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c",
          "Procesor drží vyššie takty a jadrá menej zaspávajú. Vyššia spotreba a teplota." },
        { "381b4222-f694-41f0-9685-ff5bb260df2e",
          "Predvolený plán Windows. Takty sa prispôsobujú záťaži." },
        { "a1841308-3541-4fab-bc81-f71556f20b4a",
          "Obmedzuje výkon procesora kvôli výdrži batérie." },
        { "e9a42b02-d5df-448d-aa00-03f14749eb61",
          "Plán pre pracovné stanice. Nie je dostupný na každom zariadení." },
    };

    // Formatted by hand rather than through StringFromGUID2 so this file
    // needs no COM dependency. Lowercase, no braces — the same shape the
    // power-plan GUIDs are published in.
    std::string GuidToString(const GUID& guid)
    {
        char buffer[40];
        std::snprintf(buffer, sizeof(buffer),
            "%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
            (unsigned long)guid.Data1, guid.Data2, guid.Data3,
            guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
            guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
        return buffer;
    }

    bool StringToGuid(const std::string& text, GUID* out)
    {
        unsigned long data1 = 0;
        unsigned int data2 = 0, data3 = 0, d[8] = { 0 };
        int fields = std::sscanf(text.c_str(),
            "%8lx-%4x-%4x-%2x%2x-%2x%2x%2x%2x%2x%2x",
            &data1, &data2, &data3, &d[0], &d[1], &d[2], &d[3], &d[4], &d[5], &d[6], &d[7]);
        if (fields != 11)
        {
            return false;
        }
        out->Data1 = (unsigned long)data1;
        out->Data2 = (unsigned short)data2;
        out->Data3 = (unsigned short)data3;
        for (int i = 0; i < 8; i++) out->Data4[i] = (unsigned char)d[i];
        return true;
    }

    std::string FriendlyName(const GUID& scheme)
    {
        DWORD bytes = 0;
        if (PowerReadFriendlyName(nullptr, &scheme, nullptr, nullptr, nullptr, &bytes) != ERROR_SUCCESS)
        {
            return "";
        }
        std::vector<UCHAR> buffer(bytes + sizeof(wchar_t), 0);
        if (PowerReadFriendlyName(nullptr, &scheme, nullptr, nullptr, buffer.data(), &bytes) != ERROR_SUCCESS)
        {
            return "";
        }
        return WinStr::ToUtf8(reinterpret_cast<wchar_t*>(buffer.data()));
    }

    std::string Description(const GUID& scheme)
    {
        DWORD bytes = 0;
        if (PowerReadDescription(nullptr, &scheme, nullptr, nullptr, nullptr, &bytes) != ERROR_SUCCESS)
        {
            return "";
        }
        std::vector<UCHAR> buffer(bytes + sizeof(wchar_t), 0);
        if (PowerReadDescription(nullptr, &scheme, nullptr, nullptr, buffer.data(), &bytes) != ERROR_SUCCESS)
        {
            return "";
        }
        return WinStr::ToUtf8(reinterpret_cast<wchar_t*>(buffer.data()));
    }

    std::string ActiveSchemeGuid()
    {
        GUID* active = nullptr;
        if (PowerGetActiveScheme(nullptr, &active) != ERROR_SUCCESS || active == nullptr)
        {
            return "";
        }
        std::string text = GuidToString(*active);
        LocalFree(active);
        return text;
    }

    // The journal stores registry snapshots, so the original plan's GUID goes
    // in as a REG_SZ payload. It is the same "capture once, restore exactly"
    // contract as every other setting, using the same file.
    optim::RegSnapshot SnapshotFromText(const std::string& text)
    {
        std::wstring wide = WinStr::ToWide(text);
        optim::RegSnapshot snapshot;
        snapshot.existed = true;
        snapshot.type = REG_SZ;
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(wide.c_str());
        snapshot.raw.assign(bytes, bytes + (wide.size() + 1) * sizeof(wchar_t));
        return snapshot;
    }
}

namespace optim
{
    const char* PowerPlanManager::kJournalId = "power.active_plan";

    PowerPlanManager::PowerPlanManager(BackupStore* backups) : m_backups(backups)
    {
    }

    std::vector<PowerPlan> PowerPlanManager::Enumerate() const
    {
        std::vector<PowerPlan> plans;
        std::string active = ActiveSchemeGuid();

        for (ULONG index = 0;; index++)
        {
            GUID scheme{};
            DWORD size = sizeof(scheme);
            DWORD status = PowerEnumerate(nullptr, nullptr, nullptr, ACCESS_SCHEME,
                                          index, reinterpret_cast<UCHAR*>(&scheme), &size);
            if (status != ERROR_SUCCESS) break;

            PowerPlan plan;
            plan.guid = GuidToString(scheme);
            plan.name = FriendlyName(scheme);
            plan.description = Description(scheme);
            plan.active = !plan.guid.empty() && plan.guid == active;

            for (const KnownPlan& known : kKnownPlans)
            {
                if (plan.guid == known.guid) plan.note = known.note;
            }

            if (plan.name.empty()) plan.name = plan.guid;
            plans.push_back(plan);
        }

        return plans;
    }

    Error PowerPlanManager::Activate(const std::string& guid)
    {
        GUID scheme{};
        if (!StringToGuid(guid, &scheme))
        {
            return Error::Make(Error::Code::NotSupported, "Neplatný identifikátor plánu.");
        }

        std::string current = ActiveSchemeGuid();
        if (current.empty())
        {
            return Error::Make(Error::Code::ReadFailed,
                "Aktuálny plán napájania sa nepodarilo prečítať, nič sa nezmenilo.");
        }
        if (current == guid)
        {
            return Error::Ok(); // already there; nothing to record or change
        }

        // Capture the plan that was active before Nasaki touched anything.
        m_backups->CaptureIfAbsent(kJournalId, "scheme", SnapshotFromText(current),
                                   "Pôvodný plán napájania");
        m_backups->Save();

        DWORD status = PowerSetActiveScheme(nullptr, &scheme);
        if (status != ERROR_SUCCESS)
        {
            return Error::Make(
                status == ERROR_ACCESS_DENIED ? Error::Code::AccessDenied : Error::Code::WriteFailed,
                "Plán napájania sa nepodarilo prepnúť.", (long)status);
        }

        if (ActiveSchemeGuid() != guid)
        {
            return Error::Make(Error::Code::VerifyMismatch,
                "Prepnutie prešlo, ale aktívny je stále iný plán — pravdepodobne ho riadi politika.");
        }

        m_backups->RecordHistory(kJournalId, "power-activate", true, "Plán napájania prepnutý.");
        m_backups->Save();
        return Error::Ok();
    }

    Error PowerPlanManager::RestoreOriginal()
    {
        std::optional<RegSnapshot> original = m_backups->Find(kJournalId, "scheme");
        if (!original.has_value())
        {
            return Error::Make(Error::Code::NoBackup, "Nemáme zálohu — plán sme nemenili.");
        }

        std::string guid = WinStr::ToUtf8(reg::SnapshotAsString(*original));
        GUID scheme{};
        if (!StringToGuid(guid, &scheme))
        {
            return Error::Make(Error::Code::NotSupported, "Uložený plán sa nedá prečítať.");
        }

        DWORD status = PowerSetActiveScheme(nullptr, &scheme);
        if (status != ERROR_SUCCESS)
        {
            return Error::Make(
                status == ERROR_ACCESS_DENIED ? Error::Code::AccessDenied : Error::Code::WriteFailed,
                "Pôvodný plán sa nepodarilo obnoviť.", (long)status);
        }

        if (ActiveSchemeGuid() != guid)
        {
            return Error::Make(Error::Code::VerifyMismatch,
                "Obnovenie prešlo, ale aktívny je iný plán.");
        }

        m_backups->ForgetValue(kJournalId, "scheme");
        m_backups->RecordHistory(kJournalId, "power-restore", true, "Pôvodný plán napájania obnovený.");
        m_backups->Save();
        return Error::Ok();
    }

    bool PowerPlanManager::HasOriginal() const
    {
        return m_backups->Find(kJournalId, "scheme").has_value();
    }

    std::string PowerPlanManager::OriginalPlanName() const
    {
        std::optional<RegSnapshot> original = m_backups->Find(kJournalId, "scheme");
        if (!original.has_value()) return "";

        GUID scheme{};
        std::string guid = WinStr::ToUtf8(reg::SnapshotAsString(*original));
        if (!StringToGuid(guid, &scheme)) return guid;

        std::string name = FriendlyName(scheme);
        return name.empty() ? guid : name;
    }

    std::string PowerPlanManager::GuidanceFor(const SystemInventory& inventory)
    {
        if (!inventory.isLaptop)
        {
            return "Stolný počítač: vyšší plán stojí len elektrinu, riziko prehrievania je nízke.";
        }

        if (inventory.onBattery)
        {
            std::string text =
                "Notebook beží na batérii. Vyšší výkonový plán teraz skráti výdrž a zvýši teplotu "
                "— zapni ho radšej až po pripojení do siete.";
            if (inventory.batteryPercent.has_value())
            {
                text += " Stav batérie: " + std::to_string(*inventory.batteryPercent) + " %.";
            }
            return text;
        }

        return "Notebook v sieti: vyšší plán je v poriadku, ale ak sa zariadenie prehrieva a "
               "znižuje takty, vráť sa na Vyvážený.";
    }
}
