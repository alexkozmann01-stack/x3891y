#pragma once

namespace SystemInfo
{
    // Heuristic: true if the machine reports having a battery
    // (GetSystemPowerStatus). Not perfect — a desktop on a UPS that
    // exposes battery status can register as a "laptop" — but it's the
    // standard low-risk signal (no WMI/COM needed) most apps use for this.
    bool IsLaptop();
}
