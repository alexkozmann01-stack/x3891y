#pragma once

#include <string>
#include <vector>
#include <optional>
#include <cstdint>

// A registry value captured faithfully enough to put back byte-for-byte —
// including the case that matters most for rollback: the value did not
// exist before we wrote it, so restoring means deleting it, not writing a
// zero. Writing a "default" instead of deleting is the usual way tweak
// tools quietly damage a system.
namespace optim
{
    struct RegSnapshot
    {
        bool existed = false;
        uint32_t type = 0;              // REG_DWORD, REG_SZ, ...
        uint32_t dword = 0;             // valid when type == REG_DWORD
        std::vector<uint8_t> raw;       // full data for non-DWORD types

        bool operator==(const RegSnapshot& other) const
        {
            if (existed != other.existed) return false;
            if (!existed) return true;
            if (type != other.type) return false;
            if (type == 4 /*REG_DWORD*/) return dword == other.dword;
            return raw == other.raw;
        }
    };

    // Identifies one value. Only HKCU is used by the shipped catalog —
    // HKLM would require elevation, which the UI process deliberately
    // doesn't have.
    struct RegPath
    {
        void* root = nullptr;   // HKEY; void* keeps windows.h out of this header
        std::wstring subKey;
        std::wstring valueName;
    };

    namespace reg
    {
        // Reads a value. `existed == false` is a normal result, not an error;
        // `error` is only set when the read itself failed for another reason.
        RegSnapshot Read(const RegPath& path, long* systemError = nullptr);

        bool WriteDword(const RegPath& path, uint32_t value, long* systemError = nullptr);

        // Puts back a snapshot: deletes the value when it didn't originally
        // exist, otherwise rewrites the original type and bytes.
        bool WriteSnapshot(const RegPath& path, const RegSnapshot& snapshot, long* systemError = nullptr);

        bool DeleteValue(const RegPath& path, long* systemError = nullptr);

        // True if the key exists at all — used for support detection, since
        // some of these keys only exist on certain Windows editions/builds.
        bool KeyExists(void* root, const std::wstring& subKey);
    }
}
