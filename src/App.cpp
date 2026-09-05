#include "App.h"
#include "Theme.h"
#include "UI.h"
#include "Icons.h"
#include "SystemInfo.h"
#include "ProcessBoost.h"
#include "Autostart.h"
#include "WinTweaks.h"

#include "imgui.h"

#include <windows.h>
#include <shellapi.h> // ShellExecuteW for the ms-settings deep links
#include <intrin.h>
#include <dxgi.h>
#include <ctime>
#include <cstdio>
#include <cstring>
#include <cctype> // std::tolower, for the game-library search filter
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

// m_optimizations is declared before m_worker but only stores the pointer —
// it never touches the worker during construction, and because m_worker is
// declared last it is destroyed first, so its thread is stopped before the
// service those jobs capture goes away.
App::App(HWND hwnd) : m_hwnd(hwnd), m_optimizations(&m_worker)
{
    ApplyNasakiTheme();
    m_isLaptop = SystemInfo::IsLaptop();
    m_startWithWindows = Autostart::IsEnabled();
    m_device = LicenseStore::Load();
    if (m_device.has_value())
    {
        m_view = AppView::Dashboard;
    }
    m_optimizations.RefreshAsync();
    m_optimizations.RefreshStartupAsync();
    m_optimizations.RefreshPowerPlansAsync();
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
    SetView(AppView::License);
}

// ---------------------------------------------------------------------------
// Session lifecycle

void App::SetView(AppView view)
{
    if (m_view == view) return;
    m_view = view;
    m_viewFade = 0.0f; // content fades (and rises) back in

    // Storage analysis walks whole directory trees, so it is done when the
    // page is first opened rather than on every launch. Once measured, the
    // numbers stay until the user asks for a recount.
    if (view == AppView::Storage && m_optimizations.StorageTargets().empty()
        && !m_optimizations.StorageBusy())
    {
        m_optimizations.RefreshStorageAsync();
    }
}

void App::RescanGameLibrary()
{
    if (m_gamesScanning) return;
    m_gamesScanning = true;

    // Reads the registry and walks Steam/Epic manifest folders — far too
    // slow to do inline in a frame.
    m_worker.Enqueue([this]() {
        std::vector<InstalledGame> found = GameLibrary::Scan();
        std::lock_guard<std::mutex> lock(m_gamesMutex);
        m_pendingGames = std::move(found);
    });
}

void App::ApplySessionOptimizations()
{
    if (m_highPerformancePower)
    {
        WinTweaks::BeginHighPerformancePower();
    }
}

void App::RevertSessionOptimizations()
{
    // Unconditional: a toggle switched off mid-session must not strand the
    // machine on a power scheme we changed.
    WinTweaks::EndHighPerformancePower();
}

std::string App::BuildBoostStatus(const std::string& gameName, bool boosted, int throttledCount) const
{
    std::string throttled = std::to_string(throttledCount) + " apiek na pozadí utlmených.";
    if (boosted && throttledCount > 0)
    {
        return gameName + " zvýhodnená, " + throttled;
    }
    if (boosted)
    {
        return gameName + " zvýhodnená.";
    }
    if (throttledCount > 0)
    {
        return throttled;
    }
    return "Bez zmien priorít (optimalizácie sú vypnuté v Nastaveniach).";
}

void App::RequestStartSession()
{
    if (!m_device.has_value() || m_sessionActive || m_boostPhase != BoostPhase::Idle) return;

    // If a known game (see ProcessBoost.cpp's table) is already running,
    // there's no need to guess from the foreground window or make the
    // player wait through a countdown — boost it immediately.
    std::optional<ProcessBoost::KnownGameMatch> known = ProcessBoost::FindRunningKnownGame();
    if (known.has_value())
    {
        // pid 0 when the boost toggle is off: nothing gets prioritized, but
        // the background throttle (a separate toggle) still applies.
        ProcessBoost::Result boost = ProcessBoost::BeginForPid(
            m_boostGamePriority ? known->pid : 0, m_throttleBackground);
        strncpy_s(m_gameNameInput, known->displayName.c_str(), _TRUNCATE);
        m_boostStatus = BuildBoostStatus(known->displayName, boost.foregroundFound, boost.throttledCount);
        m_boostPhase = BoostPhase::Active;
        ApplySessionOptimizations();
        StartSession();
        return;
    }

    // Unlisted game — fall back to a short countdown so the player can
    // switch to it, then boost whatever's in the foreground.
    m_boostPhase = BoostPhase::CountingDown;
    m_boostCountdown = 3.0f;
    m_boostStatus.clear();
}

