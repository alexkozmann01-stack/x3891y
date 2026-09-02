#include "App.h"
#include "Theme.h"

#include "imgui.h"

#include <windows.h>
#include <intrin.h>
#include <dxgi.h>
#include <ctime>
#include <cstdio>
#include <algorithm>

#pragma comment(lib, "dxgi.lib")

namespace
{
    std::string WideToUtf8(const std::wstring& w)
    {
        if (w.empty()) return "";
        int len = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
        std::string out(len, '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), out.data(), len, nullptr, nullptr);
        return out;
    }

    std::string GetHostnameUtf8()
    {
        wchar_t buf[256];
        DWORD size = 256;
        if (GetComputerNameExW(ComputerNameDnsHostname, buf, &size))
        {
            return WideToUtf8(std::wstring(buf, size));
        }
        return "unknown-pc";
    }

    // Best-effort. Windows version detection without an app manifest is
    // unreliable (GetVersionEx lies to unmanifested apps since Win8.1), so
    // this stays generic — os_info is an informational field on the server,
    // not load-bearing for anything.
    std::string GetOsInfoUtf8()
    {
        return "Windows";
    }

    std::string GetCpuBrandUtf8()
    {
        int cpuInfo[4] = {0};
        char brand[0x40] = {0};
        __cpuid(cpuInfo, 0x80000000);
        unsigned int maxExtId = (unsigned int)cpuInfo[0];
        if (maxExtId < 0x80000004)
        {
            return "Unknown CPU";
        }
        __cpuid((int*)(brand + 0), 0x80000002);
        __cpuid((int*)(brand + 16), 0x80000003);
        __cpuid((int*)(brand + 32), 0x80000004);
        std::string s(brand);
        // Vendor strings are padded/spaced oddly; trim surrounding whitespace.
        size_t start = s.find_first_not_of(' ');
        size_t end = s.find_last_not_of(' ');
        return (start == std::string::npos) ? "Unknown CPU" : s.substr(start, end - start + 1);
    }

    std::string GetPrimaryGpuNameUtf8()
    {
        IDXGIFactory* factory = nullptr;
        if (FAILED(CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)&factory)))
        {
            return "Unknown GPU";
        }

        std::string name = "Unknown GPU";
        IDXGIAdapter* adapter = nullptr;
        if (factory->EnumAdapters(0, &adapter) == S_OK)
        {
            DXGI_ADAPTER_DESC desc;
            if (SUCCEEDED(adapter->GetDesc(&desc)))
            {
                name = WideToUtf8(desc.Description);
            }
            adapter->Release();
        }
        factory->Release();
        return name;
    }

    std::string NowUtc()
    {
        std::time_t t = std::time(nullptr);
        std::tm tmBuf;
        gmtime_s(&tmBuf, &t);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmBuf);
        return buf;
    }

    std::string FormatPercent(std::optional<float> v)
    {
        if (!v.has_value()) return "N/A";
        char buf[16];
        snprintf(buf, sizeof(buf), "%.0f%%", *v);
        return buf;
    }
}

App::App()
{
    ApplyNasakiTheme();
    m_device = LicenseStore::Load();
    if (m_device.has_value())
    {
        m_view = AppView::Dashboard;
    }
}

// ---------------------------------------------------------------------------
// License activation

void App::SubmitLicenseKey()
{
    if (m_activating) return;

    std::string key = m_licenseKeyInput;
    if (key.empty())
    {
        m_licenseError = "Zadaj licenčný kľúč.";
        return;
    }

    m_activating = true;
    m_licenseError.clear();

    const std::string hostname = GetHostnameUtf8();
    const std::string osInfo = GetOsInfoUtf8();
    const std::string cpuInfo = GetCpuBrandUtf8();
    const std::string gpuInfo = GetPrimaryGpuNameUtf8();

    m_worker.Enqueue([this, key, hostname, osInfo, cpuInfo, gpuInfo]() {
        RegisterDeviceResult result = m_api.RegisterDevice(key, hostname, osInfo, cpuInfo, gpuInfo);
        std::lock_guard<std::mutex> lock(m_licenseResultMutex);
        m_pendingLicenseResult = result;
    });
}

void App::Unlink()
{
    StopSession();
    LicenseStore::Clear();
    m_device.reset();
    m_licenseKeyInput[0] = '\0';
    m_licenseError.clear();
    m_view = AppView::License;
}

// ---------------------------------------------------------------------------
// Session lifecycle

