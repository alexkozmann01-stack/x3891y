#include "StorageCleanup.h"
#include "../WinStr.h"

#include <windows.h>
#include <shlobj.h>
#include <knownfolders.h>
#include <shellapi.h>
#include <algorithm>

namespace
{
    // A file this new is very likely still in use by a running program.
    // Windows' own Disk Cleanup applies the same kind of guard.
    bool OldEnoughToDelete(const FILETIME& lastWrite, int minimumAgeDays)
    {
        FILETIME now{};
        GetSystemTimeAsFileTime(&now);

        ULARGE_INTEGER then{}, current{};
        then.LowPart = lastWrite.dwLowDateTime;
        then.HighPart = lastWrite.dwHighDateTime;
        current.LowPart = now.dwLowDateTime;
        current.HighPart = now.dwHighDateTime;

        if (current.QuadPart <= then.QuadPart) return false;

        // FILETIME counts 100-nanosecond intervals.
        const uint64_t hundredNsPerDay = 24ull * 60ull * 60ull * 10'000'000ull;
        uint64_t ageDays = (current.QuadPart - then.QuadPart) / hundredNsPerDay;
        return ageDays >= (uint64_t)minimumAgeDays;
    }

    uint64_t FileSize(const WIN32_FIND_DATAW& find)
    {
        ULARGE_INTEGER size{};
        size.LowPart = find.nFileSizeLow;
        size.HighPart = find.nFileSizeHigh;
        return size.QuadPart;
    }

    struct WalkTotals
    {
        uint64_t bytes = 0;
        uint64_t deletableBytes = 0;
        int files = 0;
        int deletableFiles = 0;
    };

    // Recursive walk. Reparse points are skipped so a junction can't send the
    // scan somewhere outside the folder the user is looking at.
    void Walk(const std::wstring& directory, int minimumAgeDays, WalkTotals& totals,
              std::vector<std::wstring>* deletablePaths, size_t maxCollected, int depth = 0)
    {
        if (depth > 24) return; // guard against pathological nesting

        WIN32_FIND_DATAW find{};
        HANDLE search = FindFirstFileW((directory + L"\\*").c_str(), &find);
        if (search == INVALID_HANDLE_VALUE) return;

        do
        {
            std::wstring name = find.cFileName;
            if (name == L"." || name == L"..") continue;
            if (find.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;

            std::wstring full = directory + L"\\" + name;

            if (find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                Walk(full, minimumAgeDays, totals, deletablePaths, maxCollected, depth + 1);
                continue;
            }

            uint64_t size = FileSize(find);
            totals.bytes += size;
            totals.files++;

            if (OldEnoughToDelete(find.ftLastWriteTime, minimumAgeDays))
            {
                totals.deletableBytes += size;
                totals.deletableFiles++;
                if (deletablePaths && deletablePaths->size() < maxCollected)
                {
                    deletablePaths->push_back(full);
                }
            }
        } while (FindNextFileW(search, &find));

        FindClose(search);
    }

    std::wstring TempDirectory()
    {
        wchar_t buffer[MAX_PATH + 1];
        DWORD length = GetTempPathW(MAX_PATH + 1, buffer);
        if (length == 0) return L"";
        std::wstring path(buffer, length);
        while (!path.empty() && (path.back() == L'\\' || path.back() == L'/')) path.pop_back();
        return path;
    }

    std::wstring KnownFolder(REFKNOWNFOLDERID id)
    {
        PWSTR raw = nullptr;
        if (FAILED(SHGetKnownFolderPath(id, 0, nullptr, &raw)))
        {
            return L"";
        }
        std::wstring path = raw;
        CoTaskMemFree(raw);
        return path;
    }
}

namespace optim
{
    StorageCleaner::StorageCleaner(BackupStore* backups) : m_backups(backups)
    {
    }

