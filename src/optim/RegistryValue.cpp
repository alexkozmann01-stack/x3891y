#include "RegistryValue.h"

#include <windows.h>

namespace optim
{
    namespace reg
    {
        RegSnapshot Read(const RegPath& path, long* systemError)
        {
            RegSnapshot snapshot;
            if (systemError) *systemError = 0;

            HKEY key = nullptr;
            LSTATUS status = RegOpenKeyExW((HKEY)path.root, path.subKey.c_str(), 0, KEY_QUERY_VALUE, &key);
            if (status != ERROR_SUCCESS)
            {
                // A missing key means the value doesn't exist — that's a
                // legitimate "not set" state, not a failure to report.
                if (status != ERROR_FILE_NOT_FOUND && systemError)
                {
                    *systemError = status;
                }
                return snapshot;
            }

            DWORD type = 0;
            DWORD size = 0;
            status = RegQueryValueExW(key, path.valueName.c_str(), nullptr, &type, nullptr, &size);
            if (status != ERROR_SUCCESS)
            {
                if (status != ERROR_FILE_NOT_FOUND && systemError)
                {
                    *systemError = status;
                }
                RegCloseKey(key);
                return snapshot;
            }

            snapshot.existed = true;
            snapshot.type = type;
            snapshot.raw.resize(size);
            if (size > 0)
            {
                status = RegQueryValueExW(key, path.valueName.c_str(), nullptr, &type,
                    snapshot.raw.data(), &size);
                if (status != ERROR_SUCCESS)
                {
                    if (systemError) *systemError = status;
                    RegCloseKey(key);
                    return RegSnapshot{};
                }
                snapshot.raw.resize(size);
            }

            if (type == REG_DWORD && snapshot.raw.size() >= sizeof(DWORD))
            {
                DWORD value = 0;
                memcpy(&value, snapshot.raw.data(), sizeof(DWORD));
                snapshot.dword = value;
            }

            RegCloseKey(key);
            return snapshot;
        }

        bool WriteDword(const RegPath& path, uint32_t value, long* systemError)
        {
            if (systemError) *systemError = 0;

            HKEY key = nullptr;
            LSTATUS status = RegCreateKeyExW((HKEY)path.root, path.subKey.c_str(), 0, nullptr,
                REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &key, nullptr);
            if (status != ERROR_SUCCESS)
            {
                if (systemError) *systemError = status;
                return false;
            }

            DWORD data = value;
            status = RegSetValueExW(key, path.valueName.c_str(), 0, REG_DWORD,
                reinterpret_cast<const BYTE*>(&data), sizeof(data));
            RegCloseKey(key);

            if (status != ERROR_SUCCESS)
            {
                if (systemError) *systemError = status;
                return false;
            }
            return true;
        }

        bool DeleteValue(const RegPath& path, long* systemError)
        {
            if (systemError) *systemError = 0;

            HKEY key = nullptr;
            LSTATUS status = RegOpenKeyExW((HKEY)path.root, path.subKey.c_str(), 0, KEY_SET_VALUE, &key);
            if (status != ERROR_SUCCESS)
            {
                // Key already gone: the value is absent, which is the state
                // the caller wanted.
                return status == ERROR_FILE_NOT_FOUND;
            }

            status = RegDeleteValueW(key, path.valueName.c_str());
            RegCloseKey(key);

            if (status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND)
            {
                return true;
            }
            if (systemError) *systemError = status;
            return false;
        }

        bool WriteSnapshot(const RegPath& path, const RegSnapshot& snapshot, long* systemError)
        {
            if (!snapshot.existed)
            {
                return DeleteValue(path, systemError);
            }

            if (systemError) *systemError = 0;

            HKEY key = nullptr;
            LSTATUS status = RegCreateKeyExW((HKEY)path.root, path.subKey.c_str(), 0, nullptr,
                REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &key, nullptr);
            if (status != ERROR_SUCCESS)
            {
                if (systemError) *systemError = status;
                return false;
            }

            status = RegSetValueExW(key, path.valueName.c_str(), 0, snapshot.type,
                snapshot.raw.empty() ? nullptr : snapshot.raw.data(),
                (DWORD)snapshot.raw.size());
            RegCloseKey(key);

            if (status != ERROR_SUCCESS)
            {
                if (systemError) *systemError = status;
                return false;
            }
            return true;
        }

        bool KeyExists(void* root, const std::wstring& subKey)
        {
            HKEY key = nullptr;
            if (RegOpenKeyExW((HKEY)root, subKey.c_str(), 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
            {
                return false;
            }
            RegCloseKey(key);
            return true;
        }
    }
}
