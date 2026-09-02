#pragma once

#include "imgui.h"

// ImGui style that mirrors the nasaki.eu web dashboard palette (see the
// :root CSS variables in index.html / includes/admin_layout.php) so the
// desktop app and the website read as the same product.
void ApplyNasakiTheme();

// Raw brand colors for custom-drawn widgets (status badges, etc.) that
// don't map to a standard ImGuiCol_* slot.
namespace NasakiColors
{
    ImVec4 Accent();
    ImVec4 Accent2();
    ImVec4 Ok();
    ImVec4 Danger();
    ImVec4 InkDim();
    ImVec4 Line();
    ImVec4 BgPanel2();

    // U32-packed versions of the above, for ImDrawList calls (which want
    // ImU32, not ImVec4) — same source colors, just already converted.
    ImU32 U32(const ImVec4& c);
}

// Body is Manrope (matches the site's body copy), Heading is Unbounded
// (matches the site's display headings/brand wordmark). Both are loaded in
// main.cpp — after ImGui_ImplDX11_Init but before the render loop — since
// that's the only place with access to ImGuiIO::Fonts; this namespace just
// hands the resulting ImFont* around to App.cpp/UI.cpp.
namespace NasakiFonts
{
    void Set(ImFont* body, ImFont* heading);
    ImFont* Body();
    ImFont* Heading();
}
