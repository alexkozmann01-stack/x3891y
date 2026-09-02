#pragma once

#include <string>
#include <optional>

// A device only ever holds one token at a time, persisted at
// %APPDATA%\Nasaki\config.json so re-launching the app doesn't require
// re-entering the license key.
struct StoredDevice
{
    std::string licenseKey;
    long long deviceId = 0;
    std::string deviceToken;
};

namespace LicenseStore
{
    std::optional<StoredDevice> Load();
    bool Save(const StoredDevice& device);
    void Clear();
}
