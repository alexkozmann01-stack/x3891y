#include "ManualGuide.h"

#include <windows.h>
#include <shellapi.h>

namespace optim
{
    ManualGuideOptimization::ManualGuideOptimization(Info info, std::wstring settingsUri,
                                                     std::optional<Probe> probe)
        : m_info(std::move(info)), m_settingsUri(std::move(settingsUri)), m_probe(std::move(probe))
    {
        m_info.classification = Classification::Informational;
    }

    Status ManualGuideOptimization::Read() const
    {
        Status status;
        status.state = State::Manual;

        if (m_probe.has_value())
        {
            long systemError = 0;
            RegSnapshot snapshot = reg::Read(m_probe->path, &systemError);
            std::optional<uint32_t> value;
            if (systemError == 0 && snapshot.existed && snapshot.type == REG_DWORD)
            {
                value = snapshot.dword;
            }
            if (m_probe->describeValue)
            {
                status.detail = m_probe->describeValue(value);
            }
        }

        if (status.detail.empty())
        {
            status.detail = "Nastavuje sa v systéme Windows.";
        }
        return status;
    }

    Error ManualGuideOptimization::Apply()
    {
        // ShellExecuteW returns a value > 32 on success; anything else is a
        // failure code, and we report it rather than pretending it opened.
        HINSTANCE result = ShellExecuteW(nullptr, L"open", m_settingsUri.c_str(),
                                         nullptr, nullptr, SW_SHOWNORMAL);
        if ((INT_PTR)result <= 32)
        {
            return Error::Make(Error::Code::NotSupported,
                "Stránku nastavení sa nepodarilo otvoriť.", (long)(INT_PTR)result);
        }
        return Error::Ok();
    }

    Error ManualGuideOptimization::Restore()
    {
        return Error::Make(Error::Code::NoBackup,
            "Nasaki toto nastavenie nemení, takže nie je čo vracať.");
    }
}
