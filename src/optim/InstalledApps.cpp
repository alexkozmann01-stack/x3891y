#include "InstalledApps.h"
#include "../WinStr.h"

#include <windows.h>
#include <shellapi.h>
#include <algorithm>

namespace
{
    std::wstring ReadString(HKEY key, const wchar_t* name)
    {
        wchar_t buffer[1024];
        DWORD size = sizeof(buffer);
        DWORD type = 0;
        if (RegQueryValueExW(key, name, nullptr, &type,
                reinterpret_cast<BYTE*>(buffer), &size) != ERROR_SUCCESS)
        {
            return L"";
        }
        if (type != REG_SZ && type != REG_EXPAND_SZ) return L"";

        std::wstring value(buffer, size / sizeof(wchar_t));
        while (!value.empty() && value.back() == L'\0') value.pop_back();

        if (type == REG_EXPAND_SZ)
        {
            wchar_t expanded[2048];
            DWORD written = ExpandEnvironmentStringsW(value.c_str(), expanded, 2048);
            if (written > 0 && written <= 2048) return expanded;
        }
        return value;
    }

    DWORD ReadDword(HKEY key, const wchar_t* name, DWORD fallback)
    {
        DWORD value = fallback;
        DWORD size = sizeof(value);
        DWORD type = 0;
        if (RegQueryValueExW(key, name, nullptr, &type,
                reinterpret_cast<BYTE*>(&value), &size) != ERROR_SUCCESS || type != REG_DWORD)
        {
            return fallback;
        }
        return value;
    }

    bool HasValue(HKEY key, const wchar_t* name)
    {
        DWORD size = 0;
        return RegQueryValueExW(key, name, nullptr, nullptr, nullptr, &size) == ERROR_SUCCESS;
    }

    // Programs other programs depend on. Removing one of these to "free
    // space" is how a machine ends up with applications that no longer
    // start, so they are listed but never offered an uninstall button.
    struct ProtectedPattern
    {
        const wchar_t* needle; // lowercase substring of the display name
        const char* reason;
    };

    const ProtectedPattern kProtected[] = {
        { L"visual c++",       "Runtime knižnica, ktorú potrebujú iné programy a hry." },
        { L"visual studio",    "Vývojárske nástroje — odinštaluj cez ich vlastný inštalátor." },
        { L".net",             "Runtime .NET. Závisia od neho ďalšie aplikácie." },
        { L"webview2",         "Komponent, cez ktorý vykresľujú obsah iné aplikácie." },
        { L"microsoft edge",   "Súčasť systému; z Edge závisí WebView2 aj časti Windows." },
        { L"directx",          "Grafické runtime knižnice pre hry." },
        { L"windows sdk",      "Vývojárske komponenty." },
        { L"driver",           "Ovládač zariadenia — patrí do Správcu zariadení." },
        { L"nvidia",           "Softvér ku grafickej karte. Odinštaluj cez nástroj výrobcu." },
        { L"amd software",     "Softvér ku grafickej karte. Odinštaluj cez nástroj výrobcu." },
        { L"intel",            "Ovládače a nástroje čipovej sady." },
        { L"realtek",          "Ovládače zvuku alebo siete." },
        { L"microsoft store",  "Infraštruktúra Microsoft Store." },
        { L"app installer",    "Infraštruktúra Microsoft Store." },
    };

    bool IsProtected(const std::wstring& nameLower, std::string* reason)
    {
        for (const ProtectedPattern& pattern : kProtected)
        {
            if (nameLower.find(pattern.needle) != std::wstring::npos)
            {
                if (reason) *reason = pattern.reason;
                return true;
            }
        }
        return false;
    }

