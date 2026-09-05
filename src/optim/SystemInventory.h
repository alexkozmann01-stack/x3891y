#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <optional>

// What this machine actually is. Every "recommended" decision reads from
// here rather than assuming — a setting that helps a 2-core laptop on
// battery is not automatically right for a desktop with a discrete GPU.
//
// Everything is collected from documented Windows APIs and read-only
// registry values. Anything that can't be determined is left empty/nullopt
// and shown as unknown, never guessed.
namespace optim
{
    struct StorageInfo
    {
        std::string driveLetter;      // "C:"
        uint64_t totalBytes = 0;
        uint64_t freeBytes = 0;
        // Seek penalty is how Windows itself distinguishes rotational media;
        // nullopt when the device didn't answer the query.
        std::optional<bool> isSolidState;
    };

    struct SystemInventory
    {
        // OS
        std::string osProductName;   // "Windows 11 Pro"
        std::string osDisplayVersion;// "24H2"
        uint32_t osBuild = 0;         // 22631, 26100, ...
        uint32_t osUpdateBuildRevision = 0;

        // CPU
        std::string cpuName;
        uint32_t logicalProcessors = 0;
        uint32_t physicalCores = 0;

        // Memory
        uint64_t totalPhysicalBytes = 0;
        uint64_t availablePhysicalBytes = 0;
        uint64_t commitTotalBytes = 0;
        uint64_t commitLimitBytes = 0;

        // GPU (primary adapter)
        std::string gpuName;
        uint64_t gpuDedicatedBytes = 0;
        // A discrete GPU is inferred from dedicated VRAM; integrated parts
        // report little or none. Used only to soften/strengthen advice, never
        // to claim a specific product.
        bool gpuLikelyDiscrete = false;

        // Display (primary)
        uint32_t displayWidth = 0;
        uint32_t displayHeight = 0;
        uint32_t displayRefreshHz = 0;
        // Highest refresh the primary display advertises at the current
        // resolution. When it exceeds displayRefreshHz the machine is running
        // below its panel — a real, checkable finding rather than a guess.
        uint32_t displayMaxRefreshHz = 0;

        // Power
        bool isLaptop = false;
        bool onBattery = false;
        std::optional<int> batteryPercent;

        std::vector<StorageInfo> drives;

        // Convenience predicates used by the catalog's classification.
        bool LowMemory() const { return totalPhysicalBytes > 0 && totalPhysicalBytes < (9ull << 30); }
        bool FewCores() const { return logicalProcessors > 0 && logicalProcessors <= 4; }
        bool HighRefresh() const { return displayRefreshHz >= 100; }
        bool DisplayBelowItsRefresh() const
        {
            return displayRefreshHz > 0 && displayMaxRefreshHz > displayRefreshHz + 1;
        }
        bool SystemDriveIsRotational() const;
        bool SystemDriveLowOnSpace() const;
    };

    // Reads everything above. Costs a few device queries and a registry
    // read, so it is called from the worker thread, not per frame.
    SystemInventory ReadSystemInventory();

    // Human-readable helpers for the dashboard.
    std::string FormatBytes(uint64_t bytes);
}