void App::CancelStartRequest()
{
    if (m_boostPhase == BoostPhase::CountingDown)
    {
        m_boostPhase = BoostPhase::Idle;
    }
}

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

    ProcessBoost::End();
    RevertSessionOptimizations();
    m_boostPhase = BoostPhase::Idle;
    m_boostStatus.clear();

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
                SetView(AppView::Dashboard);
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

    if (m_boostPhase == BoostPhase::CountingDown)
    {
        m_boostCountdown -= deltaSeconds;
        if (m_boostCountdown <= 0.0f)
        {
            // BeginForPid(0, ...) when the priority toggle is off: skips the
            // target boost, still applies the background throttle.
            ProcessBoost::Result boost = m_boostGamePriority
                ? ProcessBoost::Begin(m_throttleBackground)
                : ProcessBoost::BeginForPid(0, m_throttleBackground);
            m_boostPhase = BoostPhase::Active;

            if (boost.foregroundFound && !boost.targetProcessName.empty())
            {
                strncpy_s(m_gameNameInput, boost.targetProcessName.c_str(), _TRUNCATE);
            }
            m_boostStatus = BuildBoostStatus("Hra", boost.foregroundFound, boost.throttledCount);

            ApplySessionOptimizations();
            StartSession();
        }
    }

    m_optimizations.Pump();

    m_viewFade += deltaSeconds * 5.0f;
    if (m_viewFade > 1.0f) m_viewFade = 1.0f;

    {
        std::lock_guard<std::mutex> lock(m_gamesMutex);
        if (m_pendingGames.has_value())
        {
            m_games = std::move(*m_pendingGames);
            m_pendingGames.reset();
            m_gamesScanning = false;
            m_gamesScanned = true;
            m_runningGamePids.assign(m_games.size(), 0);
            m_runningCheckTimer = 100.0f; // force a running-state refresh next frame
        }
    }

    // Which installed games are running right now — one process-path scan
    // per game, so keep it to every few seconds rather than every frame.
    if (m_view == AppView::Games && !m_games.empty())
    {
        m_runningCheckTimer += deltaSeconds;
        if (m_runningCheckTimer >= 4.0f)
        {
            m_runningCheckTimer = 0.0f;
            m_runningGamePids.assign(m_games.size(), 0);
            for (size_t i = 0; i < m_games.size(); i++)
            {
                std::optional<unsigned long> pid = ProcessBoost::FindProcessUnderPath(m_games[i].installPath);
                m_runningGamePids[i] = pid.value_or(0);
            }
        }
    }

    // Auto-start: poll for a known game every few seconds while idle, so a
    // session begins on its own when the player launches something.
    if (m_autoStartSession && m_device.has_value() &&
        !m_sessionActive && m_boostPhase == BoostPhase::Idle)
    {
        m_autoDetectTimer += deltaSeconds;
        if (m_autoDetectTimer >= 5.0f)
        {
            m_autoDetectTimer = 0.0f;
            if (ProcessBoost::FindRunningKnownGame().has_value())
            {
                RequestStartSession(); // re-runs the scan and takes the fast path
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

    DrawTitleBar();

    if (m_view == AppView::License || !m_device.has_value())
    {
        DrawLicenseView();
    }
    else
    {
        DrawSidebar();
        ImGui::SameLine();
        ImGui::BeginChild("Content", ImVec2(0, 0), false);

        // Content fades in (and rises a few px) on every view switch.
        float fade = m_viewFade * m_viewFade * (3.0f - 2.0f * m_viewFade); // smoothstep
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, fade);
        ImGui::Dummy(ImVec2(0, 4 + (1.0f - fade) * 10.0f));
        switch (m_view)
        {
            case AppView::Dashboard:   DrawDashboardView();   break;
            case AppView::Performance: DrawPerformanceView();break;
            case AppView::Optimizations: DrawOptimizationsView(); break;
            case AppView::Startup:     DrawStartupView();     break;
            case AppView::Power:       DrawPowerView();       break;
            case AppView::Storage:     DrawStorageView();     break;
            case AppView::Backups:     DrawBackupsView();     break;
            case AppView::Games:       DrawGamesView();       break;
            case AppView::Settings:    DrawSettingsView();    break;
            default: break;
        }
        ImGui::PopStyleVar();
        ImGui::EndChild();
    }

    ImGui::End();
}

// ---------------------------------------------------------------------------
// Views

// This window has no OS title bar (see main.cpp: WS_POPUP), so this draws a
// custom one. Its height (36px) and the reserved button-strip width (70px,
// inside the 76px main.cpp treats as non-draggable) must stay in sync with
// kTitleBarHeight/kTitleBarButtonsWidth there — that's what makes the rest
// of this strip work as a window drag handle via WM_NCHITTEST.
void App::DrawTitleBar()
{
    ImGui::BeginChild("TitleBar", ImVec2(0, 36), false, ImGuiWindowFlags_NoScrollbar);

    ImGui::SetCursorPos(ImVec2(16, 7));
    ImGui::PushFont(NasakiFonts::Heading());
    ImGui::PushStyleColor(ImGuiCol_Text, NasakiColors::Accent2());
    ImGui::TextUnformatted("NASAKI");
    ImGui::PopStyleColor();
    ImGui::PopFont();

    ImGui::SameLine(0, 10);
    NasakiUI::BadgeAt(
        ImGui::GetWindowDrawList(),
        ImVec2(ImGui::GetCursorScreenPos().x, ImGui::GetCursorScreenPos().y + 2),
        "v0.1", NasakiColors::U32(NasakiColors::Accent2()), IM_COL32(47, 127, 252, 40));

    float windowWidth = ImGui::GetWindowWidth();
    ImGui::SetCursorPos(ImVec2(windowWidth - 66, 4));
    if (NasakiUI::TitleBarButton("min", ICON_MINIMIZE, ImVec2(28, 28), IM_COL32(255, 255, 255, 12)))
    {
        ShowWindow(m_hwnd, SW_MINIMIZE);
    }
    ImGui::SameLine(0, 4);
    if (NasakiUI::TitleBarButton("close", ICON_CLOSE, ImVec2(28, 28), IM_COL32(255, 93, 93, 45)))
    {
        PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
    }

    ImGui::EndChild();
    ImGui::Separator();
}

void App::DrawSidebar()
{
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 3));
    ImGui::BeginChild("Sidebar", ImVec2(232, 0), true);

    NasakiUI::SectionLabel("PREHĽAD");
    ImGui::Dummy(ImVec2(0, 4));

    auto navItem = [this](const char* id, const char* label, const char* icon, AppView view) {
        if (NasakiUI::NavItem(id, label, icon, m_view == view))
        {
            SetView(view);
        }
    };

    navItem("nav_dash", "Prehľad", ICON_NAV_HOME, AppView::Dashboard);
    navItem("nav_perf", "Výkon", ICON_NAV_PERF, AppView::Performance);

    ImGui::Dummy(ImVec2(0, 14));
    NasakiUI::SectionLabel("OPTIMALIZÁCIA");
    ImGui::Dummy(ImVec2(0, 4));

    navItem("nav_opt", "Optimalizácie", ICON_BOLT, AppView::Optimizations);
    navItem("nav_startup", "Po spustení", ICON_LAYERS, AppView::Startup);
    navItem("nav_power", "Napájanie", ICON_POWER, AppView::Power);
    navItem("nav_storage", "Úložisko", ICON_LAYERS, AppView::Storage);
    navItem("nav_backups", "Zálohy a história", ICON_ROTATE, AppView::Backups);

    ImGui::Dummy(ImVec2(0, 14));
    NasakiUI::SectionLabel("KNIŽNICA");
    ImGui::Dummy(ImVec2(0, 4));

    navItem("nav_games", "Hry", ICON_NAV_GAMES, AppView::Games);

    ImGui::Dummy(ImVec2(0, 14));
    NasakiUI::SectionLabel("SYSTÉM");
    ImGui::Dummy(ImVec2(0, 4));

    navItem("nav_settings", "Nastavenia", ICON_NAV_SETTINGS, AppView::Settings);

    ImGui::PopStyleVar();

    // Status chip pinned to the bottom, like the account chip in the
    // reference app: avatar dot, license state, session state.
    const float chipHeight = 64.0f;
    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - chipHeight - 12.0f);
    ImVec2 chipMin = ImGui::GetCursorScreenPos();
    ImVec2 chipMax(chipMin.x + ImGui::GetContentRegionAvail().x, chipMin.y + chipHeight);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(chipMin, chipMax, ImGui::GetColorU32(IM_COL32(14, 20, 36, 255)), 12.0f);
    dl->AddRect(chipMin, chipMax, ImGui::GetColorU32(IM_COL32(28, 39, 64, 255)), 12.0f, 0, 1.0f);

    ImVec2 avatarCenter(chipMin.x + 26, chipMin.y + chipHeight * 0.5f);
    dl->AddCircleFilled(avatarCenter, 15.0f, ImGui::GetColorU32(IM_COL32(47, 127, 252, 55)), 24);
    NasakiUI::DrawIconAt(dl, ICON_CHIP, avatarCenter, ImGui::GetColorU32(IM_COL32(127, 214, 255, 255)));

    dl->AddText(ImVec2(chipMin.x + 50, chipMin.y + 15),
        ImGui::GetColorU32(IM_COL32(238, 243, 251, 255)),
        m_device.has_value() ? "Licencia aktívna" : "Bez licencie");

    ImU32 dotColor = m_sessionActive
        ? ImGui::GetColorU32(IM_COL32(107, 227, 163, 255))
        : ImGui::GetColorU32(IM_COL32(110, 122, 150, 255));
    dl->AddCircleFilled(ImVec2(chipMin.x + 55, chipMin.y + 41), 3.5f, dotColor, 10);
    dl->AddText(ImVec2(chipMin.x + 65, chipMin.y + 34),
        ImGui::GetColorU32(IM_COL32(110, 122, 150, 255)),
        m_sessionActive ? "Session beží" : "Nečinné");

    ImGui::Dummy(ImVec2(0, chipHeight));
    ImGui::EndChild();
}

void App::DrawStatTile(const char* label, const std::string& value, float width)
{
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImVec2 size(width, 110);
    ImVec2 p1(p0.x + size.x, p0.y + size.y);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, p1, NasakiColors::U32(NasakiColors::BgPanel2()), 14.0f);
    dl->AddRect(p0, p1, ImGui::GetColorU32(ImGuiCol_Border), 14.0f);

    dl->AddText(ImVec2(p0.x + 22, p0.y + 22), NasakiColors::U32(NasakiColors::InkDim()), label);

    ImGui::PushFont(NasakiFonts::Title());
    dl->AddText(ImVec2(p0.x + 22, p0.y + 48), NasakiColors::U32(NasakiColors::Accent2()), value.c_str());
    ImGui::PopFont();

    ImGui::Dummy(size);
}

