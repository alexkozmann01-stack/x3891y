#pragma once

#include "Optimization.h"
#include "RegistryValue.h"

#include <optional>
#include <string>

namespace optim
{
    // Some settings are worth knowing about but must not be written by a
    // third-party tool: the mechanism is undocumented, per-GPU-driver, or
    // Windows owns the value and rewrites it. Faking a toggle for those is
    // exactly the "nonfunctional control" the design forbids.
    //
    // So this kind is honest instead: it *reads* the current state where a
    // documented value exists, explains the tradeoff, and its only action is
    // opening the official Windows Settings page (an `ms-settings:` URI) so
    // the user changes it themselves. It never writes and never claims to.
    class ManualGuideOptimization : public Optimization
    {
    public:
        // `statusProbe` is optional. When set, the card shows the live value
        // read from that registry path; `describeValue` turns the DWORD into
        // the text shown ("zapnuté" / "vypnuté", etc.).
        struct Probe
        {
            RegPath path;
            std::string (*describeValue)(const std::optional<uint32_t>& value) = nullptr;
        };

        ManualGuideOptimization(Info info, std::wstring settingsUri, std::optional<Probe> probe = std::nullopt);

        const Info& info() const override { return m_info; }
        Status Read() const override;

        // Opens the Settings page. Returns Ok only if the shell actually
        // launched it — and even then the state stays Manual, because
        // opening a page is not evidence the user changed anything.
        Error Apply() override;

        // Nothing was written, so there is nothing to undo.
        Error Restore() override;

        bool Supported() const override { return true; }

    private:
        Info m_info;
        std::wstring m_settingsUri;
        std::optional<Probe> m_probe;
    };
}
