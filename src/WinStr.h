#pragma once

#include <windows.h>
#include <string>

// Win32 hands back UTF-16 everywhere; ImGui and our JSON payloads want
// UTF-8. These two conversions were copy-pasted across three translation
// units — they live here now.
namespace WinStr
{
    inline std::string ToUtf8(const std::wstring& w)
    {
        if (w.empty()) return "";
        int len = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
        if (len <= 0) return "";
        std::string out(len, '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), out.data(), len, nullptr, nullptr);
        return out;
    }

    inline std::wstring ToWide(const std::string& s)
    {
        if (s.empty()) return L"";
        int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
        if (len <= 0) return L"";
        std::wstring out(len, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), len);
        return out;
    }

    inline std::wstring ToLower(const std::wstring& s)
    {
        std::wstring out = s;
        for (auto& c : out)
        {
            c = (wchar_t)towlower(c);
        }
        return out;
    }
}
