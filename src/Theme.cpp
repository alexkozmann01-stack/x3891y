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

    // Near-black ground, cards lifted with a violet cast rather than pure
    // grey, purple as the working accent and pink kept for a single
    // highlight so it stays an emphasis rather than decoration.
    const ImVec4 kBg        = RGB(0x07, 0x06, 0x0d); // near-black
    const ImVec4 kBgPanel   = RGB(0x0e, 0x0c, 0x18); // sidebar / panels
    const ImVec4 kBgPanel2  = RGB(0x14, 0x11, 0x22); // cards
    const ImVec4 kBgPanel3  = RGB(0x1b, 0x17, 0x2c); // card hover
    const ImVec4 kLine      = RGB(0x2a, 0x24, 0x40); // borders
    const ImVec4 kAccent    = RGB(0x8b, 0x5c, 0xf6); // violet — primary accent
    const ImVec4 kAccent2   = RGB(0xa7, 0x8b, 0xfa); // lighter violet
    const ImVec4 kPink      = RGB(0xe8, 0x4d, 0xb8); // restrained highlight
    const ImVec4 kInk       = RGB(0xf2, 0xf0, 0xf8); // primary text
    const ImVec4 kInkDim    = RGB(0x9a, 0x93, 0xb0); // secondary text
    const ImVec4 kInkFaint  = RGB(0x6b, 0x64, 0x82); // tertiary text
    const ImVec4 kDanger    = RGB(0xf8, 0x71, 0x71);
    const ImVec4 kWarn      = RGB(0xf5, 0xb1, 0x4c);
    const ImVec4 kOk        = RGB(0x4a, 0xd9, 0x91);
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

    style.WindowRounding    = 12.0f;
    style.ChildRounding     = 14.0f;
    style.FrameRounding     = 8.0f;
    style.PopupRounding     = 10.0f;
    style.ScrollbarRounding = 8.0f;
    style.GrabRounding      = 8.0f;
    style.TabRounding       = 8.0f;

    style.WindowBorderSize  = 1.0f;
    style.ChildBorderSize   = 1.0f;
    style.FrameBorderSize   = 1.0f;
    style.PopupBorderSize   = 1.0f;

    style.WindowPadding     = ImVec2(24, 24);
    style.FramePadding      = ImVec2(12, 9);
    style.ItemSpacing       = ImVec2(12, 12);
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
    ImVec4 Pink()    { return kPink; }
    ImVec4 Ok()      { return kOk; }
    ImVec4 Warn()    { return kWarn; }
    ImVec4 Danger()  { return kDanger; }
    ImVec4 Ink()     { return kInk; }
    ImVec4 InkDim()  { return kInkDim; }
    ImVec4 InkFaint(){ return kInkFaint; }
    ImVec4 Line()    { return kLine; }
    ImVec4 BgPanel() { return kBgPanel; }
    ImVec4 BgPanel2(){ return kBgPanel2; }
    ImVec4 BgPanel3(){ return kBgPanel3; }

    ImU32 U32(const ImVec4& c) { return ImGui::ColorConvertFloat4ToU32(c); }
}

namespace NasakiFonts
{
    static ImFont* s_body = nullptr;
    static ImFont* s_heading = nullptr;
    static ImFont* s_title = nullptr;

    void Set(ImFont* body, ImFont* heading, ImFont* title)
    {
        s_body = body;
        s_heading = heading;
        s_title = title;
    }

    ImFont* Body() { return s_body; }
    ImFont* Heading() { return s_heading; }
    ImFont* Title() { return s_title; }
}