void App::StartSession()
{
    if (!m_device.has_value() || m_sessionActive || m_sessionStarting) return;

    m_sessionStarting = true;
    m_sessionSumCpu = m_sessionSumGpu = m_sessionSumRam = 0.0;
    m_sessionSampleCount = 0;
    m_sampleBuffer.clear();

    std::string token = m_device->deviceToken;
    std::string gameName = m_gameNameInput;
    if (gameName.empty()) gameName = "Nasaki Client";

    m_worker.Enqueue([this, token, gameName]() {
        std::optional<long long> id = m_api.SessionStart(token, gameName);
        std::lock_guard<std::mutex> lock(m_sessionStartMutex);
        m_pendingSessionStartResult = id;
    });
}

void App::StopSession()
{
    if (!m_sessionActive || !m_device.has_value()) return;

    SessionEndStats stats;
    if (m_sessionSampleCount > 0)
    {
        stats.avgCpuPct = m_sessionSumCpu / m_sessionSampleCount;
        stats.avgGpuPct = m_sessionSumGpu / m_sessionSampleCount;
        stats.avgRamPct = m_sessionSumRam / m_sessionSampleCount;
    }
    // No real per-game frame-time source yet (that needs the render-hook
    // overlay, not built in this pass) — fps/frametime/low1 are left unset
    // rather than fabricated. The schema and dashboards already handle
    // missing values as "—".

    std::string token = m_device->deviceToken;
    long long sessionId = m_sessionId;
    std::vector<TelemetrySample> remaining = std::move(m_sampleBuffer);
    m_sampleBuffer.clear();

    m_worker.Enqueue([this, token, sessionId, remaining, stats]() {
        if (!remaining.empty())
        {
            m_api.SendSamples(token, sessionId, remaining);
        }
        m_api.SessionEnd(token, sessionId, stats);
    });

    m_sessionActive = false;
    m_sessionId = 0;
}

void App::SampleTick(float deltaSeconds)
{
    m_statsTickTimer += deltaSeconds;
    if (m_statsTickTimer < 1.0f) return;
    m_statsTickTimer = 0.0f;

    m_latestSnapshot = m_stats.Sample();

    m_cpuHistory[m_historyWritePos] = m_latestSnapshot.cpuPercent;
    m_gpuHistory[m_historyWritePos] = m_latestSnapshot.gpuPercent.value_or(0.0f);
    m_ramHistory[m_historyWritePos] = m_latestSnapshot.ramPercent;
    m_historyWritePos = (m_historyWritePos + 1) % kHistoryLen;
    m_historyCount = std::min(m_historyCount + 1, kHistoryLen);

    if (!m_sessionActive) return;

    m_sessionSumCpu += m_latestSnapshot.cpuPercent;
    m_sessionSumGpu += m_latestSnapshot.gpuPercent.value_or(0.0f);
    m_sessionSumRam += m_latestSnapshot.ramPercent;
    m_sessionSampleCount++;

    TelemetrySample sample;
    sample.recordedAt = NowUtc();
    sample.cpuPct = m_latestSnapshot.cpuPercent;
    sample.ramPct = m_latestSnapshot.ramPercent;
    if (m_latestSnapshot.gpuPercent.has_value())
    {
        sample.gpuPct = *m_latestSnapshot.gpuPercent;
    }
    m_sampleBuffer.push_back(sample);

    // Flush every ~30 samples (~30s at a 1 Hz tick) so a long session doesn't
    // hold everything in memory until it ends, and so the dashboard has
    // something to show while play is still in progress.
    if (m_sampleBuffer.size() >= 30)
    {
        std::string token = m_device->deviceToken;
        long long sessionId = m_sessionId;
        std::vector<TelemetrySample> batch = std::move(m_sampleBuffer);
        m_sampleBuffer.clear();
        m_worker.Enqueue([this, token, sessionId, batch]() {
            m_api.SendSamples(token, sessionId, batch);
        });
    }
}

// ---------------------------------------------------------------------------
// Update / Draw

