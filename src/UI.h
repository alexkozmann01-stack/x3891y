#pragma once

#include "imgui.h"

// Small hand-drawn (ImDrawList) UI pieces used instead of Dear ImGui's bare
// default widgets — plain ImGui::Button/PlotLines reads as programmer-art;
// these exist to close some of the gap to the nasaki.eu website's actual
// design. Everything here is deliberately simple geometry (rects, lines,
// circles) rather than an icon font or image assets, so there's nothing
// external to load or that can go missing.
namespace NasakiUI
{
    enum class Icon
    {
        Grid,      // Prehľad
        Bars,      // Výkon
        Sliders,   // Nastavenia
        Minimize,
        Close,
    };

    void DrawIcon(ImDrawList* dl, Icon icon, ImVec2 center, float size, ImU32 color);

    // A sidebar-style nav row: icon + label, rounded hover background, a
    // left accent bar when active. Returns true the frame it's clicked.
    bool NavItem(const char* id, const char* label, Icon icon, bool active);

    // A window-corner-button style control (minimize/close in the custom
    // title bar): transparent until hovered, then a soft rounded highlight.
    bool TitleBarButton(const char* id, Icon icon, ImVec2 size, ImU32 hoverBg);

    // A bordered, filled card (like the site's .bench-chart panel) containing
    // a label and a line chart with a visible fill under the curve, a
    // two-pass glow on the line, and 3 guide lines — instead of
    // ImGui::PlotLines' bare single-pixel line floating with no boundary.
    // `values`/`count`/`offset` follow ImGui::PlotLines' own convention:
    // reads values[(i + offset) % count] for i in [0, count).
    void AreaChart(
        ImVec2 pos, ImVec2 size,
        const char* label,
        const float* values, int count, int offset,
        float minV, float maxV,
        ImU32 lineColor, ImU32 fillColor);

    // Soft radial-ish glow (a few overlapping faint filled circles), behind
    // content, mirroring the site's `.bg-glow` background — call before
    // drawing anything else in that region.
    void BackgroundGlow(ImDrawList* dl, ImVec2 center, float radius, ImU32 color);

    // A pill-shaped switch with a sliding knob, colored with the accent
    // when on — drop-in replacement for ImGui::Checkbox (same signature:
    // mutates *value, returns true the frame it's toggled) but doesn't look
    // like a stock ImGui checkbox square.
    bool Toggle(const char* label, bool* value);
}