void App::DrawLicenseView()
{
    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImVec2 cardSize(380, 260);
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImVec2 cardCenter(
        origin.x + (avail.x - cardSize.x) * 0.5f + cardSize.x * 0.5f,
        origin.y + (avail.y - cardSize.y) * 0.5f + cardSize.y * 0.5f);

    // Soft accent glow behind the card, echoing the site's .bg-glow.
    NasakiUI::BackgroundGlow(ImGui::GetWindowDrawList(), cardCenter, 260.0f, IM_COL32(47, 127, 252, 40));

    ImGui::SetCursorPos(ImVec2((avail.x - cardSize.x) * 0.5f, (avail.y - cardSize.y) * 0.5f));
    ImGui::BeginChild("LicenseCard", cardSize, true);

    ImGui::PushFont(NasakiFonts::Heading());
    ImGui::TextUnformatted("Aktivovať licenciu");
    ImGui::PopFont();
    ImGui::PushStyleColor(ImGuiCol_Text, NasakiColors::InkDim());
    ImGui::TextWrapped("Zadaj licenčný kľúč z tvojho účtu na nasaki.eu.");
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0, 12));

    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##licensekey", "XXXXX-XXXXX-XXXXX-XXXXX", m_licenseKeyInput, sizeof(m_licenseKeyInput),
        ImGuiInputTextFlags_CharsUppercase);

    ImGui::Dummy(ImVec2(0, 10));
    ImGui::BeginDisabled(m_activating);
    if (NasakiUI::GradientButton(m_activating ? "Aktivujem..." : "Aktivovať licenciu", ImVec2(-1, 42)))
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
    DrawPageTitle("Prehľad", "Živý prehľad výkonu tohto počítača.");

    // What Nasaki detected. Shown first because every recommendation on the
    // Optimizations page is derived from it — if something here is wrong,
    // the advice will be too, and the user should be able to see that.
    {
        const optim::SystemInventory& inv = m_optimizations.Inventory();
        ImGui::Dummy(ImVec2(0, 12));
        ImGui::BeginChild("machine", ImVec2(0, 108.0f), true);

        NasakiUI::SectionLabel("ROZPOZNANÝ POČÍTAČ");
        ImGui::Dummy(ImVec2(0, 4));
        ImGui::PushStyleColor(ImGuiCol_Text, NasakiColors::InkDim());

        std::string os = inv.osProductName.empty() ? "Windows" : inv.osProductName;
        if (!inv.osDisplayVersion.empty()) os += " " + inv.osDisplayVersion;
        if (inv.osBuild > 0) os += " (build " + std::to_string(inv.osBuild) + ")";

        ImGui::TextWrapped("%s  •  %s", os.c_str(),
            inv.isLaptop ? (inv.onBattery ? "notebook, na batérii" : "notebook, v sieti")
                         : "stolný počítač");

        std::string cpu = inv.cpuName.empty() ? "procesor neznámy" : inv.cpuName;
        cpu += "  •  " + std::to_string(inv.logicalProcessors) + " vlákien";
        if (inv.physicalCores > 0) cpu += " / " + std::to_string(inv.physicalCores) + " jadier";
        cpu += "  •  " + optim::FormatBytes(inv.totalPhysicalBytes) + " RAM";
        ImGui::TextWrapped("%s", cpu.c_str());

        std::string gpu = inv.gpuName.empty() ? "grafika neznáma" : inv.gpuName;
        if (inv.displayRefreshHz > 0)
        {
            gpu += "  •  " + std::to_string(inv.displayWidth) + "x" +
                   std::to_string(inv.displayHeight) + " @ " +
                   std::to_string(inv.displayRefreshHz) + " Hz";
            if (inv.DisplayBelowItsRefresh())
            {
                gpu += " (displej zvláda " + std::to_string(inv.displayMaxRefreshHz) + " Hz)";
            }
        }
        ImGui::TextWrapped("%s", gpu.c_str());
        ImGui::PopStyleColor();

        ImGui::EndChild();
        ImGui::Dummy(ImVec2(0, 8));
    }

    // No real temperature sensor available (see SystemStats.h), so this is
    // a load-based proxy: sustained high CPU/GPU on a laptop (heuristically
    // detected — see SystemInfo.h) is a reasonable, honestly-framed signal
    // that thermal throttling/overheating risk is elevated.
    if (m_isLaptop && m_overheatWarning && m_historyCount >= 10)
    {
        int window = std::min(m_historyCount, 60);
        double sumCpu = 0.0, sumGpu = 0.0;
        for (int i = 0; i < window; i++)
        {
            int idx = (m_historyWritePos - 1 - i + kHistoryLen) % kHistoryLen;
            sumCpu += m_cpuHistory[idx];
            sumGpu += m_gpuHistory[idx];
        }
        float avgCpu = (float)(sumCpu / window);
        float avgGpu = (float)(sumGpu / window);
        if (avgCpu > 85.0f || avgGpu > 85.0f)
        {
            ImGui::Dummy(ImVec2(0, 10));
            ImGui::PushStyleColor(ImGuiCol_Text, NasakiColors::Danger());
            ImGui::TextWrapped(
                "Notebook je dlhší čas pod vysokou záťažou — skontroluj vetranie, "
                "aby nedošlo k prehriatiu.");
            ImGui::PopStyleColor();
        }
    }

    ImGui::Dummy(ImVec2(0, 16));

    const float tileGap = 16.0f;
    float tileWidth = (ImGui::GetContentRegionAvail().x - tileGap * 2) / 3.0f;
    DrawStatTile("CPU", FormatPercent(m_latestSnapshot.cpuPercent), tileWidth);
    ImGui::SameLine(0, tileGap);
    DrawStatTile("GPU", FormatPercent(m_latestSnapshot.gpuPercent), tileWidth);
    ImGui::SameLine(0, tileGap);
    DrawStatTile("RAM", FormatPercent(m_latestSnapshot.ramPercent), tileWidth);

    ImGui::Dummy(ImVec2(0, 16));

    // Height must clear style.WindowPadding (20px top+bottom, applied to
    // child windows same as regular ones) plus one text row, one input row,
    // and (when active) a status line, or content silently scrolls inside
    // this small box.
    ImGui::BeginChild("SessionPanel", ImVec2(0, 118), true);
    ImGui::TextUnformatted("Session");
    ImGui::Dummy(ImVec2(0, 8));

    if (m_boostPhase == BoostPhase::CountingDown)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, NasakiColors::Accent2());
        ImGui::Text("Prepni sa do hry... spúšťam o %ds", (int)(m_boostCountdown + 0.999f));
        ImGui::PopStyleColor();
        if (ImGui::Button("Zrušiť"))
        {
            CancelStartRequest();
        }
    }
    else if (m_sessionActive || m_boostPhase == BoostPhase::Active)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, NasakiColors::Ok());
        ImGui::Text("Aktívna: %s", m_gameNameInput);
        ImGui::PopStyleColor();
        if (!m_boostStatus.empty())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, NasakiColors::InkDim());
            ImGui::TextWrapped("%s", m_boostStatus.c_str());
            ImGui::PopStyleColor();
        }
        if (ImGui::Button("Ukončiť session"))
        {
            StopSession();
        }
    }
    else
    {
        ImGui::SetNextItemWidth(260);
        ImGui::InputText("##gamename", m_gameNameInput, sizeof(m_gameNameInput));
        ImGui::SameLine();
        if (NasakiUI::GradientButton("Spustiť session", ImVec2(180, 38)))
        {
            RequestStartSession();
        }
    }
    ImGui::EndChild();

    // Charts live in the Výkon view — the dashboard is deliberately just
    // "how is the machine right now" plus the session control.
}

void App::DrawLoadChart(float height)
{
    if (m_historyCount <= 1)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, NasakiColors::InkDim());
        ImGui::TextUnformatted("Zbieram dáta...");
        ImGui::PopStyleColor();
        return;
    }

    const NasakiUI::ChartSeries series[] = {
        { "CPU", m_cpuHistory, NasakiColors::U32(NasakiColors::Accent()) },
        { "GPU", m_gpuHistory, NasakiColors::U32(NasakiColors::Accent2()) },
        { "RAM", m_ramHistory, NasakiColors::U32(NasakiColors::Ok()) },
    };
    NasakiUI::MultiAreaChart(
        ImGui::GetCursorScreenPos(),
        ImVec2(ImGui::GetContentRegionAvail().x, height),
        "Záťaž (posledné ~2 min)",
        series, 3, m_historyCount, m_historyWritePos, 0.0f, 100.0f);
}

void App::DrawPerformanceView()
{
    DrawPageTitle("Výkon", "Záťaž tohto počítača v reálnom čase.");
    ImGui::Dummy(ImVec2(0, 18));

    DrawLoadChart(260.0f);
    ImGui::Dummy(ImVec2(0, 16));

    ImGui::BeginChild("PerfNote", ImVec2(0, 92), true);
    ImGui::PushStyleColor(ImGuiCol_Text, NasakiColors::InkDim());
    ImGui::TextWrapped(
        ICON_CHECK "  Plná história session-ov (FPS, 1%% low, teploty) je na "
        "nasaki.eu/account/performance.php.");
    ImGui::PopStyleColor();
    ImGui::EndChild();
}

void App::DrawPageTitle(const char* title, const char* subtitle)
{
    ImGui::PushFont(NasakiFonts::Title());
    ImGui::TextUnformatted(title);
    ImGui::PopFont();
    if (subtitle && *subtitle)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, NasakiColors::InkDim());
        ImGui::TextUnformatted(subtitle);
        ImGui::PopStyleColor();
    }
}

