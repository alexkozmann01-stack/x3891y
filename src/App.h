#pragma once

#include <string>
#include <vector>
#include <optional>
#include <mutex>

#include "imgui.h" // ImU32, used by DrawChartRow

#include "ApiClient.h"
#include "ApiWorker.h"
#include "SystemStats.h"
#include "LicenseStore.h"

enum class AppView
{
    License,
    Dashboard,
    Performance,
    Settings,
};

// main.cpp is the only includer of this header and it needs <windows.h>
// itself anyway, so pull in the real HWND rather than risk a forward-declare
// mismatch (HWND's underlying type depends on whether STRICT is defined).
#include <windows.h>

// Owns all app state and draws the whole UI for the current frame. One
// instance, created once in main.cpp and driven from the render loop:
//   app.Update(deltaSeconds);
//   app.Draw();
class App
{
public:
    explicit App(HWND hwnd);

    void Update(float deltaSeconds);
    void Draw();

private:
    // ---- views ----
    void DrawTitleBar();
    void DrawSidebar();
    void DrawLicenseView();
    void DrawDashboardView();
    void DrawPerformanceView();
    void DrawSettingsView();
    void DrawStatTile(const char* label, const std::string& value, float width);
    void DrawChartRow(const char* label, const float* values, ImU32 lineColor, float height);

    // ---- license / device lifecycle ----
    void SubmitLicenseKey();
    void Unlink();

    // ---- session lifecycle ----
    void RequestStartSession();  // starts the pre-boost countdown
    void CancelStartRequest();   // cancels a countdown in progress
    void StartSession();         // called once the countdown completes
    void StopSession();
    void SampleTick(float deltaSeconds);

    HWND m_hwnd;
    bool m_isLaptop = false;

    AppView m_view = AppView::License;

    ApiClient m_api;
    SystemStats m_stats;

    std::optional<StoredDevice> m_device;

    // License view
    char m_licenseKeyInput[64] = "";
    bool m_activating = false;
    std::string m_licenseError;
    std::mutex m_licenseResultMutex;
    std::optional<RegisterDeviceResult> m_pendingLicenseResult; // filled by worker thread, drained on the main thread

    // Session state
    bool m_sessionActive = false;
    bool m_sessionStarting = false;
    long long m_sessionId = 0;
    char m_gameNameInput[128] = "Nasaki Client";
    std::mutex m_sessionStartMutex;
    std::optional<std::optional<long long>> m_pendingSessionStartResult;

    // There's no game-render hook to identify "the game" automatically (see
    // README), so starting a session runs a short countdown first — giving
    // the player a moment to switch to the game — before ProcessBoost reads
    // whatever process is in the foreground.
    enum class BoostPhase { Idle, CountingDown, Active };
    BoostPhase m_boostPhase = BoostPhase::Idle;
    float m_boostCountdown = 0.0f;
    bool m_throttleBackground = true;
    std::string m_boostStatus; // last outcome, shown in the session panel

    std::vector<TelemetrySample> m_sampleBuffer; // batched, flushed periodically (see SampleTick)

    double m_sessionSumCpu = 0.0, m_sessionSumGpu = 0.0, m_sessionSumRam = 0.0;
    int m_sessionSampleCount = 0;

    // Rolling ~2 minute history for the live charts.
    static constexpr int kHistoryLen = 120;
    float m_cpuHistory[kHistoryLen] = {};
    float m_gpuHistory[kHistoryLen] = {};
    float m_ramHistory[kHistoryLen] = {};
    int m_historyWritePos = 0;
    int m_historyCount = 0;

    SystemSnapshot m_latestSnapshot;
    float m_statsTickTimer = 0.0f;

    // m_worker MUST be the last member declared. Members are destroyed in
    // reverse declaration order, and ~ApiWorker() blocks until its
    // background thread has drained the job queue — declaring it last means
    // it's destroyed *first*, so the thread is guaranteed stopped before
    // m_api, m_device, the mutexes, etc. get torn down. Declared any
    // earlier, a job still in flight when the app closes could run against
    // already-destroyed members.
    ApiWorker m_worker;
};