void App::Update(float deltaSeconds)
{
    {
        std::lock_guard<std::mutex> lock(m_licenseResultMutex);
        if (m_pendingLicenseResult.has_value())
        {
            RegisterDeviceResult result = *m_pendingLicenseResult;
            m_pendingLicenseResult.reset();
            m_activating = false;

            if (result.ok)
            {
                StoredDevice device;
                device.licenseKey = m_licenseKeyInput;
                device.deviceId = result.deviceId;
                device.deviceToken = result.deviceToken;
                LicenseStore::Save(device);
                m_device = device;
                m_view = AppView::Dashboard;
            }
            else if (result.error == "license_not_active")
            {
                m_licenseError = "Táto licencia nie je aktívna (neplatná, expirovaná alebo zrušená).";
            }
            else if (result.error == "network_error")
            {
                m_licenseError = "Nepodarilo sa spojiť s nasaki.eu. Skontroluj internetové pripojenie.";
            }
            else
            {
                m_licenseError = "Aktivácia zlyhala (" + result.error + ").";
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_sessionStartMutex);
        if (m_pendingSessionStartResult.has_value())
        {
            std::optional<long long> id = *m_pendingSessionStartResult;
            m_pendingSessionStartResult.reset();
            m_sessionStarting = false;

            if (id.has_value())
            {
                m_sessionId = *id;
                m_sessionActive = true;
            }
        }
    }

    if (m_device.has_value())
    {
        SampleTick(deltaSeconds);
    }
}

void App::Draw()
{
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::Begin("NasakiRoot", nullptr, flags);

    if (m_view == AppView::License || !m_device.has_value())
    {
        DrawLicenseView();
    }
    else
    {
        DrawSidebar();
        ImGui::SameLine();
        ImGui::BeginChild("Content", ImVec2(0, 0), false);
        ImGui::Dummy(ImVec2(0, 4));
        switch (m_view)
        {
            case AppView::Dashboard:   DrawDashboardView();   break;
            case AppView::Performance: DrawPerformanceView();break;
            case AppView::Settings:    DrawSettingsView();    break;
            default: break;
        }
        ImGui::EndChild();
    }

    ImGui::End();
}

// ---------------------------------------------------------------------------
// Views

void App::DrawSidebar()
{
    ImGui::BeginChild("Sidebar", ImVec2(220, 0), true);

    ImGui::PushStyleColor(ImGuiCol_Text, NasakiColors::Accent2());
    ImGui::TextUnformatted("NASAKI");
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0, 12));

    auto navItem = [this](const char* label, AppView view) {
        bool selected = (m_view == view);
        if (ImGui::Selectable(label, selected, 0, ImVec2(0, 30)))
        {
            m_view = view;
        }
    };

    navItem("Prehľad", AppView::Dashboard);
    navItem("Výkon", AppView::Performance);
    navItem("Nastavenia", AppView::Settings);

    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 60);
    ImGui::Separator();
    ImGui::PushStyleColor(ImGuiCol_Text, NasakiColors::InkDim());
    ImGui::TextWrapped("%s", m_sessionActive ? "Session aktívna" : "Bez aktívnej session");
    ImGui::PopStyleColor();

    ImGui::EndChild();
}

void App::DrawStatTile(const char* label, const std::string& value, float width)
{
    ImGui::BeginChild(label, ImVec2(width, 80), true);
    ImGui::PushStyleColor(ImGuiCol_Text, NasakiColors::Accent2());
    ImGui::SetWindowFontScale(1.4f);
    ImGui::TextUnformatted(value.c_str());
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleColor();
    ImGui::PushStyleColor(ImGuiCol_Text, NasakiColors::InkDim());
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::EndChild();
}

void App::DrawLicenseView()
{
    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImVec2 cardSize(380, 260);
    ImGui::SetCursorPos(ImVec2((avail.x - cardSize.x) * 0.5f, (avail.y - cardSize.y) * 0.5f));

    ImGui::BeginChild("LicenseCard", cardSize, true);

    ImGui::PushStyleColor(ImGuiCol_Text, NasakiColors::Accent2());
    ImGui::TextUnformatted("NASAKI");
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0, 8));
    ImGui::TextUnformatted("Aktivovať licenciu");
    ImGui::PushStyleColor(ImGuiCol_Text, NasakiColors::InkDim());
    ImGui::TextWrapped("Zadaj licenčný kľúč z tvojho účtu na nasaki.eu.");
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0, 12));

    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##licensekey", "XXXXX-XXXXX-XXXXX-XXXXX", m_licenseKeyInput, sizeof(m_licenseKeyInput),
        ImGuiInputTextFlags_CharsUppercase);

    ImGui::Dummy(ImVec2(0, 10));
    ImGui::BeginDisabled(m_activating);
    if (ImGui::Button(m_activating ? "Aktivujem..." : "Aktivovať", ImVec2(-1, 36)))
    {
        SubmitLicenseKey();
    }
    ImGui::EndDisabled();

    if (!m_licenseError.empty())
    {
        ImGui::Dummy(ImVec2(0, 8));
        ImGui::PushStyleColor(ImGuiCol_Text, NasakiColors::Danger());
        ImGui::TextWrapped("%s", m_licenseError.c_str());
        ImGui::PopStyleColor();
    }

    ImGui::EndChild();
}