void App::DrawOptimizationsView()
{
    DrawPageTitle("Optimalizácie", "Overené, vratné nastavenia Windows. Každá zmena si pamätá pôvodnú hodnotu.");
    ImGui::Dummy(ImVec2(0, 18));

    // Tab 0 filters by classification rather than category: "what should I
    // change on this machine" is the question most users actually have, and
    // it is answered from the detected hardware, not from the setting list.
    static const char* kTabs[] = {
        "Odporúčané", "Všetko", "Všeobecné", "Hranie", "Súkromie", "Štart", "Úložisko"
    };
    static const optim::Category kTabCategory[] = {
        optim::Category::General, // unused (index 0 = classification filter)
        optim::Category::General, // unused (index 1 = everything)
        optim::Category::General,
        optim::Category::Gaming,
        optim::Category::Privacy,
        optim::Category::Startup,
        optim::Category::Storage,
    };
    const int kTabCount = (int)(sizeof(kTabs) / sizeof(kTabs[0]));

    int clickedTab = NasakiUI::TabBar("opttabs", kTabs, kTabCount, m_optCategoryTab);
    if (clickedTab >= 0) m_optCategoryTab = clickedTab;
    ImGui::Dummy(ImVec2(0, 14));

    DrawProfileStrip();

    NasakiUI::SearchField("##optsearch", "Hľadať nastavenie...", m_optSearch, sizeof(m_optSearch), 280.0f);
    ImGui::SameLine(0, 12);
    ImGui::SetNextItemWidth(190.0f);
    const char* sortLabels[] = { "Zoradiť: kategória", "Zoradiť: názov", "Zoradiť: použité" };
    ImGui::Combo("##optsort", &m_optSort, sortLabels, 3);
    ImGui::SameLine(0, 12);
    if (ImGui::Button(ICON_ROTATE "  Znova zistiť", ImVec2(0, 38)))
    {
        m_optimizations.RefreshAsync();
    }
    ImGui::SameLine(0, 12);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10);
    ImGui::PushStyleColor(ImGuiCol_Text, NasakiColors::InkDim());
    if (m_optimizations.Scanning())
    {
        ImGui::TextUnformatted("Zisťujem aktuálny stav...");
    }
    else
    {
        int recommended = m_optimizations.RecommendedCount();
        if (recommended > 0)
        {
            ImGui::Text("%d použitých  •  %d odporúčaných pre tento počítač",
                m_optimizations.AppliedCount(), recommended);
        }
        else
        {
            ImGui::Text("%d použitých  •  nič ďalšie tu neodporúčame",
                m_optimizations.AppliedCount());
        }
    }
    ImGui::PopStyleColor();

    if (std::optional<optim::Service::Outcome> outcome = m_optimizations.LastOutcome())
    {
        ImGui::Dummy(ImVec2(0, 10));
        ImGui::PushStyleColor(ImGuiCol_Text,
            outcome->success ? NasakiColors::Ok() : NasakiColors::Danger());
        ImGui::TextWrapped("%s", outcome->message.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::Dummy(ImVec2(0, 16));

    // Filter and sort into a view list; the service's own order never changes.
    std::string needle = m_optSearch;
    std::transform(needle.begin(), needle.end(), needle.begin(),
        [](unsigned char c) { return (char)std::tolower(c); });

    std::vector<optim::Service::Row> rows = m_optimizations.Rows();
    std::vector<size_t> visible;
    visible.reserve(rows.size());
    for (size_t i = 0; i < rows.size(); i++)
    {
        if (m_optCategoryTab == 0)
        {
            // Only entries this machine is actually advised to change, and
            // only while they aren't already in place.
            if (rows[i].info->classification != optim::Classification::Recommended ||
                rows[i].status.state == optim::State::Applied ||
                rows[i].status.state == optim::State::Unsupported)
            {
                continue;
            }
        }
        else if (m_optCategoryTab != 1 && rows[i].info->category != kTabCategory[m_optCategoryTab])
        {
            continue;
        }
        if (!needle.empty())
        {
            std::string haystack = rows[i].info->title + " " + rows[i].info->description;
            std::transform(haystack.begin(), haystack.end(), haystack.begin(),
                [](unsigned char c) { return (char)std::tolower(c); });
            if (haystack.find(needle) == std::string::npos) continue;
        }
        visible.push_back(i);
    }

    std::sort(visible.begin(), visible.end(), [&](size_t a, size_t b) {
        if (m_optSort == 1) return rows[a].info->title < rows[b].info->title;
        if (m_optSort == 2)
        {
            bool aApplied = rows[a].status.state == optim::State::Applied;
            bool bApplied = rows[b].status.state == optim::State::Applied;
            if (aApplied != bApplied) return aApplied;
            return rows[a].info->title < rows[b].info->title;
        }
        // Default order leads with what this machine is advised to change,
        // so the useful entries aren't buried under the merely available.
        int aRank = (int)rows[a].info->classification;
        int bRank = (int)rows[b].info->classification;
        if (aRank != bRank) return aRank < bRank;
        if (rows[a].info->category != rows[b].info->category)
        {
            return (int)rows[a].info->category < (int)rows[b].info->category;
        }
        return rows[a].info->title < rows[b].info->title;
    });

    if (visible.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, NasakiColors::InkDim());
        ImGui::TextUnformatted(m_optimizations.Scanning()
            ? "Načítavam..."
            : "Nič nezodpovedá filtru.");
        ImGui::PopStyleColor();
        return;
    }

    const float gap = 16.0f;
    const float avail = ImGui::GetContentRegionAvail().x;
    int columns = (int)((avail + gap) / (340.0f + gap));
    if (columns < 1) columns = 1;
    if (columns > 3) columns = 3;
    const float cardWidth = (avail - gap * (columns - 1)) / columns;

    // Rows can differ in height when one card is expanded, so track each
    // row's tallest card rather than assuming a uniform grid.
    ImVec2 gridOrigin = ImGui::GetCursorPos();
    float y = 0.0f;
    float totalHeight = 0.0f;
    for (size_t slot = 0; slot < visible.size(); )
    {
        float rowHeight = 0.0f;
        for (int col = 0; col < columns && slot + col < visible.size(); col++)
        {
            bool expanded = rows[visible[slot + col]].info->id == m_expandedOptId;
            rowHeight = (std::max)(rowHeight, NasakiUI::OptCardHeight(expanded));
        }

        for (int col = 0; col < columns && slot < visible.size(); col++, slot++)
        {
            const optim::Service::Row& row = rows[visible[slot]];
            bool expanded = row.info->id == m_expandedOptId;

            float t = (m_viewFade - (float)slot * 0.035f) / 0.35f;
            if (t < 0.0f) t = 0.0f;
            if (t > 1.0f) t = 1.0f;

            ImGui::SetCursorPos(ImVec2(
                gridOrigin.x + col * (cardWidth + gap),
                gridOrigin.y + y + (1.0f - t) * 10.0f));

            NasakiUI::OptCardModel model;
            model.id = row.info->id.c_str();
            model.title = row.info->title.c_str();
            model.description = row.info->description.c_str();
            model.rationale = row.info->rationale.c_str();
            model.benefit = optim::BenefitLabel(row.info->benefit);
            model.evidence = optim::EvidenceLabel(row.info->evidence);
            model.tradeoffs = row.info->tradeoffs.c_str();
            model.changeSummary = row.info->changeSummary.c_str();
            model.classification = optim::ClassificationLabel(row.info->classification);
            model.classificationReason = row.info->classificationReason.c_str();
            model.recommended = row.info->classification == optim::Classification::Recommended;
            model.stateDetail = row.status.detail.c_str();
            model.errorMessage = row.status.lastError.message.c_str();
            model.requiresAdmin = row.info->requiresAdmin;
            model.requiresRestart = row.info->requiresRestart;
            model.hasBackup = row.hasBackup;
            model.busy = row.busy;

            switch (row.status.state)
            {
            case optim::State::Applied:        model.state = NasakiUI::OptState::Applied; break;
            case optim::State::NotApplied:     model.state = NasakiUI::OptState::NotApplied; break;
            case optim::State::Unsupported:    model.state = NasakiUI::OptState::Unsupported; break;
            case optim::State::PendingRestart: model.state = NasakiUI::OptState::PendingRestart; break;
            case optim::State::Failed:         model.state = NasakiUI::OptState::Failed; break;
            case optim::State::Manual:         model.state = NasakiUI::OptState::Manual; break;
            default:                           model.state = NasakiUI::OptState::Unknown; break;
            }

            switch (NasakiUI::OptCard(model, cardWidth, expanded, t))
            {
            case NasakiUI::OptCardAction::Apply:
                m_optimizations.ApplyAsync(row.info->id);
                break;
            case NasakiUI::OptCardAction::Restore:
                m_optimizations.RestoreAsync(row.info->id);
                break;
            case NasakiUI::OptCardAction::OpenSettings:
                // Reuses ApplyAsync deliberately: for a manual entry "apply"
                // is defined as opening the Settings page, and routing it
                // through the service keeps the outcome line and the history
                // journal accurate about what was actually done.
                m_optimizations.ApplyAsync(row.info->id);
                break;
            case NasakiUI::OptCardAction::ToggleDetails:
                m_expandedOptId = expanded ? std::string() : row.info->id;
                break;
            default:
                break;
            }
        }

        y += rowHeight + gap;
        totalHeight = y;
    }

    // Claim the grid's footprint with a real item so the parent's content
    // region grows (a bare cursor move would not).
    ImGui::SetCursorPos(gridOrigin);
    ImGui::Dummy(ImVec2(avail, totalHeight > gap ? totalHeight - gap : totalHeight));
}

