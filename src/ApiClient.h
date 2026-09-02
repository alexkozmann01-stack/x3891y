#pragma once

#include <string>
#include <vector>
#include <optional>

// Mirrors the JSON payload session_end / telemetry.php expects.
// See docs/API.md for the exact contract.
struct SessionEndStats
{
    std::optional<double> avgFps;
    std::optional<double> low1Fps;
    std::optional<double> avgFrametimeMs;
    std::optional<double> avgCpuPct;
    std::optional<double> avgGpuPct;
    std::optional<double> avgRamPct;
    std::optional<double> avgCpuTempC;
    std::optional<double> avgGpuTempC;
    int stutterCount = 0;
};

struct TelemetrySample
{
    std::string recordedAt; // "YYYY-MM-DD HH:MM:SS"
    std::optional<double> fps;
    std::optional<double> frametimeMs;
    std::optional<double> cpuPct;
    std::optional<double> gpuPct;
    std::optional<double> ramPct;
    std::optional<double> cpuTempC;
    std::optional<double> gpuTempC;
};

struct RegisterDeviceResult
{
    bool ok = false;
    long long deviceId = 0;
    std::string deviceToken;
    std::string error; // set when !ok — either the server's `error` field or a transport-level message
};

// Thin WinHTTP-based client for https://nasaki.eu/api/*. One instance is
// enough for the whole app's lifetime; it's cheap (opens a fresh WinHTTP
// connection per call rather than pooling, which is plenty for the call
// volume here — a handful of requests per play session, not per frame).
class ApiClient
{
public:
    explicit ApiClient(std::wstring host = L"nasaki.eu");

    RegisterDeviceResult RegisterDevice(
        const std::string& licenseKey,
        const std::string& hostname,
        const std::string& osInfo,
        const std::string& cpuInfo,
        const std::string& gpuInfo);

    // Returns the new session_id, or nullopt on failure.
    std::optional<long long> SessionStart(const std::string& deviceToken, const std::string& gameName);

    bool SessionEnd(const std::string& deviceToken, long long sessionId, const SessionEndStats& stats);

    bool SendSamples(const std::string& deviceToken, long long sessionId, const std::vector<TelemetrySample>& samples);

private:
    struct HttpResult
    {
        bool ok = false;       // transport succeeded (got a response at all)
        int statusCode = 0;
        std::string body;
    };

    HttpResult PostJson(const std::wstring& path, const std::string& jsonBody, const std::string& bearerToken);

    std::wstring m_host;
};
