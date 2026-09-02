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
}