void App::DrawStartupView()
{
    DrawPageTitle("Po spustení",
        "Programy, ktoré štartujú s Windows. Odstránenie si pamätá pôvodný príkaz "
        "a vieš ho kedykoľvek vrátiť.");
    ImGui::Dummy(ImVec2(0, 18));

    if (ImGui::Button(ICON_ROTATE "  Znova načítať", ImVec2(0, 38)))
    {
        m_optimizations.RefreshStartupAsync();
    }
    ImGui::SameLine(0, 12);
    if (ImGui::Button("Otvoriť v nastaveniach Windows", ImVec2(0, 38)))
    {
        optim::StartupManager::OpenWindowsStartupSettings();
    }

    std::vector<optim::StartupEntry> entries = m_optimizations.StartupPrograms();

    int removable = 0;
    int removed = 0;
    for (const optim::StartupEntry& entry : entries)
    {
        if (entry.removed) removed++;
        else if (entry.removable) removable++;
    }

    ImGui::SameLine(0, 12);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10);
    ImGui::PushStyleColor(ImGuiCol_Text, NasakiColors::InkDim());
    if (m_optimizations.StartupBusy())
    {
        ImGui::TextUnformatted("Načítavam...");
    }
    else
    {
        ImGui::Text("%d položiek  •  %d vieme odstrániť  •  %d odstránených",
            (int)entries.size() - removed, removable, removed);
    }
    ImGui::PopStyleColor();

    if (std::optional<optim::Service::Outcome> outcome = m_optimizations.LastOutcome())
    {
        if (outcome->action.rfind("startup-", 0) == 0)
        {
            ImGui::Dummy(ImVec2(0, 10));
            ImGui::PushStyleColor(ImGuiCol_Text,
                outcome->success ? NasakiColors::Ok() : NasakiColors::Danger());
            ImGui::TextWrapped("%s", outcome->message.c_str());
            ImGui::PopStyleColor();
        }
    }

    ImGui::Dummy(ImVec2(0, 8));
    ImGui::PushStyleColor(ImGuiCol_Text, NasakiColors::InkFaint());
    ImGui::TextWrapped(
        "Nasaki odstraňuje len položky z tvojho používateľského účtu a vždy si uloží presné "
        "pôvodné znenie. Položky pre všetkých používateľov a priečinok Po spustení iba "
        "zobrazujeme — mení ich sám Windows. Nikdy nič nevypíname hromadne.");
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0, 16));

    if (entries.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, NasakiColors::InkDim());
        ImGui::TextUnformatted(m_optimizations.StartupBusy()
            ? "Načítavam..."
            : "Nenašli sme žiadne programy spúšťané s Windows.");
        ImGui::PopStyleColor();
        return;
    }

    const float width = ImGui::GetContentRegionAvail().x;
    const float rowHeight = 74.0f;
    const float pad = 16.0f;
    ImVec2 origin = ImGui::GetCursorPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    for (size_t i = 0; i < entries.size(); i++)
    {
        const optim::StartupEntry& entry = entries[i];

        float t = (m_viewFade - (float)i * 0.03f) / 0.35f;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;

        ImGui::SetCursorPos(ImVec2(origin.x, origin.y + i * (rowHeight + 8.0f) + (1.0f - t) * 8.0f));
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImVec2 p1(p0.x + width, p0.y + rowHeight);

        ImGui::PushID((int)i);
        ImGui::InvisibleButton("row", ImVec2(width, rowHeight));
        bool hovered = ImGui::IsItemHovered();
        ImVec2 after = ImGui::GetCursorPos();

        ImU32 bg = hovered ? IM_COL32(27, 23, 44, 255) : IM_COL32(20, 17, 34, 255);
        dl->AddRectFilled(p0, p1, ImGui::GetColorU32(bg), 12.0f);
        dl->AddRect(p0, p1, ImGui::GetColorU32(
            entry.removed ? IM_COL32(245, 177, 76, 90) : IM_COL32(42, 36, 64, 255)), 12.0f, 0, 1.0f);

        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * t);

        ImGui::SetCursorScreenPos(ImVec2(p0.x + pad, p0.y + 13.0f));
        ImGui::PushFont(NasakiFonts::Heading());
        ImGui::TextUnformatted(entry.name.c_str());
        ImGui::PopFont();

        // Second line carries the executable and where the entry lives, which
        // is what tells the user whether removing it is safe.
        ImGui::SetCursorScreenPos(ImVec2(p0.x + pad, p0.y + 40.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, NasakiColors::InkFaint());
        ImGui::Text("%s  •  %s", entry.executable.empty() ? "?" : entry.executable.c_str(),
            entry.location.c_str());
        ImGui::PopStyleColor();

        if (ImGui::IsMouseHoveringRect(p0, p1) && !entry.command.empty())
        {
            ImGui::SetTooltip("%s", entry.command.c_str());
        }

        // Action on the right.
        const float btnW = 150.0f;
        ImGui::SetCursorScreenPos(ImVec2(p1.x - pad - btnW, p0.y + (rowHeight - 34.0f) * 0.5f));
        if (entry.removed)
        {
            if (ImGui::Button("Vrátiť späť", ImVec2(btnW, 34.0f)))
            {
                m_optimizations.RestoreStartupAsync(entry.id);
            }
        }
        else if (entry.removable)
        {
            if (ImGui::Button("Odstrániť", ImVec2(btnW, 34.0f)))
            {
                m_optimizations.RemoveStartupAsync(entry.id);
            }
        }
        else
        {
            // No button that would do nothing: this entry is Windows's to
            // manage, and we say so.
            ImGui::PushStyleColor(ImGuiCol_Text, NasakiColors::InkFaint());
            ImGui::SetCursorScreenPos(ImVec2(p1.x - pad - btnW, p0.y + (rowHeight - 18.0f) * 0.5f));
            ImGui::TextUnformatted(entry.needsAdmin ? "Vyžaduje správcu" : "Spravuje Windows");
            ImGui::PopStyleColor();
        }

        ImGui::PopStyleVar();
        ImGui::PopID();
        ImGui::SetCursorPos(after);
    }

    // Claim the grid's footprint so ImGui doesn't warn about the cursor
    // moves above extending the window.
    ImGui::SetCursorPos(origin);
    ImGui::Dummy(ImVec2(width, entries.size() * (rowHeight + 8.0f)));
}

void App::DrawPowerView()
{
    DrawPageTitle("Napájanie",
        "Plány napájania Windows. Prepíname celé plány — jednotlivé parametre procesora "
        "nechávame na systéme.");
    ImGui::Dummy(ImVec2(0, 18));

    // Guidance comes from the detected machine: the same plan is sensible
    // advice on a desktop and a bad idea on a laptop running on battery.
    std::string guidance = m_optimizations.PowerGuidance();
    if (!guidance.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text,
            m_isLaptop ? NasakiColors::Warn() : NasakiColors::InkDim());
        ImGui::TextWrapped(ICON_THERMO "  %s", guidance.c_str());
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, 14));
    }

    if (ImGui::Button(ICON_ROTATE "  Znova načítať", ImVec2(0, 38)))
    {
        m_optimizations.RefreshPowerPlansAsync();
    }
    if (m_optimizations.HasOriginalPowerPlan())
    {
        ImGui::SameLine(0, 12);
        std::string label = "Vrátiť pôvodný (" + m_optimizations.OriginalPowerPlanName() + ")";
        if (ImGui::Button(label.c_str(), ImVec2(0, 38)))
        {
            m_optimizations.RestorePowerPlanAsync();
        }
    }
    ImGui::SameLine(0, 12);
    if (ImGui::Button("Otvoriť nastavenia napájania", ImVec2(0, 38)))
    {
        ShellExecuteW(nullptr, L"open", L"ms-settings:powersleep", nullptr, nullptr, SW_SHOWNORMAL);
    }

    if (std::optional<optim::Service::Outcome> outcome = m_optimizations.LastOutcome())
    {
        if (outcome->action.rfind("power-", 0) == 0)
        {
            ImGui::Dummy(ImVec2(0, 10));
            ImGui::PushStyleColor(ImGuiCol_Text,
                outcome->success ? NasakiColors::Ok() : NasakiColors::Danger());
            ImGui::TextWrapped("%s", outcome->message.c_str());
            ImGui::PopStyleColor();
        }
    }
    ImGui::Dummy(ImVec2(0, 16));

    std::vector<optim::PowerPlan> plans = m_optimizations.PowerPlans();
    if (plans.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, NasakiColors::InkDim());
        ImGui::TextWrapped("%s", m_optimizations.PowerBusy()
            ? "Načítavam plány napájania..."
            : "Windows nevrátil žiadne plány napájania. Na zariadeniach s Moderným pohotovostným "
              "režimom môže byť zoznam prázdny a režim sa nastavuje priamo v Nastaveniach.");
        ImGui::PopStyleColor();
        return;
    }

    const float width = ImGui::GetContentRegionAvail().x;
    const float pad = 18.0f;
    ImVec2 origin = ImGui::GetCursorPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float y = 0.0f;

    for (size_t i = 0; i < plans.size(); i++)
    {
        const optim::PowerPlan& plan = plans[i];
        const float height = plan.note.empty() ? 78.0f : 100.0f;

        float t = (m_viewFade - (float)i * 0.04f) / 0.35f;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;

        ImGui::SetCursorPos(ImVec2(origin.x, origin.y + y + (1.0f - t) * 8.0f));
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImVec2 p1(p0.x + width, p0.y + height);

        ImGui::PushID((int)i);
        ImGui::InvisibleButton("plan", ImVec2(width, height));
        bool hovered = ImGui::IsItemHovered();
        ImVec2 after = ImGui::GetCursorPos();

        dl->AddRectFilled(p0, p1, ImGui::GetColorU32(
            hovered ? IM_COL32(27, 23, 44, 255) : IM_COL32(20, 17, 34, 255)), 12.0f);
        dl->AddRect(p0, p1, ImGui::GetColorU32(
            plan.active ? IM_COL32(139, 92, 246, 170) : IM_COL32(42, 36, 64, 255)), 12.0f, 0, 1.0f);

        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * t);

        ImGui::SetCursorScreenPos(ImVec2(p0.x + pad, p0.y + 14.0f));
        ImGui::PushFont(NasakiFonts::Heading());
        ImGui::TextUnformatted(plan.name.c_str());
        ImGui::PopFont();

        if (plan.active)
        {
            ImVec2 size = ImGui::CalcTextSize("Aktívny");
            NasakiUI::BadgeAt(dl, ImVec2(p1.x - pad - (size.x + 18.0f), p0.y + 16.0f), "Aktívny",
                ImGui::GetColorU32(IM_COL32(167, 139, 250, 255)),
                ImGui::GetColorU32(IM_COL32(139, 92, 246, 40)));
        }

        // The note is ours and only exists for the plans Windows ships; the
        // description is whatever Windows itself reports.
        const std::string& line = plan.note.empty() ? plan.description : plan.note;
        if (!line.empty())
        {
            ImGui::SetCursorScreenPos(ImVec2(p0.x + pad, p0.y + 42.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, NasakiColors::InkFaint());
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width - pad * 2 - 170.0f);
            ImGui::TextUnformatted(line.c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
        }

        if (!plan.active)
        {
            ImGui::SetCursorScreenPos(ImVec2(p1.x - pad - 150.0f, p1.y - pad - 26.0f));
            if (ImGui::Button("Aktivovať", ImVec2(150.0f, 34.0f)))
            {
                m_optimizations.ActivatePowerPlanAsync(plan.guid);
            }
        }

        ImGui::PopStyleVar();
        ImGui::PopID();
        ImGui::SetCursorPos(after);
        y += height + 8.0f;
    }

    ImGui::SetCursorPos(origin);
    ImGui::Dummy(ImVec2(width, y));
}

