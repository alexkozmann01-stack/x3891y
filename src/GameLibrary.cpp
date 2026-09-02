#include "GameLibrary.h"
#include "WinStr.h"

#include <windows.h>
#include <shlobj.h>
#include <shellapi.h> // ShellExecuteW — not pulled in by windows.h under WIN32_LEAN_AND_MEAN
#include <fstream>
#include <sstream>
#include <algorithm>
#include <nlohmann/json.hpp>

#pragma comment(lib, "shell32.lib")

namespace
{
    std::wstring RegReadString(HKEY root, const wchar_t* subKey, const wchar_t* valueName)
    {
        wchar_t buffer[1024];
        DWORD size = sizeof(buffer);
        DWORD type = 0;
        LSTATUS status = RegGetValueW(root, subKey, valueName, RRF_RT_REG_SZ, &type, buffer, &size);
        if (status != ERROR_SUCCESS)
        {
            return L"";
        }
        return std::wstring(buffer);
    }

    std::string ReadWholeFile(const std::wstring& path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open())
        {
            return "";
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }

    // Valve's .acf/.vdf files are a simple quoted key/value text format:
    //     "name"    "Counter-Strike 2"
    // Good enough to pull single top-level-ish keys out without a real
    // VDF parser.
    std::string VdfFindValue(const std::string& text, const std::string& key)
    {
        std::string needle = "\"" + key + "\"";
        size_t pos = text.find(needle);
        if (pos == std::string::npos)
        {
            return "";
        }
        pos = text.find('"', pos + needle.size()); // opening quote of the value
        if (pos == std::string::npos)
        {
            return "";
        }
        size_t end = text.find('"', pos + 1);
        if (end == std::string::npos)
        {
            return "";
        }
        return text.substr(pos + 1, end - pos - 1);
    }

    // Every "path" value in libraryfolders.vdf — one per Steam library
    // folder (the default one plus any the user added on other drives).
    std::vector<std::string> VdfFindAllPaths(const std::string& text)
    {
        std::vector<std::string> out;
        const std::string needle = "\"path\"";
        size_t search = 0;
        while ((search = text.find(needle, search)) != std::string::npos)
        {
            size_t open = text.find('"', search + needle.size());
            if (open == std::string::npos) break;
            size_t end = text.find('"', open + 1);
            if (end == std::string::npos) break;

            std::string raw = text.substr(open + 1, end - open - 1);
            std::string unescaped;
            for (size_t i = 0; i < raw.size(); i++)
            {
                if (raw[i] == '\\' && i + 1 < raw.size() && raw[i + 1] == '\\')
                {
                    unescaped += '\\';
                    i++;
                }
                else
                {
                    unescaped += raw[i];
                }
            }
            out.push_back(unescaped);
            search = end + 1;
        }
        return out;
    }

