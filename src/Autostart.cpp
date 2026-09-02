#include "Autostart.h"

#include <windows.h>
#include <string>

namespace
{
    const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    const wchar_t* kValueName = L"Nasaki";

    std::wstring QuotedExePath()
    {
        wchar_t path[MAX_PATH];
        DWORD len = GetModuleFileNameW(nullptr, path, MAX_PATH);
        if (len == 0 || len >= MAX_PATH)
        {
            return L"";
        }
        return L"\"" + std::wstring(path, len) + L"\"";
    }
}

namespace Autostart
{
    bool IsEnabled()
    {
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        {
            return false;
        }
        LSTATUS status = RegQueryValueExW(key, kValueName, nullptr, nullptr, nullptr, nullptr);
        RegCloseKey(key);
        return status == ERROR_SUCCESS;
    }

    void SetEnabled(bool enabled)
    {
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS)
        {
            return;
        }

        if (enabled)
        {
            std::wstring value = QuotedExePath();
            if (!value.empty())
            {
                RegSetValueExW(
                    key, kValueName, 0, REG_SZ,
                    reinterpret_cast<const BYTE*>(value.c_str()),
                    (DWORD)((value.size() + 1) * sizeof(wchar_t)));
            }
        }
        else
        {
            RegDeleteValueW(key, kValueName);
        }

        RegCloseKey(key);
    }
}
