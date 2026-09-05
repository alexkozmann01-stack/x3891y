#pragma once

#include "Optimization.h"
#include "RegistryValue.h"
#include "BackupStore.h"

#include <vector>
#include <functional>

namespace optim
{
    // Most Windows preferences are one or more DWORDs under HKCU. This
    // implements the whole contract for that shape: back up every value it
    // touches, write, read back, and report a mismatch as a failure rather
    // than success.
    class RegistryOptimization : public Optimization
    {
    public:
        struct Target
        {
            std::string key;      // stable name for this value inside the optimization
            RegPath path;
            uint32_t appliedValue = 0;
        };

        // `extraSupportCheck` covers the cases where the key existing isn't
        // enough — a Windows 11-only value whose parent key also exists on
        // Windows 10, for instance. Returning false makes the card show
        // "unsupported" rather than silently disappearing.
        using SupportCheck = std::function<bool()>;

        RegistryOptimization(Info info, std::vector<Target> targets, BackupStore* backups,
                             SupportCheck extraSupportCheck = nullptr);

        const Info& info() const override { return m_info; }
        Status Read() const override;
        Error Apply() override;
        Error Restore() override;
        bool Supported() const override;

    private:
        Info m_info;
        std::vector<Target> m_targets;
        BackupStore* m_backups;
        SupportCheck m_extraSupportCheck;
    };

    // Same guarantees as above for values Windows stores as text rather than
    // a DWORD (menu delay, for instance). Kept separate instead of adding a
    // type switch to RegistryOptimization so the verification stays exact:
    // a REG_SZ compare is a string compare, not a numeric one.
    class RegistryStringOptimization : public Optimization
    {
    public:
        struct Target
        {
            std::string key;
            RegPath path;
            std::wstring appliedValue;
        };

        RegistryStringOptimization(Info info, std::vector<Target> targets, BackupStore* backups);

        const Info& info() const override { return m_info; }
        Status Read() const override;
        Error Apply() override;
        Error Restore() override;
        bool Supported() const override;

    private:
        Info m_info;
        std::vector<Target> m_targets;
        BackupStore* m_backups;
    };

    // Settings exposed through SystemParametersInfo rather than a documented
    // registry value. The SET variants take the BOOL *in* pvParam (the docs
    // distinguish "must point to a BOOL" for GET from "a BOOL variable" for
    // SET), which is the usual place this API gets misused.
    class SpiBoolOptimization : public Optimization
    {
    public:
        SpiBoolOptimization(Info info, unsigned int getAction, unsigned int setAction,
                            bool appliedValue, BackupStore* backups);

        const Info& info() const override { return m_info; }
        Status Read() const override;
        Error Apply() override;
        Error Restore() override;

    private:
        std::optional<bool> ReadRaw(long* systemError) const;
        Error Write(bool value);

        Info m_info;
        unsigned int m_getAction;
        unsigned int m_setAction;
        bool m_appliedValue;
        BackupStore* m_backups;
    };
}
