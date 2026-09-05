#include "SystemInventory.h"
#include "../WinStr.h"

#include <windows.h>
#include <winioctl.h>
#include <psapi.h>
#include <dxgi.h>
#include <intrin.h>
#include <cstdio>
#include <vector>

#pragma comment(lib, "dxgi.lib")

namespace
{
    std::wstring RegString(HKEY root, const wchar_t* subKey, const wchar_t* value)
    {
        wchar_t buffer[512];
        DWORD size = sizeof(buffer);
        if (RegGetValueW(root, subKey, value, RRF_RT_REG_SZ, nullptr, buffer, &size) != ERROR_SUCCESS)
        {
            return L"";
        }
        return buffer;
    }

    DWORD RegDword(HKEY root, const wchar_t* subKey, const wchar_t* value, DWORD fallback)
    {
        DWORD data = fallback;
        DWORD size = sizeof(data);
        if (RegGetValueW(root, subKey, value, RRF_RT_REG_DWORD, nullptr, &data, &size) != ERROR_SUCCESS)
        {
            return fallback;
        }
        return data;
    }

    void ReadOs(optim::SystemInventory& inv)
    {
        // GetVersionEx lies to unmanifested apps, so read the values Windows
        // itself keeps in CurrentVersion. Read-only, no elevation.
        const wchar_t* key = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
        inv.osProductName = WinStr::ToUtf8(RegString(HKEY_LOCAL_MACHINE, key, L"ProductName"));
        inv.osDisplayVersion = WinStr::ToUtf8(RegString(HKEY_LOCAL_MACHINE, key, L"DisplayVersion"));

        std::wstring build = RegString(HKEY_LOCAL_MACHINE, key, L"CurrentBuildNumber");
        inv.osBuild = build.empty() ? 0 : (uint32_t)_wtoi(build.c_str());
        inv.osUpdateBuildRevision = RegDword(HKEY_LOCAL_MACHINE, key, L"UBR", 0);

        // The registry still says "Windows 10" on 11; build 22000+ is the
        // documented dividing line.
        if (inv.osBuild >= 22000 && inv.osProductName.find("Windows 10") != std::string::npos)
        {
            inv.osProductName.replace(inv.osProductName.find("Windows 10"), 10, "Windows 11");
        }
    }