void App::DrawDashboardView()
{
    ImGui::TextUnformatted("Vitaj späť");
    ImGui::PushStyleColor(ImGuiCol_Text, NasakiColors::InkDim());
    ImGui::TextUnformatted("Živý prehľad výkonu tohto počítača.");
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0, 16));

    float tileWidth = (ImGui::GetContentRegionAvail().x - 20) / 3.0f;
    DrawStatTile("CPU", FormatPercent(m_latestSnapshot.cpuPercent), tileWidth);
    ImGui::SameLine();
    DrawStatTile("GPU", FormatPercent(m_latestSnapshot.gpuPercent), tileWidth);
    ImGui::SameLine();
    DrawStatTile("RAM", FormatPercent(m_latestSnapshot.ramPercent), tileWidth);

    ImGui::Dummy(ImVec2(0, 16));

    ImGui::BeginChild("SessionPanel", ImVec2(0, 110), true);
    ImGui::TextUnformatted("Session");
    ImGui::Dummy(ImVec2(0, 6));
    if (!m_sessionActive)
    {
        ImGui::SetNextItemWidth(260);
        ImGui::InputText("##gamename", m_gameNameInput, sizeof(m_gameNameInput));
        ImGui::SameLine();
        ImGui::BeginDisabled(m_sessionStarting);
        if (ImGui::Button(m_sessionStarting ? "Spúšťam..." : "Spustiť session"))
        {
            StartSession();
        }
        ImGui::EndDisabled();
    }
    else
    {
        ImGui::PushStyleColor(ImGuiCol_Text, NasakiColors::Ok());
        ImGui::Text("Aktívna: %s", m_gameNameInput);
        ImGui::PopStyleColor();
        if (ImGui::Button("Ukončiť session"))
        {
            StopSession();
        }
    }
    ImGui::EndChild();

    ImGui::Dummy(ImVec2(0, 16));
    ImGui::TextUnformatted("Záťaž (posledné ~2 min)");
    if (m_historyCount > 1)
    {
        ImGui::PlotLines("CPU %", m_cpuHistory, m_historyCount, m_historyWritePos, nullptr, 0.0f, 100.0f, ImVec2(-1, 60));
        ImGui::PlotLines("GPU %", m_gpuHistory, m_historyCount, m_historyWritePos, nullptr, 0.0f, 100.0f, ImVec2(-1, 60));
        ImGui::PlotLines("RAM %", m_ramHistory, m_historyCount, m_historyWritePos, nullptr, 0.0f, 100.0f, ImVec2(-1, 60));
    }
    else
    {
        ImGui::PushStyleColor(ImGuiCol_Text, NasakiColors::InkDim());
        ImGui::TextUnformatted("Zbieram dáta...");
        ImGui::PopStyleColor();
    }
}

void App::DrawPerformanceView()
{
    ImGui::TextUnformatted("Výkon");
    ImGui::PushStyleColor(ImGuiCol_Text, NasakiColors::InkDim());
    ImGui::TextWrapped(
        "Tento panel zobrazuje záťaž aktuálnej relácie. Plná história session-ov "
        "(FPS, 1%% low, teploty) je na nasaki.eu/account/performance.php.");
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0, 16));

    if (m_historyCount > 1)
    {
        ImGui::TextUnformatted("CPU %");
        ImGui::PlotLines("##cpu", m_cpuHistory, m_historyCount, m_historyWritePos, nullptr, 0.0f, 100.0f, ImVec2(-1, 100));
        ImGui::Dummy(ImVec2(0, 10));
        ImGui::TextUnformatted("GPU %");
        ImGui::PlotLines("##gpu", m_gpuHistory, m_historyCount, m_historyWritePos, nullptr, 0.0f, 100.0f, ImVec2(-1, 100));
        ImGui::Dummy(ImVec2(0, 10));
        ImGui::TextUnformatted("RAM %");
        ImGui::PlotLines("##ram", m_ramHistory, m_historyCount, m_historyWritePos, nullptr, 0.0f, 100.0f, ImVec2(-1, 100));
    }
    else
    {
        ImGui::PushStyleColor(ImGuiCol_Text, NasakiColors::InkDim());
        ImGui::TextUnformatted("Zbieram dáta...");
        ImGui::PopStyleColor();
    }
}

void App::DrawSettingsView()
{
    ImGui::TextUnformatted("Nastavenia");
    ImGui::Dummy(ImVec2(0, 16));

    if (m_device.has_value())
    {
        ImGui::Text("Licenčný kľúč: %s", m_device->licenseKey.c_str());
        ImGui::Text("Device ID: %lld", m_device->deviceId);
    }

    ImGui::Dummy(ImVec2(0, 16));
    if (ImGui::Button("Odpojiť toto zariadenie"))
    {
        Unlink();
    }
    ImGui::PushStyleColor(ImGuiCol_Text, NasakiColors::InkDim());
    ImGui::TextWrapped(
        "Odstráni uloženú licenciu z tohto počítača. Budeš ju musieť znova aktivovať "
        "(licenciu si môžeš spravovať aj na nasaki.eu/account/devices.php).");
    ImGui::PopStyleColor();
}