void App::DrawStorageView()
{
    DrawPageTitle("Úložisko",
        "Nasaki maže len to, čo ti najprv ukáže. Osobné priečinky iba meriame.");
    ImGui::Dummy(ImVec2(0, 18));

    // Free space per fixed drive, straight from the inventory.
    const optim::SystemInventory& inventory = m_optimizations.Inventory();
    for (const optim::StorageInfo& drive : inventory.drives)
    {
        if (drive.totalBytes == 0) continue;

        float used = 1.0f - (float)((double)drive.freeBytes / (double)drive.totalBytes);
        ImGui::Text("%s  %s voľných z %s", drive.driveLetter.c_str(),
            optim::FormatBytes(drive.freeBytes).c_str(),
            optim::FormatBytes(drive.totalBytes).c_str());
        ImGui::SameLine(0, 12);
        ImGui::PushStyleColor(ImGuiCol_Text, NasakiColors::InkFaint());
        if (drive.isSolidState.has_value())
        {
            ImGui::TextUnformatted(*drive.isSolidState ? "SSD" : "Pevný disk");
        }
        else
        {
            ImGui::TextUnformatted("typ neznámy");
        }
        ImGui::PopStyleColor();

        ImGui::PushStyleColor(ImGuiCol_PlotHistogram,
            used > 0.9f ? NasakiColors::Danger() : NasakiColors::Accent());
        ImGui::ProgressBar(used, ImVec2(-1.0f, 8.0f), "");
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, 6));
    }

    ImGui::Dummy(ImVec2(0, 10));
    if (ImGui::Button(ICON_ROTATE "  Prepočítať", ImVec2(0, 38)))
    {
        m_optimizations.RefreshStorageAsync();
    }
    ImGui::SameLine(0, 12);
    if (ImGui::Button("Čistenie disku (Windows)", ImVec2(0, 38)))
    {
        // The system caches need elevation and belong to Windows' own tool.
        optim::StorageCleaner::OpenWindowsDiskCleanup();
    }
    ImGui::SameLine(0, 12);
    if (ImGui::Button("Nastavenia úložiska", ImVec2(0, 38)))
    {
        optim::StorageCleaner::OpenStorageSettings();
    }
    ImGui::SameLine(0, 12);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10);
    ImGui::PushStyleColor(ImGuiCol_Text, NasakiColors::InkDim());
    ImGui::TextUnformatted(m_optimizations.StorageBusy() ? "Počítam..." : "");
    ImGui::PopStyleColor();

    if (std::optional<optim::Service::Outcome> outcome = m_optimizations.LastOutcome())
    {
        if (outcome->action == "storage-clean")
        {
            ImGui::Dummy(ImVec2(0, 10));
            ImGui::PushStyleColor(ImGuiCol_Text,
                outcome->success ? NasakiColors::Ok() : NasakiColors::Danger());
            ImGui::TextWrapped("%s", outcome->message.c_str());
            ImGui::PopStyleColor();
        }
    }
    ImGui::Dummy(ImVec2(0, 16));

    std::vector<optim::CleanupTarget> targets = m_optimizations.StorageTargets();
    optim::Service::StoragePreview preview = m_optimizations.CurrentStoragePreview();

    if (targets.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, NasakiColors::InkDim());
        ImGui::TextUnformatted(m_optimizations.StorageBusy()
            ? "Prehľadávam priečinky..."
            : "Stlač Prepočítať a pozrieme sa, kde je miesto.");
        ImGui::PopStyleColor();
        return;
    }

    for (size_t i = 0; i < targets.size(); i++)
    {
        const optim::CleanupTarget& target = targets[i];
        bool showingPreview = preview.targetId == target.id && !preview.lines.empty();

        ImGui::PushID((int)i);
        ImGui::BeginChild("target", ImVec2(0, showingPreview ? 320.0f : 150.0f), true);

        ImGui::PushFont(NasakiFonts::Heading());
        ImGui::TextUnformatted(target.name.c_str());
        ImGui::PopFont();

        ImGui::PushStyleColor(ImGuiCol_Text, NasakiColors::InkDim());
        ImGui::TextWrapped("%s", target.description.c_str());
        ImGui::PopStyleColor();

        ImGui::PushStyleColor(ImGuiCol_Text, NasakiColors::InkFaint());
        if (target.deletable)
        {
            ImGui::Text("%s zaberá  •  %s sa dá uvoľniť (%d súborov)  •  %s",
                optim::FormatBytes(target.bytes).c_str(),
                optim::FormatBytes(target.deletableBytes).c_str(),
                target.deletableFileCount,
                target.path.c_str());
        }
        else
        {
            ImGui::Text("%s v %d súboroch  •  %s", optim::FormatBytes(target.bytes).c_str(),
                target.fileCount, target.path.c_str());
        }
        ImGui::PopStyleColor();

        if (!target.caution.empty())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, NasakiColors::Warn());
            ImGui::TextWrapped(ICON_WARNING "  %s", target.caution.c_str());
            ImGui::PopStyleColor();
        }

        ImGui::Dummy(ImVec2(0, 6));

        if (!target.deletable)
        {
            // Measured only. No button, because there is no action we would
            // take on a personal folder.
            ImGui::PushStyleColor(ImGuiCol_Text, NasakiColors::InkFaint());
            ImGui::TextUnformatted("Nasaki tu nemaže nič — otvor si priečinok a rozhodni sám.");
            ImGui::PopStyleColor();
        }
        else if (target.deletableFileCount == 0 && target.deletableBytes == 0)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, NasakiColors::InkFaint());
            ImGui::TextUnformatted("Nie je čo uvoľniť.");
            ImGui::PopStyleColor();
        }
        else if (!showingPreview)
        {
            if (ImGui::Button("Ukázať, čo sa zmaže", ImVec2(220.0f, 34.0f)))
            {
                m_optimizations.RequestStoragePreviewAsync(target.id);
            }
        }
        else
        {
            // Delete is only reachable after the list has been shown for
            // this specific target.
            ImGui::BeginChild("preview", ImVec2(0, 150.0f), true);
            for (const std::string& line : preview.lines)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, NasakiColors::InkFaint());
                ImGui::TextUnformatted(line.c_str());
                ImGui::PopStyleColor();
            }
            if (preview.truncated)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, NasakiColors::InkDim());
                ImGui::TextUnformatted("... a ďalšie. Zoznam je skrátený, zmaže sa všetko uvedené vyššie aj zvyšok.");
                ImGui::PopStyleColor();
            }
            ImGui::EndChild();

            ImGui::Dummy(ImVec2(0, 6));
            if (ImGui::Button("Áno, vymazať", ImVec2(180.0f, 34.0f)))
            {
                m_optimizations.CleanStorageAsync(target.id);
            }
            ImGui::SameLine(0, 10);
            if (ImGui::Button("Zrušiť", ImVec2(120.0f, 34.0f)))
            {
                m_optimizations.ClearStoragePreview();
            }
        }

        ImGui::EndChild();
        ImGui::PopID();
        ImGui::Dummy(ImVec2(0, 10));
    }
}

