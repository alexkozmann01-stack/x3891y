#include "WinTweaks.h"

#include <windows.h>
#include <powrprof.h>

#pragma comment(lib, "powrprof.lib")

namespace
{
    // Windows' built-in High Performance scheme. Not present on every
    // machine (some OEM images strip it, and "modern standby" laptops hide
    // it), so PowerSetActiveScheme is allowed to fail and we just report it.
    const GUID kHighPerformance =
        { 0x8c5e7fda, 0xe8bf, 0x4a96, { 0x9a, 0x85, 0xa6, 0xe2, 0x3a, 0x8c, 0x63, 0x5c } };

    bool g_powerChanged = false;
    GUID g_previousScheme{};
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

}
