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

    // Line chart with a soft fill under the curve and a two-pass glow on the
    // line itself, plus 3 faint horizontal guide lines — the same visual
    // language as the benchmark chart on the landing page, instead of
    // ImGui::PlotLines' bare single-pixel line.
    // `values`/`count`/`offset` follow ImGui::PlotLines' own convention:
    // reads values[(i + offset) % count] for i in [0, count).
    void AreaChart(
        ImVec2 pos, ImVec2 size,
        const float* values, int count, int offset,
        float minV, float maxV,
        ImU32 lineColor, ImU32 fillColor);

    // Soft radial-ish glow (a few overlapping faint filled circles), behind
    // content, mirroring the site's `.bg-glow` background — call before
    // drawing anything else in that region.
    void BackgroundGlow(ImDrawList* dl, ImVec2 center, float radius, ImU32 color);
}