void App::DrawProfileStrip()
{
    const std::vector<optim::Profile>& profiles = m_optimizations.Profiles();
    if (profiles.empty()) return;

    NasakiUI::SectionLabel("PROFILY");
    ImGui::Dummy(ImVec2(0, 6));
    ImGui::PushStyleColor(ImGuiCol_Text, NasakiColors::InkFaint());
    ImGui::TextWrapped(
        "Profil je len pomenovaná skupina nastavení nižšie. Nič navyše nerobí a každé "
        "nastavenie sa použije samostatne, so svojou vlastnou zálohou.");
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0, 8));

    for (size_t i = 0; i < profiles.size(); i++)
    {
        if (i > 0) ImGui::SameLine(0, 10);
        ImGui::PushID((int)i);
        bool open = m_previewProfileId == profiles[i].id;
        if (ImGui::Button(profiles[i].name.c_str(), ImVec2(0, 36)))
        {
            // Selecting a profile shows what it would change; it never
            // applies anything on its own.
            m_previewProfileId = open ? std::string() : profiles[i].id;
        }
        ImGui::PopID();
    }

    if (!m_previewProfileId.empty())
    {
        const optim::Profile* selected = nullptr;
        for (const optim::Profile& profile : profiles)
        {
            if (profile.id == m_previewProfileId) selected = &profile;
        }

        if (selected)
        {
            ImGui::Dummy(ImVec2(0, 10));
            ImGui::BeginChild("profilepreview", ImVec2(0, 260.0f), true);

            ImGui::PushFont(NasakiFonts::Heading());
            ImGui::TextUnformatted(selected->name.c_str());
            ImGui::PopFont();

            ImGui::PushStyleColor(ImGuiCol_Text, NasakiColors::InkDim());
            ImGui::TextWrapped("%s", selected->description.c_str());
            ImGui::PopStyleColor();
            ImGui::PushStyleColor(ImGuiCol_Text, NasakiColors::InkFaint());
            ImGui::TextWrapped("%s", selected->suitedFor.c_str());
            ImGui::PopStyleColor();
            ImGui::Dummy(ImVec2(0, 8));

            std::vector<optim::Service::ProfileStep> steps =
                m_optimizations.PreviewProfile(m_previewProfileId);

            int applicable = 0;
            ImGui::BeginChild("steps", ImVec2(0, 120.0f), true);
            for (const optim::Service::ProfileStep& step : steps)
            {
                const char* status = "sa zmení";
                ImVec4 color = NasakiColors::InkDim();
                if (!step.supported)
                {
                    status = "nedostupné na tomto systéme";
                    color = NasakiColors::InkFaint();
                }
                else if (step.currentState == optim::State::Applied)
                {
                    status = "už je použité";
                    color = NasakiColors::Ok();
                }
                else
                {
                    applicable++;
                }

                ImGui::PushStyleColor(ImGuiCol_Text, color);
                ImGui::TextWrapped("%s — %s (%s)", step.title.c_str(), status,
                    step.changeSummary.c_str());
                ImGui::PopStyleColor();
            }
            ImGui::EndChild();

            ImGui::Dummy(ImVec2(0, 6));
            if (applicable == 0)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, NasakiColors::InkFaint());
                ImGui::TextUnformatted("Tento profil už nemá čo zmeniť.");
                ImGui::PopStyleColor();
            }
            else
            {
                std::string label = "Použiť " + std::to_string(applicable) + " nastavení";
                if (ImGui::Button(label.c_str(), ImVec2(240.0f, 36.0f)))
                {
                    m_optimizations.ApplyProfileAsync(m_previewProfileId);
                }
            }
            ImGui::SameLine(0, 10);
            if (ImGui::Button("Zavrieť", ImVec2(120.0f, 36.0f)))
            {
                m_previewProfileId.clear();
            }

            ImGui::EndChild();
        }
    }

    ImGui::Dummy(ImVec2(0, 16));
}

void App::DrawBackupsView()
{
    DrawPageTitle("Zálohy a história",
        "Každá zmena si pred zápisom uložila pôvodnú hodnotu. Tu ich vidíš a vieš vrátiť.");
    ImGui::Dummy(ImVec2(0, 18));

    int backedUp = m_optimizations.BackedUpCount();
    ImGui::Text("%d nastavení má uloženú pôvodnú hodnotu.", backedUp);
    ImGui::Dummy(ImVec2(0, 10));

    if (backedUp > 0)
    {
        if (ImGui::Button("Vrátiť všetko späť", ImVec2(220.0f, 38.0f)))
        {
            m_optimizations.RestoreEverythingAsync();
        }
        ImGui::SameLine(0, 12);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10);
        ImGui::PushStyleColor(ImGuiCol_Text, NasakiColors::InkFaint());
        ImGui::TextUnformatted("Každé nastavenie sa vráti a overí zvlášť.");
        ImGui::PopStyleColor();
    }

    if (std::optional<optim::Service::Outcome> outcome = m_optimizations.LastOutcome())
    {
        if (outcome->action == "restore-all" || outcome->action == "profile-apply")
        {
            ImGui::Dummy(ImVec2(0, 10));
            ImGui::PushStyleColor(ImGuiCol_Text,
                outcome->success ? NasakiColors::Ok() : NasakiColors::Warn());
            ImGui::TextWrapped("%s", outcome->message.c_str());
            ImGui::PopStyleColor();
        }
    }

    ImGui::Dummy(ImVec2(0, 18));
    NasakiUI::SectionLabel("ULOŽENÉ PÔVODNÉ HODNOTY");
    ImGui::Dummy(ImVec2(0, 6));

    std::vector<optim::BackupEntry> entries = m_optimizations.BackupEntries();
    if (entries.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, NasakiColors::InkFaint());
        ImGui::TextUnformatted("Zatiaľ žiadne — Nasaki nič nezmenil.");
        ImGui::PopStyleColor();
    }
    else
    {
        ImGui::BeginChild("backups", ImVec2(0, 190.0f), true);
        for (const optim::BackupEntry& entry : entries)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, NasakiColors::InkDim());
            // "nenastavené" is the meaningful case: restoring deletes the
            // value rather than writing a zero.
            ImGui::TextWrapped("%s / %s — pôvodne %s   (%s)",
                entry.optimizationId.c_str(), entry.valueKey.c_str(),
                entry.original.existed ? "malo hodnotu" : "nenastavené",
                entry.appliedAtUtc.c_str());
            ImGui::PopStyleColor();
        }
        ImGui::EndChild();
    }

    ImGui::Dummy(ImVec2(0, 18));
    NasakiUI::SectionLabel("HISTÓRIA ZMIEN");
    ImGui::Dummy(ImVec2(0, 6));

    std::vector<optim::BackupStore::HistoryRecord> history = m_optimizations.History();
    if (history.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, NasakiColors::InkFaint());
        ImGui::TextUnformatted("Zatiaľ prázdna.");
        ImGui::PopStyleColor();
        return;
    }

    ImGui::BeginChild("history", ImVec2(0, 0), true);
    for (size_t i = history.size(); i > 0; i--)
    {
        const optim::BackupStore::HistoryRecord& record = history[i - 1];
        ImGui::PushStyleColor(ImGuiCol_Text,
            record.success ? NasakiColors::InkDim() : NasakiColors::Danger());
        ImGui::TextWrapped("%s  %s  %s — %s", record.timestampUtc.c_str(),
            record.action.c_str(), record.optimizationId.c_str(), record.message.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::EndChild();
}

void App::DrawGamesView()
{
    DrawPageTitle("Hry", "Hry nainštalované na tomto počítači.");
    ImGui::Dummy(ImVec2(0, 16));

    if (!m_gamesScanned && !m_gamesScanning)
    {
        RescanGameLibrary(); // first visit to this view kicks off the scan
    }

    NasakiUI::SearchField("##gamesearch", "Hľadať hru...", m_gameSearch, sizeof(m_gameSearch), 280.0f);
    ImGui::SameLine(0, 12);
    if (ImGui::Button(ICON_ROTATE "  Znova prehľadať", ImVec2(0, 38)))
    {
        RescanGameLibrary();
    }
    ImGui::SameLine(0, 12);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10);
    ImGui::PushStyleColor(ImGuiCol_Text, NasakiColors::InkDim());
    if (m_gamesScanning)
    {
        ImGui::TextUnformatted("Prehľadávam Steam, Epic a GOG...");
    }
    else
    {
        ImGui::Text("%d nájdených", (int)m_games.size());
    }
    ImGui::PopStyleColor();

    if (!m_gamesLaunchNote.empty())
    {
        ImGui::Dummy(ImVec2(0, 10));
        ImGui::PushStyleColor(ImGuiCol_Text, NasakiColors::Accent2());
        ImGui::TextUnformatted(m_gamesLaunchNote.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::Dummy(ImVec2(0, 16));

    // Filter by the search box (case-insensitive substring on the name).
    std::string needle = m_gameSearch;
    std::transform(needle.begin(), needle.end(), needle.begin(),
        [](unsigned char c) { return (char)std::tolower(c); });

    std::vector<size_t> visible;
    visible.reserve(m_games.size());
    for (size_t i = 0; i < m_games.size(); i++)
    {
        if (needle.empty())
        {
            visible.push_back(i);
            continue;
        }
        std::string name = m_games[i].name;
        std::transform(name.begin(), name.end(), name.begin(),
            [](unsigned char c) { return (char)std::tolower(c); });
        if (name.find(needle) != std::string::npos)
        {
            visible.push_back(i);
        }
    }

    if (m_games.empty())
    {
        if (!m_gamesScanning)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, NasakiColors::InkDim());
            ImGui::TextWrapped(
                "Nenašli sa žiadne nainštalované hry. Nasaki číta zoznam priamo zo "
                "Steamu, Epicu a GOG — ak používaš iný launcher, session vieš stále "
                "spustiť ručne v Prehľade.");
            ImGui::PopStyleColor();
        }
        return;
    }

    const float gap = 16.0f;
    const float avail = ImGui::GetContentRegionAvail().x;
    int columns = (int)((avail + gap) / (330.0f + gap));
    if (columns < 1) columns = 1;
    if (columns > 3) columns = 3;
    const float cardWidth = (avail - gap * (columns - 1)) / columns;
    const float cardHeight = NasakiUI::GameCardHeight();

    ImVec2 gridOrigin = ImGui::GetCursorPos();
    for (size_t slot = 0; slot < visible.size(); slot++)
    {
        size_t i = visible[slot];
        int row = (int)slot / columns;
        int col = (int)slot % columns;

        // Staggered entrance: each card fades and rises slightly later than
        // the one before it.
        float delay = (float)slot * 0.045f;
        float t = (m_viewFade - delay) / 0.35f;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        float rise = (1.0f - t) * 10.0f;

        ImGui::SetCursorPos(ImVec2(
            gridOrigin.x + col * (cardWidth + gap),
            gridOrigin.y + row * (cardHeight + gap) + rise));

        const InstalledGame& g = m_games[i];
        bool running = i < m_runningGamePids.size() && m_runningGamePids[i] != 0;
        std::string cardId = "game_" + std::to_string(i);
        NasakiUI::GameCardAction action = NasakiUI::GameCard(
            cardId.c_str(), g.name.c_str(), g.source.c_str(), g.installPath.c_str(),
            running, !g.launchCommand.empty(), cardWidth, t);

        if (action == NasakiUI::GameCardAction::Launch)
        {
            if (GameLibrary::Launch(g))
            {
                // The process won't exist for a second or two while the
                // launcher spins up, so let the running-state poll pick it
                // up rather than trying to boost a pid that isn't there yet.
                m_runningCheckTimer = 100.0f;
                m_gamesLaunchNote = g.name + " sa spúšťa...";
            }
            else
            {
                m_gamesLaunchNote = "Nepodarilo sa spustiť " + g.name + ".";
            }
        }
        else if (action == NasakiUI::GameCardAction::StartSession &&
                 m_boostPhase == BoostPhase::Idle && !m_sessionActive)
        {
            // Start a session boosting exactly this game's process.
            ProcessBoost::Result boost = ProcessBoost::BeginForPid(
                m_boostGamePriority ? m_runningGamePids[i] : 0, m_throttleBackground);
            strncpy_s(m_gameNameInput, g.name.c_str(), _TRUNCATE);
            m_boostStatus = BuildBoostStatus(g.name, boost.foregroundFound, boost.throttledCount);
            m_boostPhase = BoostPhase::Active;
            ApplySessionOptimizations();
            StartSession();
            SetView(AppView::Dashboard);
        }
    }

    // Claim the grid's footprint with a real item. Moving the cursor alone
    // doesn't grow the parent's content region — ImGui warns about exactly
    // that ("use SetCursorPos to extend boundaries... submit an item e.g.
    // Dummy() afterwards"), and the view would clip/not scroll.
    int rows = ((int)visible.size() + columns - 1) / columns;
    if (rows < 1) rows = 1;
    ImGui::SetCursorPos(gridOrigin);
    ImGui::Dummy(ImVec2(avail, rows * cardHeight + (rows - 1) * gap));
}

