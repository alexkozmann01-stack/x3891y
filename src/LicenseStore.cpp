#include "LicenseStore.h"

#include <windows.h>
#include <shlobj.h>
#include <fstream>
#include <sstream>
#include <nlohmann/json.hpp>

#pragma comment(lib, "shell32.lib")

using json = nlohmann::json;

namespace
{
    std::wstring ConfigDir()
    {
        PWSTR appData = nullptr;
        std::wstring dir;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appData)))
        {
            dir = appData;
            dir += L"\\Nasaki";
            CoTaskMemFree(appData);
        }
        return dir;
    }

    std::wstring ConfigPath()
    {
        return ConfigDir() + L"\\config.json";
    }
}

namespace LicenseStore
{
    std::optional<StoredDevice> Load()
    {
        // std::ifstream's C++17 std::filesystem::path constructor handles the
        // wide path (so a non-ASCII Windows username in %APPDATA% still
        // resolves); file contents themselves are plain ASCII bytes (hex
        // token, license key), so no wide/narrow transcoding is needed.
        std::ifstream file(ConfigPath());
        if (!file.is_open())
        {
            return std::nullopt;
        }

        std::stringstream buffer;
        buffer << file.rdbuf();

        try
        {
            json j = json::parse(buffer.str());
            StoredDevice device;
            device.licenseKey = j.value("license_key", "");
            device.deviceId = j.value("device_id", 0LL);
            device.deviceToken = j.value("device_token", "");
            if (device.deviceToken.empty())
            {
                return std::nullopt;
            }
            return device;
        }
        catch (const json::parse_error&)
        {
            return std::nullopt;
        }
    }

    bool Save(const StoredDevice& device)
    {
        std::wstring dir = ConfigDir();
        if (dir.empty())
        {
            return false;
        }
        SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);

        json j = {
            {"license_key", device.licenseKey},
            {"device_id", device.deviceId},
            {"device_token", device.deviceToken},
        };

        std::ofstream file(ConfigPath(), std::ios::trunc);
        if (!file.is_open())
        {
            return false;
        }
        file << j.dump(2);
        return true;
    }

    void Clear()
    {
        DeleteFileW(ConfigPath().c_str());
    }
}
