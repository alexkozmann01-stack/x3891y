#include "ProcessBoost.h"

#include <windows.h>
#include <tlhelp32.h>
#include <vector>
#include <cwctype>

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
        std::wstring out = s;
        for (auto& c : out)
        {
            c = (wchar_t)std::towlower(c);
        }
        return out;
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

    void AdjustPriority(DWORD pid, DWORD newPriority)
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

        int len = WideCharToMultiByte(CP_UTF8, 0, nameNoExt.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (len <= 0)
        {
            return "";
        }
        std::string utf8(len, '\0');
        WideCharToMultiByte(CP_UTF8, 0, nameNoExt.c_str(), -1, utf8.data(), len, nullptr, nullptr);
        if (!utf8.empty() && utf8.back() == '\0')
        {
            utf8.pop_back(); // the -1/len pair above counts the null terminator
        }
        return utf8;
    }
}

namespace ProcessBoost
{
    Result Begin(bool throttleBackground)
    {
        Result result;

        HWND fg = GetForegroundWindow();
        DWORD fgPid = 0;
        if (fg)
        {
            GetWindowThreadProcessId(fg, &fgPid);
        }

        if (fgPid != 0 && fgPid != GetCurrentProcessId())
        {
            AdjustPriority(fgPid, ABOVE_NORMAL_PRIORITY_CLASS);
            result.foregroundFound = true;
            result.targetPid = fgPid;

            HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, fgPid);
            if (h)
            {
                result.targetProcessName = ExeBaseNameUtf8NoExt(h);
                CloseHandle(h);
            }
        }

        if (throttleBackground)
        {
            HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
            if (snap != INVALID_HANDLE_VALUE)
            {
                PROCESSENTRY32W entry;
                entry.dwSize = sizeof(entry);
                if (Process32FirstW(snap, &entry))
                {
                    do
                    {
                        if (entry.th32ProcessID == fgPid || entry.th32ProcessID == GetCurrentProcessId())
                        {
                            continue;
                        }
                        if (IsAllowlistedBackground(ToLowerCopy(entry.szExeFile)))
                        {
                            AdjustPriority(entry.th32ProcessID, BELOW_NORMAL_PRIORITY_CLASS);
                            result.throttledCount++;
                        }
                    } while (Process32NextW(snap, &entry));
                }
                CloseHandle(snap);
            }
        }

        return result;
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