void App::DrawSettingsView()
{
    DrawPageTitle("Nastavenia", "Optimalizácie a správa tohto zariadenia.");
    ImGui::Dummy(ImVec2(0, 20));

    // Explicit grid rather than SameLine flow: SettingCard draws its text
    // through ImGui's text API (for wrapping) and restores the cursor, which
    // SameLine's previous-line bookkeeping wouldn't survive cleanly.
    struct CardDef
    {
        const char* id;
        const char* icon;
        const char* title;
        const char* description;
        bool* value;
        const char* badge;
    };

    // Applied when a session starts, reverted when it ends.
    CardDef sessionCards[] = {
        { "set_boost", ICON_BOLT, "Prioritizácia hry",
          "Hra dostane počas session-y vyššiu prioritu CPU, aby ju Windows neodsúval kvôli procesom na pozadí.",
          &m_boostGamePriority, nullptr },
        { "set_throttle", ICON_LAYERS, "Tlmenie pozadia",
          "Dočasne zníži prioritu známych aplikácií (prehliadač, Discord, Spotify) a po session-e ju vráti späť.",
          &m_throttleBackground, nullptr },
        { "set_power", ICON_POWER, "Výkonový režim",
          m_isLaptop
            ? "Prepne Windows na High performance. Na notebooku býva najväčší rozdiel — Balanced parkuje jadrá a drží nízke takty."
            : "Prepne Windows na High performance počas hrania a po skončení vráti pôvodnú schému.",
          &m_highPerformancePower, "Nové" },
    };

    // Persistent Windows settings and app behaviour.
    CardDef systemCards[] = {
        { "set_autostart_session", ICON_CROSSHAIRS, "Detekcia hry",
          "Sleduje spustené procesy a session spustí sám, keď zaznamená známu hru.",
          &m_autoStartSession, nullptr },
        { "set_overheat", ICON_THERMO, "Prehrievanie",
          m_isLaptop
            ? "Na notebooku upozorní, keď je CPU/GPU dlhší čas pod vysokou záťažou."
            : "Upozorní pri dlhodobo vysokej záťaži. Relevantné hlavne pre notebooky — tento počítač je desktop.",
          &m_overheatWarning, nullptr },
        { "set_autostart_win", ICON_POWER, "Spustiť s Windows",
          "Nasaki sa spustí automaticky po prihlásení do Windows.",
          &m_startWithWindows, nullptr },
    };

    const float gap = 16.0f;
    const float avail = ImGui::GetContentRegionAvail().x;
    int columns = (int)((avail + gap) / (340.0f + gap));
    if (columns < 1) columns = 1;
    if (columns > 3) columns = 3;
    const float cardWidth = (avail - gap * (columns - 1)) / columns;
    const float cardHeight = NasakiUI::SettingCardHeight();

    // Shared by both grids: lays cards out at explicit positions, applies
    // the staggered entrance, and claims the footprint with a real item so
    // the parent's content region grows (a bare cursor move wouldn't).
    int stagger = 0;
    auto drawGrid = [&](CardDef* cards, int count) {
        ImVec2 gridOrigin = ImGui::GetCursorPos();
        for (int i = 0; i < count; i++)
        {
            float t = (m_viewFade - (float)(stagger++) * 0.04f) / 0.35f;
            if (t < 0.0f) t = 0.0f;
            if (t > 1.0f) t = 1.0f;

            ImGui::SetCursorPos(ImVec2(
                gridOrigin.x + (i % columns) * (cardWidth + gap),
                gridOrigin.y + (i / columns) * (cardHeight + gap) + (1.0f - t) * 10.0f));

            const CardDef& c = cards[i];
            if (NasakiUI::SettingCard(c.id, c.icon, c.title, c.description, c.value, cardWidth, c.badge, t))
            {
                // The toggles backed by real system state write through the
                // moment they're flipped.
                if (c.value == &m_startWithWindows)      Autostart::SetEnabled(m_startWithWindows);
                else if (c.value == &m_highPerformancePower && !m_highPerformancePower)
                {
                    WinTweaks::EndHighPerformancePower(); // switched off mid-session
                }
            }
        }
        int rows = (count + columns - 1) / columns;
        ImGui::SetCursorPos(gridOrigin);
        ImGui::Dummy(ImVec2(avail, rows * cardHeight + (rows - 1) * gap));
    };

    ImGui::PushFont(NasakiFonts::Heading());
    ImGui::TextUnformatted("Počas hrania");
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, 10));
    drawGrid(sessionCards, (int)(sizeof(sessionCards) / sizeof(sessionCards[0])));

    ImGui::Dummy(ImVec2(0, 22));
    ImGui::PushFont(NasakiFonts::Heading());
    ImGui::TextUnformatted("Systém");
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, 10));
    drawGrid(systemCards, (int)(sizeof(systemCards) / sizeof(systemCards[0])));

    ImGui::Dummy(ImVec2(0, 12));
    ImGui::PushFont(NasakiFonts::Heading());
    ImGui::TextUnformatted("Zariadenie");
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, 10));

    ImGui::BeginChild("DevicePanel", ImVec2(0, 190), true);
    if (m_device.has_value())
    {
        ImGui::Text("Licenčný kľúč: %s", m_device->licenseKey.c_str());
        ImGui::Text("Device ID: %lld", m_device->deviceId);
    }
    ImGui::Text("Typ zariadenia: %s", m_isLaptop ? "Notebook" : "Desktop");

    ImGui::Dummy(ImVec2(0, 12));
    if (ImGui::Button("Odpojiť toto zariadenie"))
    {
        Unlink();
    }
    ImGui::PushStyleColor(ImGuiCol_Text, NasakiColors::InkDim());
    ImGui::TextWrapped(
        "Odstráni uloženú licenciu z tohto počítača. Budeš ju musieť znova aktivovať "
        "(licenciu si môžeš spravovať aj na nasaki.eu/account/devices.php).");
    ImGui::PopStyleColor();
    ImGui::EndChild();
}
