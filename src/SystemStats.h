#pragma once

#include <optional>

// One point-in-time read of host machine load. Percentages are 0-100.
//
// Temperatures are intentionally NOT included here: reading CPU/GPU die
// temperature on Windows without admin rights and without a vendor SDK
// (NVAPI/ADLX) or a kernel driver (the approach LibreHardwareMonitor takes,
// via its bundled WinRing0 driver) isn't reliably possible. Wiring that up
// is future work — see the comment above SampleGpu() in SystemStats.cpp.
// The telemetry API already has cpu_temp_c/gpu_temp_c fields ready for
// whenever that lands.
struct SystemSnapshot
{
    float cpuPercent = 0.0f;
    std::optional<float> gpuPercent; // unset if the GPU Engine perf counter isn't available
    float ramPercent = 0.0f;
};

// Samples CPU/RAM/GPU load. Not thread-safe; call Sample() from one thread
// (e.g. once per second from the render loop) and reuse the instance so the
// CPU/GPU counters have a previous reading to diff against.
class SystemStats
{
public:
    SystemStats();
    ~SystemStats();

    SystemStats(const SystemStats&) = delete;
    SystemStats& operator=(const SystemStats&) = delete;

    SystemSnapshot Sample();

private:
    float SampleCpu();
    float SampleRam();
    std::optional<float> SampleGpu();

    struct Impl;
    Impl* m_impl;
};
