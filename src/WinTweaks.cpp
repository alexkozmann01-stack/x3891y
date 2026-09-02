#include "WinTweaks.h"

#include <windows.h>
#include <powrprof.h>
#include <timeapi.h>

#pragma comment(lib, "powrprof.lib")
#pragma comment(lib, "winmm.lib")

namespace
{
    // Windows' built-in High Performance scheme. Not present on every
    // machine (some OEM images strip it, and "modern standby" laptops hide
    // it), so PowerSetActiveScheme is allowed to fail and we just report it.
    const GUID kHighPerformance =
        { 0x8c5e7fda, 0xe8bf, 0x4a96, { 0x9a, 0x85, 0xa6, 0xe2, 0x3a, 0x8c, 0x63, 0x5c } };

    bool g_powerChanged = false;
    GUID g_previousScheme{};

    bool g_timerRaised = false;

    DWORD RegReadDword(HKEY root, const wchar_t* subKey, const wchar_t* valueName, DWORD fallback)
    {
        DWORD value = fallback;
        DWORD size = sizeof(value);
        DWORD type = 0;
        if (RegGetValueW(root, subKey, valueName, RRF_RT_REG_DWORD, &type, &value, &size) != ERROR_SUCCESS)
        {
            return fallback;
        }
        return value;
    }

    void RegWriteDword(HKEY root, const wchar_t* subKey, const wchar_t* valueName, DWORD value)
    {
        HKEY key = nullptr;
        if (RegCreateKeyExW(root, subKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
        {
            return;
        }
        RegSetValueExW(key, valueName, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
        RegCloseKey(key);
    }

    const wchar_t* kGameDvrStore = L"System\\GameConfigStore";
    const wchar_t* kGameDvrPolicy = L"Software\\Microsoft\\Windows\\CurrentVersion\\GameDVR";
    const wchar_t* kGameBar = L"Software\\Microsoft\\GameBar";
}

namespace WinTweaks
{
    bool BeginHighPerformancePower()
    {
        if (g_powerChanged)
        {
            return true;
        }

        GUID* active = nullptr;
        if (PowerGetActiveScheme(nullptr, &active) != ERROR_SUCCESS || active == nullptr)
        {
            return false;
        }
        g_previousScheme = *active;
        LocalFree(active);

        if (PowerSetActiveScheme(nullptr, &kHighPerformance) != ERROR_SUCCESS)
        {
            return false; // scheme not available on this machine — leave things alone
        }
        g_powerChanged = true;
        return true;
    }

    void EndHighPerformancePower()
    {
        if (!g_powerChanged)
        {
            return;
        }
        PowerSetActiveScheme(nullptr, &g_previousScheme);
        g_powerChanged = false;
    }

    void BeginHighResolutionTimer()
    {
        if (g_timerRaised)
        {
            return;
        }
        if (timeBeginPeriod(1) == TIMERR_NOERROR)
        {
            g_timerRaised = true;
        }
    }

    void EndHighResolutionTimer()
    {
        if (!g_timerRaised)
        {
            return;
        }
        timeEndPeriod(1);
        g_timerRaised = false;
    }

    bool IsGameDvrDisabled()
    {
        // Default (no value present) is enabled, so treat "missing" as
        // not-disabled rather than assuming our own preferred state.
        return RegReadDword(HKEY_CURRENT_USER, kGameDvrStore, L"GameDVR_Enabled", 1) == 0;
    }

    void SetGameDvrDisabled(bool disabled)
    {
        const DWORD enabled = disabled ? 0u : 1u;
        RegWriteDword(HKEY_CURRENT_USER, kGameDvrStore, L"GameDVR_Enabled", enabled);
        RegWriteDword(HKEY_CURRENT_USER, kGameDvrPolicy, L"AppCaptureEnabled", enabled);
    }

    bool IsGameModeEnabled()
    {
        return RegReadDword(HKEY_CURRENT_USER, kGameBar, L"AutoGameModeEnabled", 1) != 0;
    }

    void SetGameModeEnabled(bool enabled)
    {
        RegWriteDword(HKEY_CURRENT_USER, kGameBar, L"AutoGameModeEnabled", enabled ? 1u : 0u);
        RegWriteDword(HKEY_CURRENT_USER, kGameBar, L"AllowAutoGameMode", enabled ? 1u : 0u);
    }
}