    void ReadCpu(optim::SystemInventory& inv)
    {
        int cpuInfo[4] = { 0 };
        char brand[0x40] = { 0 };
        __cpuid(cpuInfo, 0x80000000);
        if ((unsigned)cpuInfo[0] >= 0x80000004)
        {
            __cpuid((int*)(brand + 0), 0x80000002);
            __cpuid((int*)(brand + 16), 0x80000003);
            __cpuid((int*)(brand + 32), 0x80000004);
            std::string name(brand);
            size_t start = name.find_first_not_of(' ');
            size_t end = name.find_last_not_of(' ');
            if (start != std::string::npos) inv.cpuName = name.substr(start, end - start + 1);
        }

        SYSTEM_INFO sysInfo{};
        GetSystemInfo(&sysInfo);
        inv.logicalProcessors = sysInfo.dwNumberOfProcessors;

        // Physical cores: count RelationProcessorCore entries.
        DWORD length = 0;
        GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &length);
        if (length > 0 && GetLastError() == ERROR_INSUFFICIENT_BUFFER)
        {
            std::vector<uint8_t> buffer(length);
            if (GetLogicalProcessorInformationEx(RelationProcessorCore,
                    reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data()), &length))
            {
                uint32_t cores = 0;
                DWORD offset = 0;
                while (offset < length)
                {
                    auto* entry = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data() + offset);
                    if (entry->Relationship == RelationProcessorCore) cores++;
                    offset += entry->Size;
                }
                inv.physicalCores = cores;
            }
        }
    }

    void ReadMemory(optim::SystemInventory& inv)
    {
        MEMORYSTATUSEX status{};
        status.dwLength = sizeof(status);
        if (GlobalMemoryStatusEx(&status))
        {
            inv.totalPhysicalBytes = status.ullTotalPhys;
            inv.availablePhysicalBytes = status.ullAvailPhys;
        }

        PERFORMANCE_INFORMATION perf{};
        perf.cb = sizeof(perf);
        if (GetPerformanceInfo(&perf, sizeof(perf)))
        {
            // Commit is reported in pages; this is the number that actually
            // predicts out-of-memory, not "free RAM".
            inv.commitTotalBytes = (uint64_t)perf.CommitTotal * perf.PageSize;
            inv.commitLimitBytes = (uint64_t)perf.CommitLimit * perf.PageSize;
        }
    }

    void ReadGpu(optim::SystemInventory& inv)
    {
        IDXGIFactory* factory = nullptr;
        if (FAILED(CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)&factory)))
        {
            return;
        }
        IDXGIAdapter* adapter = nullptr;
        if (factory->EnumAdapters(0, &adapter) == S_OK)
        {
            DXGI_ADAPTER_DESC desc{};
            if (SUCCEEDED(adapter->GetDesc(&desc)))
            {
                inv.gpuName = WinStr::ToUtf8(desc.Description);
                inv.gpuDedicatedBytes = desc.DedicatedVideoMemory;
                // Integrated parts report little or no dedicated memory.
                // This only shades advice; it never claims a specific model.
                inv.gpuLikelyDiscrete = desc.DedicatedVideoMemory > (1ull << 30);
            }
            adapter->Release();
        }
        factory->Release();
    }

    void ReadDisplay(optim::SystemInventory& inv)
    {
        DEVMODEW mode{};
        mode.dmSize = sizeof(mode);
        if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &mode))
        {
            inv.displayWidth = mode.dmPelsWidth;
            inv.displayHeight = mode.dmPelsHeight;
            inv.displayRefreshHz = mode.dmDisplayFrequency;

            // Walk every mode the adapter advertises and keep the highest
            // refresh available at the resolution and depth already in use —
            // comparing across resolutions would be meaningless advice.
            DEVMODEW candidate{};
            candidate.dmSize = sizeof(candidate);
            for (DWORD index = 0; EnumDisplaySettingsW(nullptr, index, &candidate); index++)
            {
                if (candidate.dmPelsWidth == mode.dmPelsWidth &&
                    candidate.dmPelsHeight == mode.dmPelsHeight &&
                    candidate.dmBitsPerPel == mode.dmBitsPerPel &&
                    candidate.dmDisplayFrequency > inv.displayMaxRefreshHz)
                {
                    inv.displayMaxRefreshHz = candidate.dmDisplayFrequency;
                }
            }
        }
    }

    void ReadPower(optim::SystemInventory& inv)
    {
        SYSTEM_POWER_STATUS status{};
        if (GetSystemPowerStatus(&status))
        {
            inv.isLaptop = status.BatteryFlag != BATTERY_FLAG_NO_BATTERY;
            inv.onBattery = status.ACLineStatus == 0;
            if (status.BatteryLifePercent != 255)
            {
                inv.batteryPercent = status.BatteryLifePercent;
            }
        }
    }

    std::optional<bool> QuerySolidState(const std::wstring& driveLetter)
    {
        // Opening the volume with no access rights is enough for a property
        // query and needs no elevation.
        std::wstring path = L"\\\\.\\" + driveLetter;
        HANDLE volume = CreateFileW(path.c_str(), 0,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (volume == INVALID_HANDLE_VALUE)
        {
            return std::nullopt;
        }

        STORAGE_PROPERTY_QUERY query{};
        query.PropertyId = StorageDeviceSeekPenaltyProperty;
        query.QueryType = PropertyStandardQuery;

        DEVICE_SEEK_PENALTY_DESCRIPTOR descriptor{};
        DWORD returned = 0;
        BOOL ok = DeviceIoControl(volume, IOCTL_STORAGE_QUERY_PROPERTY,
            &query, sizeof(query), &descriptor, sizeof(descriptor), &returned, nullptr);
        CloseHandle(volume);

        if (!ok || returned == 0)
        {
            return std::nullopt; // device didn't answer — report unknown
        }
        return descriptor.IncursSeekPenalty == FALSE;
    }

    void ReadStorage(optim::SystemInventory& inv)
    {
        DWORD mask = GetLogicalDrives();
        for (int i = 0; i < 26; i++)
        {
            if ((mask & (1u << i)) == 0) continue;

            wchar_t root[] = { (wchar_t)(L'A' + i), L':', L'\\', 0 };
            if (GetDriveTypeW(root) != DRIVE_FIXED) continue;

            optim::StorageInfo info;
            info.driveLetter = std::string(1, (char)('A' + i)) + ":";

            ULARGE_INTEGER freeForCaller{}, total{}, totalFree{};
            if (GetDiskFreeSpaceExW(root, &freeForCaller, &total, &totalFree))
            {
                info.totalBytes = total.QuadPart;
                info.freeBytes = freeForCaller.QuadPart;
            }

            std::wstring letter = { (wchar_t)(L'A' + i), L':' };
            info.isSolidState = QuerySolidState(letter);

            inv.drives.push_back(info);
        }
    }
}

namespace optim
{
    bool SystemInventory::SystemDriveIsRotational() const
    {
        for (const StorageInfo& drive : drives)
        {
            if (drive.driveLetter == "C:" && drive.isSolidState.has_value())
            {
                return !*drive.isSolidState;
            }
        }
        return false; // unknown is not treated as rotational
    }

    bool SystemInventory::SystemDriveLowOnSpace() const
    {
        for (const StorageInfo& drive : drives)
        {
            if (drive.driveLetter == "C:" && drive.totalBytes > 0)
            {
                // Windows itself wants headroom for updates and the page
                // file; under 10% or 20 GB is where trouble starts.
                double ratio = (double)drive.freeBytes / (double)drive.totalBytes;
                return ratio < 0.10 || drive.freeBytes < (20ull << 30);
            }
        }
        return false;
    }

    SystemInventory ReadSystemInventory()
    {
        SystemInventory inv;
        ReadOs(inv);
        ReadCpu(inv);
        ReadMemory(inv);
        ReadGpu(inv);
        ReadDisplay(inv);
        ReadPower(inv);
        ReadStorage(inv);
        return inv;
    }

    std::string FormatBytes(uint64_t bytes)
    {
        const char* units[] = { "B", "KB", "MB", "GB", "TB" };
        double value = (double)bytes;
        int unit = 0;
        while (value >= 1024.0 && unit < 4)
        {
            value /= 1024.0;
            unit++;
        }
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), unit >= 3 ? "%.1f %s" : "%.0f %s", value, units[unit]);
        return buffer;
    }
}