    void ScanUninstallKey(HKEY root, const wchar_t* subKey, REGSAM view, bool perMachine,
                          std::vector<optim::InstalledApp>& out)
    {
        HKEY key = nullptr;
        if (RegOpenKeyExW(root, subKey, 0, KEY_READ | view, &key) != ERROR_SUCCESS)
        {
            return;
        }

        for (DWORD index = 0;; index++)
        {
            wchar_t name[512];
            DWORD nameLength = 512;
            if (RegEnumKeyExW(key, index, name, &nameLength, nullptr, nullptr, nullptr, nullptr)
                != ERROR_SUCCESS)
            {
                break;
            }

            HKEY entry = nullptr;
            if (RegOpenKeyExW(key, name, 0, KEY_READ | view, &entry) != ERROR_SUCCESS)
            {
                continue;
            }

            std::wstring displayName = ReadString(entry, L"DisplayName");

            // Windows' own Programs list applies the same three filters:
            // no display name, marked as a system component, or a patch
            // hanging off a parent product.
            bool systemComponent = ReadDword(entry, L"SystemComponent", 0) == 1;
            bool isUpdate = HasValue(entry, L"ParentKeyName") || HasValue(entry, L"ParentDisplayName");

            if (displayName.empty() || systemComponent || isUpdate)
            {
                RegCloseKey(entry);
                continue;
            }

            optim::InstalledApp app;
            app.id = WinStr::ToUtf8(name);
            app.name = WinStr::ToUtf8(displayName);
            app.version = WinStr::ToUtf8(ReadString(entry, L"DisplayVersion"));
            app.publisher = WinStr::ToUtf8(ReadString(entry, L"Publisher"));
            app.installDate = WinStr::ToUtf8(ReadString(entry, L"InstallDate"));
            app.perMachine = perMachine;

            // EstimatedSize is in KB and is whatever the installer chose to
            // write, so it is shown as an estimate and often missing.
            DWORD sizeKb = ReadDword(entry, L"EstimatedSize", 0);
            app.estimatedBytes = (uint64_t)sizeKb * 1024ull;

            std::wstring quiet = ReadString(entry, L"QuietUninstallString");
            std::wstring standard = ReadString(entry, L"UninstallString");
            // The interactive uninstaller is preferred: a quiet one would
            // remove the program with no chance to cancel.
            app.uninstallCommand = WinStr::ToUtf8(standard.empty() ? quiet : standard);

            std::string reason;
            if (IsProtected(WinStr::ToLower(displayName), &reason))
            {
                app.protectedComponent = true;
                app.protectionReason = reason;
                app.uninstallable = false;
            }
            else
            {
                app.uninstallable = !app.uninstallCommand.empty();
            }

            out.push_back(app);
            RegCloseKey(entry);
        }

        RegCloseKey(key);
    }
}

namespace optim
{
    std::vector<InstalledApp> AppInventory::Scan() const
    {
        const wchar_t* uninstall = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall";

        std::vector<InstalledApp> apps;
        ScanUninstallKey(HKEY_LOCAL_MACHINE, uninstall, KEY_WOW64_64KEY, true, apps);
        ScanUninstallKey(HKEY_LOCAL_MACHINE, uninstall, KEY_WOW64_32KEY, true, apps);
        ScanUninstallKey(HKEY_CURRENT_USER, uninstall, 0, false, apps);

        // The 32- and 64-bit views overlap on some entries; keep one of each.
        std::sort(apps.begin(), apps.end(), [](const InstalledApp& a, const InstalledApp& b) {
            if (a.name != b.name) return a.name < b.name;
            return a.id < b.id;
        });
        apps.erase(std::unique(apps.begin(), apps.end(),
            [](const InstalledApp& a, const InstalledApp& b) {
                return a.name == b.name && a.version == b.version;
            }), apps.end());

        std::sort(apps.begin(), apps.end(), [](const InstalledApp& a, const InstalledApp& b) {
            // Biggest first: that is the question the page answers.
            if (a.estimatedBytes != b.estimatedBytes) return a.estimatedBytes > b.estimatedBytes;
            return a.name < b.name;
        });
        return apps;
    }

    Error AppInventory::LaunchUninstaller(const InstalledApp& app) const
    {
        if (app.protectedComponent)
        {
            return Error::Make(Error::Code::NotSupported,
                "Toto je systémová alebo runtime súčasť — Nasaki ju odinštalovať neponúka.");
        }
        if (app.uninstallCommand.empty())
        {
            return Error::Make(Error::Code::NotSupported,
                "Program neuviedol príkaz na odinštalovanie.");
        }

        // Handed to the shell as-is so the publisher's own uninstaller runs,
        // interactively. Nasaki deletes nothing itself.
        std::wstring command = WinStr::ToWide(app.uninstallCommand);
        HINSTANCE result = ShellExecuteW(nullptr, nullptr, L"cmd.exe",
            (L"/c start \"\" " + command).c_str(), nullptr, SW_SHOWNORMAL);

        if ((INT_PTR)result <= 32)
        {
            return Error::Make(Error::Code::WriteFailed,
                "Odinštalátor sa nepodarilo spustiť.", (long)(INT_PTR)result);
        }
        return Error::Ok();
    }

    void AppInventory::OpenWindowsAppsSettings()
    {
        ShellExecuteW(nullptr, L"open", L"ms-settings:appsfeatures", nullptr, nullptr, SW_SHOWNORMAL);
    }
}