    std::vector<CleanupTarget> StorageCleaner::Analyze(const SystemInventory& inventory) const
    {
        std::vector<CleanupTarget> targets;

        // ---- user temp -------------------------------------------------
        {
            CleanupTarget target;
            target.id = "temp.user";
            target.name = "Dočasné súbory";
            target.description =
                "Súbory, ktoré programy odložili do priečinka TEMP a už ich nepotrebujú. "
                "Mažeme len tie staršie ako " + std::to_string(kMinimumAgeDays) +
                " dní — novšie môže mať otvorený bežiaci program.";
            target.caution =
                "Ak práve prebieha inštalácia, jej dočasné súbory necháme na pokoji "
                "(sú príliš nové aj otvorené).";
            target.deletable = true;

            std::wstring temp = TempDirectory();
            target.path = WinStr::ToUtf8(temp);
            if (!temp.empty())
            {
                WalkTotals totals;
                Walk(temp, kMinimumAgeDays, totals, nullptr, 0);
                target.bytes = totals.bytes;
                target.deletableBytes = totals.deletableBytes;
                target.fileCount = totals.files;
                target.deletableFileCount = totals.deletableFiles;
                target.scanned = true;
            }
            targets.push_back(target);
        }

        // ---- recycle bin -----------------------------------------------
        {
            CleanupTarget target;
            target.id = "recyclebin";
            target.name = "Kôš";
            target.description =
                "Vysype Kôš cez rovnaké systémové volanie, aké používa Prieskumník.";
            target.caution = "Po vysypaní sa súbory z Koša už nedajú obnoviť.";
            target.deletable = true;
            target.path = "Kôš (všetky disky)";

            SHQUERYRBINFO info{};
            info.cbSize = sizeof(info);
            if (SUCCEEDED(SHQueryRecycleBinW(nullptr, &info)))
            {
                target.bytes = (uint64_t)info.i64Size;
                target.deletableBytes = target.bytes;
                target.fileCount = (int)info.i64NumItems;
                target.deletableFileCount = target.fileCount;
                target.scanned = true;
            }
            targets.push_back(target);
        }

        // ---- personal folders: measured, never deletable from here ------
        struct PersonalFolder
        {
            const KNOWNFOLDERID* id;
            const char* targetId;
            const char* name;
        };
        const PersonalFolder personal[] = {
            { &FOLDERID_Downloads, "personal.downloads", "Stiahnuté súbory" },
        };

        for (const PersonalFolder& folder : personal)
        {
            std::wstring path = KnownFolder(*folder.id);
            if (path.empty()) continue;

            CleanupTarget target;
            target.id = folder.targetId;
            target.name = folder.name;
            target.path = WinStr::ToUtf8(path);
            target.description =
                "Býva to najväčší zabudnutý priečinok. Nasaki tu nič nemaže — "
                "ukazujeme len, koľko miesta zaberá.";
            target.deletable = false;

            WalkTotals totals;
            Walk(path, 0, totals, nullptr, 0);
            target.bytes = totals.bytes;
            target.fileCount = totals.files;
            target.scanned = true;
            targets.push_back(target);
        }

        // Sort by what actually frees the most, but keep the read-only
        // entries at the end so the actionable ones lead.
        std::sort(targets.begin(), targets.end(), [](const CleanupTarget& a, const CleanupTarget& b) {
            if (a.deletable != b.deletable) return a.deletable;
            return a.deletableBytes > b.deletableBytes;
        });

        (void)inventory;
        return targets;
    }

    std::vector<std::string> StorageCleaner::Preview(const std::string& targetId, size_t maxItems,
                                                     bool* truncated) const
    {
        if (truncated) *truncated = false;
        std::vector<std::string> preview;

        if (targetId == "temp.user")
        {
            std::wstring temp = TempDirectory();
            if (temp.empty()) return preview;

            WalkTotals totals;
            std::vector<std::wstring> paths;
            Walk(temp, kMinimumAgeDays, totals, &paths, maxItems + 1);

            if (truncated) *truncated = paths.size() > maxItems;
            for (size_t i = 0; i < paths.size() && i < maxItems; i++)
            {
                preview.push_back(WinStr::ToUtf8(paths[i]));
            }
            return preview;
        }

        if (targetId == "recyclebin")
        {
            // The bin's contents are not enumerable through a documented API
            // without shell interfaces, so we say plainly what will happen
            // rather than showing a list we can't produce reliably.
            SHQUERYRBINFO info{};
            info.cbSize = sizeof(info);
            if (SUCCEEDED(SHQueryRecycleBinW(nullptr, &info)))
            {
                preview.push_back("Vysype sa " + std::to_string((long long)info.i64NumItems) +
                                  " položiek (" + FormatBytes((uint64_t)info.i64Size) + ").");
                preview.push_back("Obsah si vieš pozrieť v Koši na ploche pred vysypaním.");
            }
            return preview;
        }

        return preview;
    }

