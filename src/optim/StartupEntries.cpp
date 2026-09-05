#include "StartupEntries.h"
#include "../WinStr.h"

#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <algorithm>
#include <string>

namespace
{
    // Not const: tests point this at a scratch key. Only ever written once,
    // before any scanning, from the test's main thread.
    std::wstring g_runSubKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

    // Reads every value under a Run key. Returns name -> command pairs; a
    // key that doesn't exist is an empty list, not an error.
    std::vector<std::pair<std::wstring, std::wstring>> ReadRunValues(HKEY root)
    {
        std::vector<std::pair<std::wstring, std::wstring>> values;

        HKEY key = nullptr;
        if (RegOpenKeyExW(root, g_runSubKey.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS)
        {
            return values;
        }

        for (DWORD index = 0;; index++)
        {
            wchar_t name[16383];               // documented maximum value-name length
            DWORD nameLength = 16383;
            BYTE data[8192];
            DWORD dataSize = sizeof(data);
            DWORD type = 0;

            LSTATUS status = RegEnumValueW(key, index, name, &nameLength, nullptr,
                                           &type, data, &dataSize);
            if (status == ERROR_NO_MORE_ITEMS) break;
            if (status != ERROR_SUCCESS) break;
            if (type != REG_SZ && type != REG_EXPAND_SZ) continue;

            std::wstring command(reinterpret_cast<wchar_t*>(data),
                                 dataSize / sizeof(wchar_t));
            while (!command.empty() && command.back() == L'\0') command.pop_back();

            values.emplace_back(std::wstring(name, nameLength), command);
        }

        RegCloseKey(key);
        return values;
    }

    // Pulls the executable out of a command line. Handles the two shapes
    // that actually occur: a quoted path followed by arguments, and a bare
    // path where the first space may or may not be part of the path.
    std::wstring ExecutableFromCommand(const std::wstring& command)
    {
        if (command.empty()) return L"";

        if (command.front() == L'"')
        {
            size_t end = command.find(L'"', 1);
            if (end != std::wstring::npos) return command.substr(1, end - 1);
            return command.substr(1);
        }

        // Unquoted: prefer a prefix that names a file that exists, so
        // "C:\Program Files\App\app.exe -x" isn't cut at the first space.
        size_t space = command.find(L' ');
        while (space != std::wstring::npos)
        {
            std::wstring candidate = command.substr(0, space);
            if (GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES)
            {
                return candidate;
            }
            space = command.find(L' ', space + 1);
        }
        return command;
    }

    std::string FileNameOf(const std::wstring& path)
    {
        size_t slash = path.find_last_of(L"\\/");
        std::wstring base = (slash == std::wstring::npos) ? path : path.substr(slash + 1);
        return WinStr::ToUtf8(base);
    }

    // Shortcuts in a Startup folder. Read-only: deleting or moving a user's
    // shortcut file is not something we can undo as reliably as a registry
    // value, so these are listed and linked out to Windows instead.
    void ScanStartupFolder(int folderId, const char* label,
                           std::vector<optim::StartupEntry>& out)
    {
        wchar_t folder[MAX_PATH];
        if (FAILED(SHGetFolderPathW(nullptr, folderId, nullptr, SHGFP_TYPE_CURRENT, folder)))
        {
            return;
        }

        std::wstring pattern = std::wstring(folder) + L"\\*";
        WIN32_FIND_DATAW find{};
        HANDLE search = FindFirstFileW(pattern.c_str(), &find);
        if (search == INVALID_HANDLE_VALUE) return;

        do
        {
            if (find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;

            std::wstring name = find.cFileName;
            if (name == L"desktop.ini") continue;

            optim::StartupEntry entry;
            entry.id = std::string("folder:") + label + ":" + WinStr::ToUtf8(name);
            entry.name = FileNameOf(name);
            entry.command = WinStr::ToUtf8(std::wstring(folder) + L"\\" + name);
            entry.executable = entry.command;
            entry.location = label;
            entry.removable = false;
            out.push_back(entry);
        } while (FindNextFileW(search, &find));

        FindClose(search);
    }
}

namespace optim
{
    const char* StartupManager::kJournalId = "startup.run_entries";

    StartupManager::StartupManager(BackupStore* backups) : m_backups(backups)
    {
    }

    std::vector<StartupEntry> StartupManager::Scan() const
    {
        std::vector<StartupEntry> entries;

        for (const auto& [name, command] : ReadRunValues(HKEY_CURRENT_USER))
        {
            StartupEntry entry;
            entry.id = "hkcu-run:" + WinStr::ToUtf8(name);
            entry.name = WinStr::ToUtf8(name);
            entry.command = WinStr::ToUtf8(command);
            entry.executable = FileNameOf(ExecutableFromCommand(command));
            entry.location = "Tento používateľ (Run)";
            entry.removable = true;
            entries.push_back(entry);
        }

        for (const auto& [name, command] : ReadRunValues(HKEY_LOCAL_MACHINE))
        {
            StartupEntry entry;
            entry.id = "hklm-run:" + WinStr::ToUtf8(name);
            entry.name = WinStr::ToUtf8(name);
            entry.command = WinStr::ToUtf8(command);
            entry.executable = FileNameOf(ExecutableFromCommand(command));
            entry.location = "Všetci používatelia (Run)";
            entry.removable = false; // HKLM needs elevation we deliberately don't hold
            entry.needsAdmin = true;
            entries.push_back(entry);
        }

        ScanStartupFolder(CSIDL_STARTUP, "Priečinok Po spustení", entries);
        ScanStartupFolder(CSIDL_COMMON_STARTUP, "Po spustení (všetci)", entries);

        // Anything Nasaki removed is still listed, marked as removed, so the
        // user can see what they turned off and put it back.
        for (const BackupEntry& backup : m_backups->Entries())
        {
            if (backup.optimizationId != kJournalId) continue;

            std::string id = "hkcu-run:" + backup.valueKey;
            bool alreadyListed = std::any_of(entries.begin(), entries.end(),
                [&](const StartupEntry& e) { return e.id == id; });
            if (alreadyListed) continue;

            StartupEntry entry;
            entry.id = id;
            entry.name = backup.valueKey;
            entry.command = WinStr::ToUtf8(reg::SnapshotAsString(backup.original));
            entry.executable = FileNameOf(ExecutableFromCommand(
                reg::SnapshotAsString(backup.original)));
            entry.location = "Tento používateľ (Run)";
            entry.removable = true;
            entry.removed = true;
            entries.push_back(entry);
        }

        std::sort(entries.begin(), entries.end(), [](const StartupEntry& a, const StartupEntry& b) {
            if (a.removable != b.removable) return a.removable; // actionable first
            return a.name < b.name;
        });
        return entries;
    }

    Error StartupManager::Remove(const std::string& id)
    {
        const std::string prefix = "hkcu-run:";
        if (id.rfind(prefix, 0) != 0)
        {
            return Error::Make(Error::Code::NotSupported,
                "Túto položku Nasaki nemení — spravuje sa v nastaveniach Windows.");
        }

        std::string valueName = id.substr(prefix.size());
        RegPath path{ HKEY_CURRENT_USER, g_runSubKey, WinStr::ToWide(valueName) };

        long systemError = 0;
        RegSnapshot before = reg::Read(path, &systemError);
        if (systemError != 0)
        {
            return Error::Make(Error::Code::ReadFailed,
                "Položku sa nepodarilo prečítať, nič sa nezmenilo.", systemError);
        }
        if (!before.existed)
        {
            return Error::Make(Error::Code::ReadFailed, "Položka po spustení už neexistuje.");
        }

        // Capture the exact original first — name, type and bytes — so
        // re-enabling restores the command line unchanged.
        m_backups->CaptureIfAbsent(kJournalId, valueName, before, "Po spustení: " + valueName);
        m_backups->Save();

        if (!reg::DeleteValue(path, &systemError))
        {
            return Error::Make(
                systemError == ERROR_ACCESS_DENIED ? Error::Code::AccessDenied : Error::Code::WriteFailed,
                "Položku sa nepodarilo odstrániť.", systemError);
        }

        // Verify: a delete that reported success but left the value behind
        // must not be shown as done.
        if (reg::Read(path).existed)
        {
            return Error::Make(Error::Code::VerifyMismatch,
                "Odstránenie prešlo, ale položka je stále v registri.");
        }

        m_backups->RecordHistory(kJournalId, "startup-remove", true,
            "Odstránené zo spúšťania: " + valueName);
        m_backups->Save();
        return Error::Ok();
    }

    Error StartupManager::Restore(const std::string& id)
    {
        const std::string prefix = "hkcu-run:";
        if (id.rfind(prefix, 0) != 0)
        {
            return Error::Make(Error::Code::NotSupported, "Túto položku Nasaki nespravuje.");
        }

        std::string valueName = id.substr(prefix.size());
        std::optional<RegSnapshot> original = m_backups->Find(kJournalId, valueName);
        if (!original.has_value())
        {
            return Error::Make(Error::Code::NoBackup,
                "Nemáme zálohu — túto položku Nasaki neodstránil.");
        }

        RegPath path{ HKEY_CURRENT_USER, g_runSubKey, WinStr::ToWide(valueName) };
        long systemError = 0;
        if (!reg::WriteSnapshot(path, *original, &systemError))
        {
            return Error::Make(
                systemError == ERROR_ACCESS_DENIED ? Error::Code::AccessDenied : Error::Code::WriteFailed,
                "Položku sa nepodarilo vrátiť späť.", systemError);
        }

        RegSnapshot verify = reg::Read(path);
        if (!(verify == *original))
        {
            return Error::Make(Error::Code::VerifyMismatch,
                "Zápis prešiel, ale kontrola pôvodnú položku nenašla.");
        }

        m_backups->ForgetValue(kJournalId, valueName);
        m_backups->RecordHistory(kJournalId, "startup-restore", true,
            "Vrátené do spúšťania: " + valueName);
        m_backups->Save();
        return Error::Ok();
    }

    void StartupManager::SetRunSubKeyForTesting(const std::wstring& subKey)
    {
        g_runSubKey = subKey;
    }

    void StartupManager::OpenWindowsStartupSettings()
    {
        ShellExecuteW(nullptr, L"open", L"ms-settings:startupapps", nullptr, nullptr, SW_SHOWNORMAL);
    }
}
