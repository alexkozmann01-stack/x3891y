#include "ProcessBoost.h"
#include "WinStr.h"

#include <windows.h>
#include <tlhelp32.h>
#include <vector>
#include <cwctype>
#include <functional>

namespace
{
    struct Adjusted
    {
        DWORD pid;
        HANDLE handle;
        DWORD originalPriority;
    };

    // Module-local rather than a class member: ProcessBoost is used from a
    // single App instance in practice, and keeping the OS handles here
    // (rather than threading them through App's header) keeps windows.h
    // internals out of App.h.
    std::vector<Adjusted> g_adjusted;

    std::wstring ToLowerCopy(const std::wstring& s)
    {
        return WinStr::ToLower(s);
    }

    // Full executable path of a pid, or empty if it can't be read.
    std::wstring ProcessImagePath(DWORD pid)
    {
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!h)
        {
            return L"";
        }
        wchar_t path[MAX_PATH];
        DWORD size = MAX_PATH;
        std::wstring result;
        if (QueryFullProcessImageNameW(h, 0, path, &size))
        {
            result.assign(path, size);
        }
        CloseHandle(h);
        return result;
    }

    // Snapshots running processes once and calls `fn(entry)` for each.
    void ForEachProcess(const std::function<void(const PROCESSENTRY32W&)>& fn)
    {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE)
        {
            return;
        }
        PROCESSENTRY32W entry;
        entry.dwSize = sizeof(entry);
        if (Process32FirstW(snap, &entry))
        {
            do
            {
                fn(entry);
            } while (Process32NextW(snap, &entry));
        }
        CloseHandle(snap);
    }

    // Deliberately conservative: well-known consumer apps only, never
    // anything system-owned. Extend this list rather than widening the
    // matching logic if more apps need covering.
    const wchar_t* kBackgroundAllowlist[] = {
        L"chrome.exe", L"msedge.exe", L"firefox.exe", L"opera.exe", L"brave.exe",
        L"discord.exe", L"discordptb.exe", L"discordcanary.exe",
        L"spotify.exe", L"slack.exe", L"teams.exe", L"skype.exe",
        L"steamwebhelper.exe", L"epicgameslauncher.exe", L"origin.exe",
        L"onedrive.exe", L"dropbox.exe",
    };

    bool IsAllowlistedBackground(const std::wstring& exeNameLower)
    {
        for (const wchar_t* name : kBackgroundAllowlist)
        {
            if (exeNameLower == name)
            {
                return true;
            }
        }
        return false;
    }

    struct KnownGame
    {
        const wchar_t* exeName; // lowercase
        const char* displayName;
    };

    // A static starter list of popular executable names -> display names.
    // Good enough to make session-start instant for most players without
    // needing the switch-to-game countdown; nowhere near exhaustive (that's
    // realistically a server-fetched, regularly updated list for a v2 —
    // there's no way to keep a hardcoded client-side table current with
    // every game that ships).
    const KnownGame kKnownGames[] = {
        { L"cs2.exe", "Counter-Strike 2" },
        { L"csgo.exe", "CS:GO" },
        { L"valorant-win64-shipping.exe", "Valorant" },
        { L"leagueclientux.exe", "League of Legends" },
        { L"league of legends.exe", "League of Legends" },
        { L"dota2.exe", "Dota 2" },
        { L"fortniteclient-win64-shipping.exe", "Fortnite" },
        { L"gta5.exe", "GTA V" },
        { L"gta5_enhanced.exe", "GTA V" },
        { L"rocketleague.exe", "Rocket League" },
        { L"rainbowsix.exe", "Rainbow Six Siege" },
        { L"rainbowsix_vulkan.exe", "Rainbow Six Siege" },
        { L"overwatch.exe", "Overwatch 2" },
        { L"r5apex.exe", "Apex Legends" },
        { L"tslgame.exe", "PUBG: Battlegrounds" },
        { L"wow.exe", "World of Warcraft" },
        { L"wowclassic.exe", "World of Warcraft Classic" },
        { L"rustclient.exe", "Rust" },
        { L"dayz_x64.exe", "DayZ" },
        { L"eurotrucks2.exe", "Euro Truck Simulator 2" },
        { L"ats.exe", "American Truck Simulator" },
        { L"genshinimpact.exe", "Genshin Impact" },
        { L"ffxiv_dx11.exe", "Final Fantasy XIV" },
        { L"terraria.exe", "Terraria" },
        { L"shootergame.exe", "ARK: Survival Evolved" },
        { L"eldenring.exe", "Elden Ring" },
        { L"cyberpunk2077.exe", "Cyberpunk 2077" },
        { L"witcher3.exe", "The Witcher 3" },
        { L"minecraft.windows.exe", "Minecraft" },
        { L"javaw.exe", "Minecraft" }, // ambiguous (any Java app), but common enough to be worth it
    };

    void AdjustPriority(DWORD pid, DWORD newPriority, int* adjustedCounter = nullptr)
    {
        HANDLE h = OpenProcess(PROCESS_SET_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!h)
        {
            return; // no permission (elevated process, etc.) — skip, don't fail the session
        }

        DWORD original = GetPriorityClass(h);
        if (original == 0)
        {
            CloseHandle(h);
            return;
        }

        if (SetPriorityClass(h, newPriority))
        {
            g_adjusted.push_back({ pid, h, original });
            if (adjustedCounter) (*adjustedCounter)++;
        }
        else
        {
            CloseHandle(h);
        }
    }

    std::string ExeBaseNameUtf8NoExt(HANDLE process)
    {
        wchar_t path[MAX_PATH];
        DWORD size = MAX_PATH;
        if (!QueryFullProcessImageNameW(process, 0, path, &size))
        {
            return "";
        }

        std::wstring full(path, size);
        size_t slash = full.find_last_of(L"\\/");
        std::wstring base = (slash == std::wstring::npos) ? full : full.substr(slash + 1);
        size_t dot = base.find_last_of(L'.');
        std::wstring nameNoExt = (dot == std::wstring::npos) ? base : base.substr(0, dot);
        return WinStr::ToUtf8(nameNoExt);
    }
}