    void ScanSteam(std::vector<InstalledGame>& games)
    {
        std::wstring steamPath = RegReadString(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath");
        if (steamPath.empty())
        {
            steamPath = RegReadString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam", L"InstallPath");
        }
        if (steamPath.empty())
        {
            return;
        }
        std::replace(steamPath.begin(), steamPath.end(), L'/', L'\\');

        std::vector<std::wstring> libraries{ steamPath };
        std::string vdf = ReadWholeFile(steamPath + L"\\steamapps\\libraryfolders.vdf");
        for (const std::string& p : VdfFindAllPaths(vdf))
        {
            std::wstring wide = WinStr::ToWide(p);
            if (!wide.empty() && wide != steamPath)
            {
                libraries.push_back(wide);
            }
        }

        for (const std::wstring& lib : libraries)
        {
            std::wstring pattern = lib + L"\\steamapps\\appmanifest_*.acf";
            WIN32_FIND_DATAW findData;
            HANDLE find = FindFirstFileW(pattern.c_str(), &findData);
            if (find == INVALID_HANDLE_VALUE)
            {
                continue;
            }
            do
            {
                std::string manifest = ReadWholeFile(lib + L"\\steamapps\\" + findData.cFileName);
                std::string name = VdfFindValue(manifest, "name");
                if (name.empty())
                {
                    continue;
                }
                std::string installDir = VdfFindValue(manifest, "installdir");
                std::string path = installDir.empty()
                    ? ""
                    : WinStr::ToUtf8(lib) + "\\steamapps\\common\\" + installDir;

                // Launch through Steam itself rather than the bare exe, so
                // its overlay/cloud saves/DRM all still apply.
                std::string appId = VdfFindValue(manifest, "appid");
                std::string launch = appId.empty() ? "" : "steam://rungameid/" + appId;
                games.push_back({ name, "Steam", path, launch });
            } while (FindNextFileW(find, &findData));
            FindClose(find);
        }
    }

    void ScanEpic(std::vector<InstalledGame>& games)
    {
        PWSTR programData = nullptr;
        if (FAILED(SHGetKnownFolderPath(FOLDERID_ProgramData, 0, nullptr, &programData)))
        {
            return;
        }
        std::wstring manifestDir = std::wstring(programData) + L"\\Epic\\EpicGamesLauncher\\Data\\Manifests";
        CoTaskMemFree(programData);

        WIN32_FIND_DATAW findData;
        HANDLE find = FindFirstFileW((manifestDir + L"\\*.item").c_str(), &findData);
        if (find == INVALID_HANDLE_VALUE)
        {
            return;
        }
        do
        {
            std::string content = ReadWholeFile(manifestDir + L"\\" + findData.cFileName);
            if (content.empty())
            {
                continue;
            }
            try
            {
                nlohmann::json j = nlohmann::json::parse(content);
                std::string name = j.value("DisplayName", "");
                if (name.empty())
                {
                    continue;
                }
                std::string appName = j.value("AppName", "");
                std::string launch = appName.empty()
                    ? ""
                    : "com.epicgames.launcher://apps/" + appName + "?action=launch&silent=true";
                games.push_back({ name, "Epic", j.value("InstallLocation", ""), launch });
            }
            catch (const nlohmann::json::parse_error&)
            {
                // A manifest we can't read is not worth failing the scan over.
            }
        } while (FindNextFileW(find, &findData));
        FindClose(find);
    }

    void ScanGog(std::vector<InstalledGame>& games)
    {
        const wchar_t* gogRoot = L"SOFTWARE\\WOW6432Node\\GOG.com\\Games";
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, gogRoot, 0, KEY_READ, &key) != ERROR_SUCCESS)
        {
            return;
        }

        wchar_t subKeyName[256];
        DWORD index = 0;
        DWORD nameLen = 256;
        while (RegEnumKeyExW(key, index, subKeyName, &nameLen, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS)
        {
            std::wstring full = std::wstring(gogRoot) + L"\\" + subKeyName;
            std::wstring name = RegReadString(HKEY_LOCAL_MACHINE, full.c_str(), L"gameName");
            if (!name.empty())
            {
                std::wstring path = RegReadString(HKEY_LOCAL_MACHINE, full.c_str(), L"path");
                std::wstring exe = RegReadString(HKEY_LOCAL_MACHINE, full.c_str(), L"exe");
                games.push_back({
                    WinStr::ToUtf8(name), "GOG", WinStr::ToUtf8(path), WinStr::ToUtf8(exe) });
            }
            index++;
            nameLen = 256;
        }
        RegCloseKey(key);
    }
}

namespace GameLibrary
{
    bool Launch(const InstalledGame& game)
    {
        if (game.launchCommand.empty())
        {
            return false;
        }
        std::wstring command = WinStr::ToWide(game.launchCommand);
        // ShellExecute handles both the steam://ourl form and a plain .exe
        // path; anything at or below 32 is one of its documented failures.
        HINSTANCE result = ShellExecuteW(nullptr, L"open", command.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return (INT_PTR)result > 32;
    }

    std::vector<InstalledGame> Scan()
    {
        std::vector<InstalledGame> games;
        ScanSteam(games);
        ScanEpic(games);
        ScanGog(games);

        std::sort(games.begin(), games.end(), [](const InstalledGame& a, const InstalledGame& b) {
            return a.name < b.name;
        });
        return games;
    }
}
