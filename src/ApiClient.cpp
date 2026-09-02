#include "ApiClient.h"

#include <windows.h>
#include <winhttp.h>
#include <nlohmann/json.hpp>

#pragma comment(lib, "winhttp.lib")

using json = nlohmann::json;

namespace
{
    std::wstring Utf8ToWide(const std::string& s)
    {
        if (s.empty()) return L"";
        int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
        std::wstring out(len, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), len);
        return out;
    }

    // json's `optional<double>` fields are only written when set, so
    // absent/unavailable metrics (e.g. no GPU temp yet) come through to the
    // server as an absent key rather than a false "0".
    template <typename T>
    void PutIfSet(json& j, const char* key, const std::optional<T>& value)
    {
        if (value.has_value())
        {
            j[key] = *value;
        }
    }
}

ApiClient::ApiClient(std::wstring host) : m_host(std::move(host)) {}

ApiClient::HttpResult ApiClient::PostJson(const std::wstring& path, const std::string& jsonBody, const std::string& bearerToken)
{
    HttpResult result;

    HINTERNET hSession = WinHttpOpen(
        L"NasakiClient/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);
    if (!hSession)
    {
        return result;
    }

    HINTERNET hConnect = WinHttpConnect(hSession, m_host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect)
    {
        WinHttpCloseHandle(hSession);
        return result;
    }

    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect, L"POST", path.c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE);
    if (!hRequest)
    {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return result;
    }

    std::wstring headers = L"Content-Type: application/json\r\n";
    if (!bearerToken.empty())
    {
        headers += L"Authorization: Bearer " + Utf8ToWide(bearerToken) + L"\r\n";
    }

    BOOL sent = WinHttpSendRequest(
        hRequest,
        headers.c_str(), (DWORD)headers.size(),
        (LPVOID)jsonBody.data(), (DWORD)jsonBody.size(),
        (DWORD)jsonBody.size(),
        0);

    if (sent && WinHttpReceiveResponse(hRequest, nullptr))
    {
        DWORD statusCode = 0;
        DWORD statusSize = sizeof(statusCode);
        WinHttpQueryHeaders(
            hRequest,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);

        std::string body;
        DWORD available = 0;
        while (WinHttpQueryDataAvailable(hRequest, &available) && available > 0)
        {
            std::vector<char> chunk(available);
            DWORD read = 0;
            if (!WinHttpReadData(hRequest, chunk.data(), available, &read))
            {
                break;
            }
            body.append(chunk.data(), read);
        }

        result.ok = true;
        result.statusCode = (int)statusCode;
        result.body = std::move(body);
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return result;
}

RegisterDeviceResult ApiClient::RegisterDevice(
    const std::string& licenseKey,
    const std::string& hostname,
    const std::string& osInfo,
    const std::string& cpuInfo,
    const std::string& gpuInfo)
{
    RegisterDeviceResult result;

    json payload = {
        {"license_key", licenseKey},
        {"hostname", hostname},
        {"os_info", osInfo},
        {"cpu_info", cpuInfo},
        {"gpu_info", gpuInfo},
    };

    HttpResult http = PostJson(L"/api/register-device.php", payload.dump(), "");
    if (!http.ok)
    {
        result.error = "network_error";
        return result;
    }

    json body;
    try
    {
        body = json::parse(http.body);
    }
    catch (const json::parse_error&)
    {
        result.error = "invalid_response";
        return result;
    }

    if (http.statusCode != 200 || !body.value("ok", false))
    {
        result.error = body.value("error", "server_error");
        return result;
    }

    result.ok = true;
    result.deviceId = body.value("device_id", 0LL);
    result.deviceToken = body.value("device_token", "");
    return result;
}

std::optional<long long> ApiClient::SessionStart(const std::string& deviceToken, const std::string& gameName)
{
    json payload = {
        {"event", "session_start"},
        {"game_name", gameName},
        {"device_token", deviceToken}, // fallback if the Authorization header gets stripped somewhere
    };

    HttpResult http = PostJson(L"/api/telemetry.php", payload.dump(), deviceToken);
    if (!http.ok || http.statusCode != 200)
    {
        return std::nullopt;
    }

    try
    {
        json body = json::parse(http.body);
        if (body.value("ok", false))
        {
            return body.value("session_id", 0LL);
        }
    }
    catch (const json::parse_error&) {}

    return std::nullopt;
}

bool ApiClient::SessionEnd(const std::string& deviceToken, long long sessionId, const SessionEndStats& stats)
{
    json payload = {
        {"event", "session_end"},
        {"session_id", sessionId},
        {"device_token", deviceToken},
        {"stutter_count", stats.stutterCount},
    };
    PutIfSet(payload, "avg_fps", stats.avgFps);
    PutIfSet(payload, "low1_fps", stats.low1Fps);
    PutIfSet(payload, "avg_frametime_ms", stats.avgFrametimeMs);
    PutIfSet(payload, "avg_cpu_pct", stats.avgCpuPct);
    PutIfSet(payload, "avg_gpu_pct", stats.avgGpuPct);
    PutIfSet(payload, "avg_ram_pct", stats.avgRamPct);
    PutIfSet(payload, "avg_cpu_temp_c", stats.avgCpuTempC);
    PutIfSet(payload, "avg_gpu_temp_c", stats.avgGpuTempC);

    HttpResult http = PostJson(L"/api/telemetry.php", payload.dump(), deviceToken);
    return http.ok && http.statusCode == 200;
}

bool ApiClient::SendSamples(const std::string& deviceToken, long long sessionId, const std::vector<TelemetrySample>& samples)
{
    json samplesJson = json::array();
    for (const auto& s : samples)
    {
        json sj = {{"recorded_at", s.recordedAt}};
        PutIfSet(sj, "fps", s.fps);
        PutIfSet(sj, "frametime_ms", s.frametimeMs);
        PutIfSet(sj, "cpu_pct", s.cpuPct);
        PutIfSet(sj, "gpu_pct", s.gpuPct);
        PutIfSet(sj, "ram_pct", s.ramPct);
        PutIfSet(sj, "cpu_temp_c", s.cpuTempC);
        PutIfSet(sj, "gpu_temp_c", s.gpuTempC);
        samplesJson.push_back(std::move(sj));
    }

    json payload = {
        {"event", "samples"},
        {"session_id", sessionId},
        {"device_token", deviceToken},
        {"samples", samplesJson},
    };

    HttpResult http = PostJson(L"/api/telemetry.php", payload.dump(), deviceToken);
    return http.ok && http.statusCode == 200;
}