    Error StorageCleaner::Clean(const std::string& targetId, CleanupResult* result)
    {
        CleanupResult local;

        if (targetId == "temp.user")
        {
            std::wstring temp = TempDirectory();
            if (temp.empty())
            {
                return Error::Make(Error::Code::ReadFailed, "Priečinok TEMP sa nepodarilo nájsť.");
            }

            WalkTotals totals;
            std::vector<std::wstring> paths;
            // No cap here: this is the actual delete, and it must cover
            // everything the analysis counted.
            Walk(temp, kMinimumAgeDays, totals, &paths, (size_t)-1);

            for (const std::wstring& path : paths)
            {
                WIN32_FILE_ATTRIBUTE_DATA attributes{};
                uint64_t size = 0;
                if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attributes))
                {
                    ULARGE_INTEGER value{};
                    value.LowPart = attributes.nFileSizeLow;
                    value.HighPart = attributes.nFileSizeHigh;
                    size = value.QuadPart;
                }

                if (DeleteFileW(path.c_str()))
                {
                    local.filesDeleted++;
                    local.bytesFreed += size;
                }
                else
                {
                    // Locked or protected. Left alone and counted — never
                    // forced, and never hidden from the report.
                    local.filesSkipped++;
                }
            }

            m_backups->RecordHistory("storage.temp", "clean", true,
                "Uvoľnené " + FormatBytes(local.bytesFreed) + ", preskočených " +
                std::to_string(local.filesSkipped) + " používaných súborov.");
            m_backups->Save();

            if (result) *result = local;
            return Error::Ok();
        }

        if (targetId == "recyclebin")
        {
            SHQUERYRBINFO before{};
            before.cbSize = sizeof(before);
            uint64_t sizeBefore = SUCCEEDED(SHQueryRecycleBinW(nullptr, &before))
                ? (uint64_t)before.i64Size : 0;
            int itemsBefore = SUCCEEDED(SHQueryRecycleBinW(nullptr, &before))
                ? (int)before.i64NumItems : 0;

            // Our own confirmation already happened in the UI, so Windows'
            // duplicate prompt is suppressed — but not the sound, which is
            // the user's usual feedback that it worked.
            HRESULT hr = SHEmptyRecycleBinW(nullptr, nullptr,
                SHERB_NOCONFIRMATION | SHERB_NOPROGRESSUI);
            if (FAILED(hr) && hr != S_FALSE)
            {
                return Error::Make(Error::Code::WriteFailed, "Kôš sa nepodarilo vysypať.", (long)hr);
            }

            SHQUERYRBINFO after{};
            after.cbSize = sizeof(after);
            uint64_t sizeAfter = SUCCEEDED(SHQueryRecycleBinW(nullptr, &after))
                ? (uint64_t)after.i64Size : 0;

            local.bytesFreed = sizeBefore > sizeAfter ? sizeBefore - sizeAfter : 0;
            local.filesDeleted = itemsBefore;

            m_backups->RecordHistory("storage.recyclebin", "clean", true,
                "Kôš vysypaný, uvoľnené " + FormatBytes(local.bytesFreed) + ".");
            m_backups->Save();

            if (result) *result = local;
            return Error::Ok();
        }

        return Error::Make(Error::Code::NotSupported,
            "Túto položku Nasaki nemaže — je len informatívna.");
    }

    void StorageCleaner::OpenWindowsDiskCleanup()
    {
        // The system locations (Windows Update cache, Delivery Optimization,
        // previous installations) need elevation and are Windows' own job.
        ShellExecuteW(nullptr, L"open", L"cleanmgr.exe", nullptr, nullptr, SW_SHOWNORMAL);
    }

    void StorageCleaner::OpenStorageSettings()
    {
        ShellExecuteW(nullptr, L"open", L"ms-settings:storagesense", nullptr, nullptr, SW_SHOWNORMAL);
    }
}
