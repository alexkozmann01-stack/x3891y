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
    // Three baked sizes rather than one font scaled at draw time —
    // ImGui rasterizes at the baked size, so scaling a 24px font up to a
    // 32px page title just renders it blurry.
    void Set(ImFont* body, ImFont* heading, ImFont* title);
    ImFont* Body();     // Manrope Medium 16 — body copy
    ImFont* Heading();  // Unbounded ExtraBold 19 — card/section titles
    ImFont* Title();    // Unbounded ExtraBold 32 — page titles, stat numbers
}
