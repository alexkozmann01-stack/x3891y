#include "SystemInfo.h"

#include <windows.h>

namespace SystemInfo
{
    bool IsLaptop()
    {
        SYSTEM_POWER_STATUS status{};
        if (!GetSystemPowerStatus(&status))
        {
            return false; // can't tell — default to the more common assumption
        }
        return status.BatteryFlag != BATTERY_FLAG_NO_BATTERY;
    }
}
