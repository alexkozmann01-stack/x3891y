#include "Theme.h"
#include "imgui.h"

namespace
{
    // 0-255 -> 0-1 helper so the values below can be typed straight from the
    // site's hex palette instead of hand-converted floats.
    ImVec4 RGB(int r, int g, int b, float a = 1.0f)
    {
        return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a);
    }

    // Same palette as :root in index.html / includes/admin_layout.php.
    const ImVec4 kBg        = RGB(0x06, 0x09, 0x12); // --bg
    const ImVec4 kBgPanel   = RGB(0x0b, 0x0f, 0x1c); // --bg-panel
    const ImVec4 kBgPanel2  = RGB(0x0e, 0x14, 0x24); // --bg-panel-2
    const ImVec4 kLine      = RGB(0x1c, 0x27, 0x40); // --line
    const ImVec4 kAccent    = RGB(0x2f, 0x7f, 0xfc); // --accent
    const ImVec4 kAccent2   = RGB(0x7f, 0xd6, 0xff); // --accent-2
    const ImVec4 kInk       = RGB(0xee, 0xf3, 0xfb); // --ink
    const ImVec4 kInkDim    = RGB(0x8b, 0x96, 0xb3); // --ink-dim
    const ImVec4 kDanger    = RGB(0xff, 0x5d, 0x5d); // --danger
    const ImVec4 kOk        = RGB(0x6b, 0xe3, 0xa3); // --ok
}

void ApplyNasakiTheme()
{
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    colors[ImGuiCol_Text]                  = kInk;
    colors[ImGuiCol_TextDisabled]          = kInkDim;
    colors[ImGuiCol_WindowBg]              = kBg;
    colors[ImGuiCol_ChildBg]               = kBgPanel;
    colors[ImGuiCol_PopupBg]               = kBgPanel;
    colors[ImGuiCol_Border]                = kLine;
    colors[ImGuiCol_BorderShadow]          = ImVec4(0, 0, 0, 0);

    colors[ImGuiCol_FrameBg]               = kBgPanel2;
    colors[ImGuiCol_FrameBgHovered]        = ImVec4(kLine.x, kLine.y, kLine.z, 1.0f);
    colors[ImGuiCol_FrameBgActive]         = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.35f);

    colors[ImGuiCol_TitleBg]               = kBgPanel;
    colors[ImGuiCol_TitleBgActive]         = kBgPanel;
    colors[ImGuiCol_TitleBgCollapsed]      = kBgPanel;
    colors[ImGuiCol_MenuBarBg]             = kBgPanel;

    colors[ImGuiCol_ScrollbarBg]           = kBg;
    colors[ImGuiCol_ScrollbarGrab]         = kLine;
    colors[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.6f);
    colors[ImGuiCol_ScrollbarGrabActive]   = kAccent;

    colors[ImGuiCol_CheckMark]             = kAccent2;
    colors[ImGuiCol_SliderGrab]            = kAccent;
    colors[ImGuiCol_SliderGrabActive]      = kAccent2;

    colors[ImGuiCol_Button]                = kBgPanel2;
    colors[ImGuiCol_ButtonHovered]         = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.55f);
    colors[ImGuiCol_ButtonActive]          = kAccent;

    // Used for selected sidebar nav items, selectable rows, etc. — the site
    // uses a translucent accent-2 wash for "active" nav links; mirror that.
    colors[ImGuiCol_Header]                = ImVec4(kAccent2.x, kAccent2.y, kAccent2.z, 0.12f);
    colors[ImGuiCol_HeaderHovered]         = ImVec4(kAccent2.x, kAccent2.y, kAccent2.z, 0.20f);
    colors[ImGuiCol_HeaderActive]          = ImVec4(kAccent2.x, kAccent2.y, kAccent2.z, 0.28f);

    colors[ImGuiCol_Separator]             = kLine;
    colors[ImGuiCol_SeparatorHovered]      = kAccent;
    colors[ImGuiCol_SeparatorActive]       = kAccent;

    colors[ImGuiCol_ResizeGrip]            = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.25f);
    colors[ImGuiCol_ResizeGripHovered]     = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.55f);
    colors[ImGuiCol_ResizeGripActive]      = kAccent;

    colors[ImGuiCol_Tab]                   = kBgPanel2;
    colors[ImGuiCol_TabHovered]            = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.55f);
    colors[ImGuiCol_TabActive]             = ImVec4(kAccent2.x, kAccent2.y, kAccent2.z, 0.18f);
    colors[ImGuiCol_TabUnfocused]          = kBgPanel2;
    colors[ImGuiCol_TabUnfocusedActive]    = kBgPanel2;

    colors[ImGuiCol_PlotLines]             = kAccent;
    colors[ImGuiCol_PlotLinesHovered]      = kAccent2;
    colors[ImGuiCol_PlotHistogram]         = kAccent;
    colors[ImGuiCol_PlotHistogramHovered]  = kAccent2;

    colors[ImGuiCol_TextSelectedBg]        = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.35f);
    colors[ImGuiCol_DragDropTarget]        = kAccent2;
    colors[ImGuiCol_NavHighlight]          = kAccent2;

    // The site favors small, tight radii (3-8px) over fully rounded controls.
    style.WindowRounding    = 8.0f;
    style.ChildRounding     = 8.0f;
    style.FrameRounding     = 4.0f;
    style.PopupRounding     = 6.0f;
    style.ScrollbarRounding = 6.0f;
    style.GrabRounding      = 4.0f;
    style.TabRounding       = 4.0f;

    style.WindowBorderSize  = 1.0f;
    style.ChildBorderSize   = 1.0f;
    style.FrameBorderSize   = 1.0f;
    style.PopupBorderSize   = 1.0f;

    style.WindowPadding     = ImVec2(20, 20);
    style.FramePadding      = ImVec2(10, 8);
    style.ItemSpacing       = ImVec2(10, 10);
    style.ItemInnerSpacing  = ImVec2(8, 6);
    style.IndentSpacing     = 18.0f;
    style.ScrollbarSize     = 12.0f;
}

// Exposed for widgets that want a specific brand color without going through
// the full ImGuiStyle table (e.g. a status badge that isn't a standard
// ImGui element). Kept in this translation unit so the palette has one
// source of truth.
namespace NasakiColors
{
    ImVec4 Accent()  { return kAccent; }
    ImVec4 Accent2() { return kAccent2; }
    ImVec4 Ok()      { return kOk; }
    ImVec4 Danger()  { return kDanger; }
    ImVec4 InkDim()  { return kInkDim; }
    ImVec4 Line()    { return kLine; }
    ImVec4 BgPanel2(){ return kBgPanel2; }
}
