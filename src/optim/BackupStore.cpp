#include "BackupStore.h"
#include "../WinStr.h"

#include <windows.h>
#include <shlobj.h>
#include <fstream>
#include <sstream>
#include <ctime>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace
{
    std::wstring StoreDir()
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

    std::wstring StorePath()
    {
        std::wstring dir = StoreDir();
        return dir.empty() ? L"" : dir + L"\\backups.json";
    }

    std::string NowUtc()
    {
        std::time_t t = std::time(nullptr);
        std::tm tmBuf{};
        gmtime_s(&tmBuf, &t);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmBuf);
        return buf;
    }

    // Registry data is arbitrary bytes; hex keeps the journal valid JSON and
    // round-trips exactly (base64 would do too, hex is easier to eyeball).
    std::string ToHex(const std::vector<uint8_t>& bytes)
    {
        static const char* digits = "0123456789abcdef";
        std::string out;
        out.reserve(bytes.size() * 2);
        for (uint8_t b : bytes)
        {
            out += digits[b >> 4];
            out += digits[b & 0x0F];
        }
        return out;
    }

    std::vector<uint8_t> FromHex(const std::string& hex)
    {
        auto nibble = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        std::vector<uint8_t> out;
        out.reserve(hex.size() / 2);
        for (size_t i = 0; i + 1 < hex.size(); i += 2)
        {
            int hi = nibble(hex[i]);
            int lo = nibble(hex[i + 1]);
            if (hi < 0 || lo < 0) return {};
            out.push_back((uint8_t)((hi << 4) | lo));
        }
        return out;
    }
}

namespace optim
{
    std::string BackupStore::Key(const std::string& id, const std::string& valueKey) const
    {
        return id + "/" + valueKey;
    }

    void BackupStore::SetStorePathForTesting(const std::wstring& path)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pathOverride = path;
    }

    void BackupStore::Load()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_loaded = true;

        std::wstring path = m_pathOverride.empty() ? StorePath() : m_pathOverride;
        if (path.empty()) return;

        std::ifstream file(path);
        if (!file.is_open()) return;

        std::stringstream buffer;
        buffer << file.rdbuf();

        try
        {
            json root = json::parse(buffer.str());

            for (const auto& e : root.value("backups", json::array()))
            {
                BackupEntry entry;
                entry.optimizationId = e.value("id", "");
                entry.valueKey = e.value("valueKey", "");
                entry.appliedAtUtc = e.value("appliedAt", "");
                entry.note = e.value("note", "");
                entry.original.existed = e.value("existed", false);
                entry.original.type = e.value("type", 0u);
                entry.original.dword = e.value("dword", 0u);
                entry.original.raw = FromHex(e.value("raw", ""));
                if (!entry.optimizationId.empty())
                {
                    m_entries[Key(entry.optimizationId, entry.valueKey)] = entry;
                }
            }

            for (const auto& h : root.value("history", json::array()))
            {
                HistoryRecord record;
                record.timestampUtc = h.value("at", "");
                record.optimizationId = h.value("id", "");
                record.action = h.value("action", "");
                record.success = h.value("success", false);
                record.message = h.value("message", "");
                m_history.push_back(record);
            }
        }
        catch (const json::parse_error&)
        {
            // Leave the file on disk untouched — losing the record of what
            // to roll back to is worse than starting this session empty.
            m_entries.clear();
            m_history.clear();
        }
    }

    bool BackupStore::CaptureIfAbsent(const std::string& id, const std::string& valueKey,
                                      const RegSnapshot& original, const std::string& note)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::string key = Key(id, valueKey);
        if (m_entries.count(key) != 0)
        {
            return false; // keep the first-ever original
        }

        BackupEntry entry;
        entry.optimizationId = id;
        entry.valueKey = valueKey;
        entry.original = original;
        entry.appliedAtUtc = NowUtc();
        entry.note = note;
        m_entries[key] = entry;
        return true;
    }

    std::optional<RegSnapshot> BackupStore::Find(const std::string& id, const std::string& valueKey) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_entries.find(Key(id, valueKey));
        if (it == m_entries.end()) return std::nullopt;
        return it->second.original;
    }

    bool BackupStore::Has(const std::string& id) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto& [key, entry] : m_entries)
        {
            if (entry.optimizationId == id) return true;
        }
        return false;
    }

    void BackupStore::Forget(const std::string& id)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto it = m_entries.begin(); it != m_entries.end();)
        {
            it = (it->second.optimizationId == id) ? m_entries.erase(it) : std::next(it);
        }
    }

    void BackupStore::ForgetValue(const std::string& id, const std::string& valueKey)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_entries.erase(Key(id, valueKey));
    }

    std::vector<BackupEntry> BackupStore::Entries() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<BackupEntry> out;
        out.reserve(m_entries.size());
        for (const auto& [key, entry] : m_entries)
        {
            out.push_back(entry);
        }
        return out;
    }

    void BackupStore::RecordHistory(const std::string& id, const std::string& action,
                                    bool success, const std::string& message)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        HistoryRecord record;
        record.timestampUtc = NowUtc();
        record.optimizationId = id;
        record.action = action;
        record.success = success;
        record.message = message;
        m_history.push_back(record);

        // Bounded so the journal can't grow without limit.
        if (m_history.size() > 500)
        {
            m_history.erase(m_history.begin(), m_history.begin() + (m_history.size() - 500));
        }
    }

    std::vector<BackupStore::HistoryRecord> BackupStore::History() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_history;
    }

    bool BackupStore::Save()
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        std::wstring path = m_pathOverride;
        if (path.empty())
        {
            std::wstring dir = StoreDir();
            if (dir.empty()) return false;
            SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
            path = StorePath();
        }

        json root;
        root["backups"] = json::array();
        for (const auto& [key, entry] : m_entries)
        {
            json e;
            e["id"] = entry.optimizationId;
            e["valueKey"] = entry.valueKey;
            e["appliedAt"] = entry.appliedAtUtc;
            e["note"] = entry.note;
            e["existed"] = entry.original.existed;
            e["type"] = entry.original.type;
            e["dword"] = entry.original.dword;
            e["raw"] = ToHex(entry.original.raw);
            root["backups"].push_back(e);
        }

        root["history"] = json::array();
        for (const auto& record : m_history)
        {
            json h;
            h["at"] = record.timestampUtc;
            h["id"] = record.optimizationId;
            h["action"] = record.action;
            h["success"] = record.success;
            h["message"] = record.message;
            root["history"].push_back(h);
        }

        std::ofstream file(path, std::ios::trunc);
        if (!file.is_open()) return false;
        file << root.dump(2);
        return true;
    }
}