namespace ProcessBoost
{
    std::optional<KnownGameMatch> FindRunningKnownGame()
    {
        std::optional<KnownGameMatch> found;
        DWORD selfPid = GetCurrentProcessId();

        ForEachProcess([&](const PROCESSENTRY32W& entry) {
            if (found.has_value() || entry.th32ProcessID == selfPid)
            {
                return;
            }
            std::wstring nameLower = ToLowerCopy(entry.szExeFile);
            for (const KnownGame& g : kKnownGames)
            {
                if (nameLower == g.exeName)
                {
                    found = KnownGameMatch{ entry.th32ProcessID, g.displayName };
                    break;
                }
            }
        });

        return found;
    }

    std::optional<unsigned long> FindProcessUnderPath(const std::string& installDir)
    {
        if (installDir.empty())
        {
            return std::nullopt;
        }

        std::wstring prefix = WinStr::ToLower(WinStr::ToWide(installDir));
        if (!prefix.empty() && prefix.back() != L'\\')
        {
            prefix += L'\\';
        }

        std::optional<unsigned long> found;
        DWORD selfPid = GetCurrentProcessId();
        ForEachProcess([&](const PROCESSENTRY32W& entry) {
            if (found.has_value() || entry.th32ProcessID == selfPid)
            {
                return;
            }
            std::wstring path = WinStr::ToLower(ProcessImagePath(entry.th32ProcessID));
            if (!path.empty() && path.rfind(prefix, 0) == 0)
            {
                found = entry.th32ProcessID;
            }
        });
        return found;
    }

    Result BeginForPid(unsigned long pid, bool throttleBackground)
    {
        Result result;
        DWORD selfPid = GetCurrentProcessId();

        if (pid != 0 && pid != selfPid)
        {
            AdjustPriority(pid, ABOVE_NORMAL_PRIORITY_CLASS);
            result.foregroundFound = true;
            result.targetPid = pid;

            HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
            if (h)
            {
                result.targetProcessName = ExeBaseNameUtf8NoExt(h);
                CloseHandle(h);
            }
        }

        if (throttleBackground)
        {
            ForEachProcess([&](const PROCESSENTRY32W& entry) {
                if (entry.th32ProcessID == pid || entry.th32ProcessID == selfPid)
                {
                    return;
                }
                if (IsAllowlistedBackground(ToLowerCopy(entry.szExeFile)))
                {
                    AdjustPriority(entry.th32ProcessID, BELOW_NORMAL_PRIORITY_CLASS, &result.throttledCount);
                }
            });
        }

        return result;
    }

    Result Begin(bool throttleBackground)
    {
        HWND fg = GetForegroundWindow();
        DWORD fgPid = 0;
        if (fg)
        {
            GetWindowThreadProcessId(fg, &fgPid);
        }
        return BeginForPid(fgPid, throttleBackground);
    }

    int TrimBackgroundMemory()
    {
        int trimmed = 0;
        DWORD selfPid = GetCurrentProcessId();
        ForEachProcess([&](const PROCESSENTRY32W& entry) {
            if (entry.th32ProcessID == selfPid)
            {
                return;
            }
            if (!IsAllowlistedBackground(ToLowerCopy(entry.szExeFile)))
            {
                return;
            }
            HANDLE h = OpenProcess(PROCESS_SET_QUOTA | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
            if (!h)
            {
                return;
            }
            // (SIZE_T)-1 for both limits is the documented way to ask
            // Windows to trim a process's working set.
            if (SetProcessWorkingSetSize(h, (SIZE_T)-1, (SIZE_T)-1))
            {
                trimmed++;
            }
            CloseHandle(h);
        });
        return trimmed;
    }

    void End()
    {
        for (Adjusted& a : g_adjusted)
        {
            SetPriorityClass(a.handle, a.originalPriority);
            CloseHandle(a.handle);
        }
        g_adjusted.clear();
    }
}
