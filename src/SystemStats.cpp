#include "SystemStats.h"

#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <vector>

#pragma comment(lib, "pdh.lib")

namespace
{
    unsigned long long ToU64(const FILETIME& ft)
    {
        ULARGE_INTEGER u;
        u.LowPart = ft.dwLowDateTime;
        u.HighPart = ft.dwHighDateTime;
        return u.QuadPart;
    }
}

struct SystemStats::Impl
{
    bool haveCpuBaseline = false;
    unsigned long long prevIdle = 0, prevKernel = 0, prevUser = 0;

    PDH_HQUERY gpuQuery = nullptr;
    PDH_HCOUNTER gpuCounter = nullptr;
    bool gpuAvailable = false;

    Impl()
    {
        // "GPU Engine(*)\Utilization Percentage" is a wildcard counter: one
        // instance per (process, engine) pair. It's exposed by the OS
        // (DXGK) since Windows 10 1803, so this works across NVIDIA/AMD/
        // Intel without a vendor SDK — unlike temperature, which has no
        // such OS-level counter.
        if (PdhOpenQueryW(nullptr, 0, &gpuQuery) == ERROR_SUCCESS)
        {
            PDH_STATUS status = PdhAddEnglishCounterW(
                gpuQuery, L"\\GPU Engine(*)\\Utilization Percentage", 0, &gpuCounter);
            if (status == ERROR_SUCCESS)
            {
                // Rate counters need one throwaway collection before the
                // first real reading is meaningful.
                PdhCollectQueryData(gpuQuery);
                gpuAvailable = true;
            }
            else
            {
                PdhCloseQuery(gpuQuery);
                gpuQuery = nullptr;
            }
        }
    }

    ~Impl()
    {
        if (gpuQuery)
        {
            PdhCloseQuery(gpuQuery);
        }
    }
};

SystemStats::SystemStats() : m_impl(new Impl()) {}

SystemStats::~SystemStats()
{
    delete m_impl;
}

float SystemStats::SampleCpu()
{
    FILETIME idleFt, kernelFt, userFt;
    if (!GetSystemTimes(&idleFt, &kernelFt, &userFt))
    {
        return 0.0f;
    }

    const unsigned long long idle = ToU64(idleFt);
    const unsigned long long kernel = ToU64(kernelFt); // includes idle time
    const unsigned long long user = ToU64(userFt);

    if (!m_impl->haveCpuBaseline)
    {
        m_impl->prevIdle = idle;
        m_impl->prevKernel = kernel;
        m_impl->prevUser = user;
        m_impl->haveCpuBaseline = true;
        return 0.0f;
    }

    const unsigned long long idleDelta = idle - m_impl->prevIdle;
    const unsigned long long kernelDelta = kernel - m_impl->prevKernel;
    const unsigned long long userDelta = user - m_impl->prevUser;
    const unsigned long long totalDelta = kernelDelta + userDelta;

    m_impl->prevIdle = idle;
    m_impl->prevKernel = kernel;
    m_impl->prevUser = user;

    if (totalDelta == 0)
    {
        return 0.0f;
    }

    const double busy = static_cast<double>(totalDelta - idleDelta);
    float pct = static_cast<float>(busy / static_cast<double>(totalDelta) * 100.0);
    if (pct < 0.0f) pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;
    return pct;
}

float SystemStats::SampleRam()
{
    MEMORYSTATUSEX mem{};
    mem.dwLength = sizeof(mem);
    if (!GlobalMemoryStatusEx(&mem))
    {
        return 0.0f;
    }
    return static_cast<float>(mem.dwMemoryLoad);
}

std::optional<float> SystemStats::SampleGpu()
{
    if (!m_impl->gpuAvailable)
    {
        return std::nullopt;
    }

    if (PdhCollectQueryData(m_impl->gpuQuery) != ERROR_SUCCESS)
    {
        return std::nullopt;
    }

    DWORD bufferSize = 0, itemCount = 0;
    PDH_STATUS status = PdhGetFormattedCounterArrayW(
        m_impl->gpuCounter, PDH_FMT_DOUBLE, &bufferSize, &itemCount, nullptr);
    if (status != PDH_MORE_DATA || itemCount == 0)
    {
        return std::nullopt;
    }

    std::vector<char> buffer(bufferSize);
    auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());
    status = PdhGetFormattedCounterArrayW(
        m_impl->gpuCounter, PDH_FMT_DOUBLE, &bufferSize, &itemCount, items);
    if (status != ERROR_SUCCESS)
    {
        return std::nullopt;
    }

    // Sum every (process, engine) instance. This double-counts a bit versus
    // Task Manager's "per engine type, take the max" approach, but is a
    // reasonable first-pass approximation of "how busy is the GPU overall."
    double total = 0.0;
    for (DWORD i = 0; i < itemCount; ++i)
    {
        if (items[i].FmtValue.CStatus == ERROR_SUCCESS)
        {
            total += items[i].FmtValue.doubleValue;
        }
    }

    if (total > 100.0) total = 100.0;
    if (total < 0.0) total = 0.0;
    return static_cast<float>(total);
}

SystemSnapshot SystemStats::Sample()
{
    SystemSnapshot snapshot;
    snapshot.cpuPercent = SampleCpu();
    snapshot.ramPercent = SampleRam();
    snapshot.gpuPercent = SampleGpu();
    return snapshot;
}
