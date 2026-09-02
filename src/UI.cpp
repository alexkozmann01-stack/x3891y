#include "UI.h"

#include <vector>

namespace NasakiUI
{
    void DrawIcon(ImDrawList* dl, Icon icon, ImVec2 c, float s, ImU32 col)
    {
        const float half = s * 0.5f;
        const float scaledThickness = s * 0.11f;
        const float thickness = scaledThickness > 1.3f ? scaledThickness : 1.3f; // ImMax is imgui_internal.h-only

        switch (icon)
        {
        case Icon::Grid:
        {
            float gap = s * 0.16f;
            float cell = (s - gap) * 0.5f;
            ImVec2 origin(c.x - half, c.y - half);
            for (int row = 0; row < 2; row++)
            {
                for (int colIdx = 0; colIdx < 2; colIdx++)
                {
                    ImVec2 p0(origin.x + colIdx * (cell + gap), origin.y + row * (cell + gap));
                    dl->AddRectFilled(p0, ImVec2(p0.x + cell, p0.y + cell), col, 1.5f);
                }
            }
            break;
        }
        case Icon::Bars:
        {
            const float barW = s * 0.22f;
            const float gap = s * 0.12f;
            const float heights[3] = { s * 0.5f, s * 0.85f, s * 1.05f };
            const float totalW = barW * 3 + gap * 2;
            const float startX = c.x - totalW * 0.5f;
            const float baseY = c.y + half + s * 0.05f;
            for (int i = 0; i < 3; i++)
            {
                float x0 = startX + i * (barW + gap);
                dl->AddRectFilled(ImVec2(x0, baseY - heights[i]), ImVec2(x0 + barW, baseY), col, 1.0f);
            }
            break;
        }
        case Icon::Sliders:
        {
            const float lineLen = s * 0.95f;
            const float knobR = s * 0.1f;
            const float rowGap = s * 0.34f;
            const float knobT[3] = { -0.22f, 0.28f, -0.32f };
            for (int i = 0; i < 3; i++)
            {
                float y = c.y - rowGap + i * rowGap;
                dl->AddLine(ImVec2(c.x - lineLen * 0.5f, y), ImVec2(c.x + lineLen * 0.5f, y), col, thickness);
                dl->AddCircleFilled(ImVec2(c.x + knobT[i] * lineLen, y), knobR, col);
            }
            break;
        }
        case Icon::Minimize:
        {
            float w = s * 0.5f;
            dl->AddLine(ImVec2(c.x - w, c.y), ImVec2(c.x + w, c.y), col, thickness);
            break;
        }
        case Icon::Close:
        {
            float w = s * 0.36f;
            dl->AddLine(ImVec2(c.x - w, c.y - w), ImVec2(c.x + w, c.y + w), col, thickness);
            dl->AddLine(ImVec2(c.x - w, c.y + w), ImVec2(c.x + w, c.y - w), col, thickness);
            break;
        }
        }
    }

    bool NavItem(const char* id, const char* label, Icon icon, bool active)
    {
        ImVec2 size(ImGui::GetContentRegionAvail().x, 40);
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImVec2 p1(p0.x + size.x, p0.y + size.y);

        ImGui::InvisibleButton(id, size);
        bool hovered = ImGui::IsItemHovered();
        bool clicked = ImGui::IsItemClicked();

        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (active)
        {
            dl->AddRectFilled(p0, p1, IM_COL32(127, 214, 255, 22), 6.0f);
            dl->AddRectFilled(ImVec2(p0.x, p0.y + 7), ImVec2(p0.x + 3, p1.y - 7), IM_COL32(127, 214, 255, 255), 2.0f);
        }
        else if (hovered)
        {
            dl->AddRectFilled(p0, p1, IM_COL32(255, 255, 255, 8), 6.0f);
        }

        ImU32 fg = active ? IM_COL32(127, 214, 255, 255) : IM_COL32(139, 150, 179, 255);
        DrawIcon(dl, icon, ImVec2(p0.x + 26, p0.y + size.y * 0.5f), 16.0f, fg);

        ImU32 textCol = active ? IM_COL32(238, 243, 251, 255) : IM_COL32(139, 150, 179, 255);
        ImVec2 textSize = ImGui::CalcTextSize(label);
        dl->AddText(ImVec2(p0.x + 46, p0.y + (size.y - textSize.y) * 0.5f), textCol, label);

        return clicked;
    }

    bool TitleBarButton(const char* id, Icon icon, ImVec2 size, ImU32 hoverBg)
    {
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImVec2 p1(p0.x + size.x, p0.y + size.y);
        ImVec2 center((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f);

        ImGui::InvisibleButton(id, size);
        bool hovered = ImGui::IsItemHovered();
        bool clicked = ImGui::IsItemClicked();

        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (hovered)
        {
            dl->AddRectFilled(p0, p1, hoverBg, 5.0f);
        }
        DrawIcon(dl, icon, center, size.x * 0.5f, IM_COL32(238, 243, 251, 255));

        return clicked;
    }

    void AreaChart(
        ImVec2 pos, ImVec2 size,
        const char* label,
        const float* values, int count, int offset,
        float minV, float maxV,
        ImU32 lineColor, ImU32 fillColor)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p1(pos.x + size.x, pos.y + size.y);

        // Card background + border — same treatment as the site's
        // .bench-chart panel — so the chart reads as one bounded, intentional
        // element instead of text and a line floating in open space.
        dl->AddRectFilled(pos, p1, IM_COL32(14, 20, 36, 255), 8.0f);
        dl->AddRect(pos, p1, IM_COL32(28, 39, 64, 255), 8.0f, 0, 1.0f);

        bool hasLabel = label && *label;
        if (hasLabel)
        {
            dl->AddText(ImVec2(pos.x + 14, pos.y + 10), IM_COL32(139, 150, 179, 255), label);
        }

        float topPad = hasLabel ? 30.0f : 14.0f;
        ImVec2 plotPos(pos.x + 14, pos.y + topPad);
        ImVec2 plotSize(size.x - 28, size.y - topPad - 14.0f);

        if (plotSize.x > 1.0f && plotSize.y > 1.0f)
        {
            for (int g = 1; g <= 3; g++)
            {
                float y = plotPos.y + plotSize.y * g / 4.0f;
                dl->AddLine(ImVec2(plotPos.x, y), ImVec2(plotPos.x + plotSize.x, y), IM_COL32(255, 255, 255, 18));
            }

            if (count >= 2)
            {
                float range = maxV - minV;
                if (range <= 0.0f) range = 1.0f;

                std::vector<ImVec2> pts(count);
                for (int i = 0; i < count; i++)
                {
                    float t = (float)i / (float)(count - 1);
                    float raw = values[(i + offset) % count];
                    float v = (raw - minV) / range;
                    if (v < 0.0f) v = 0.0f;
                    if (v > 1.0f) v = 1.0f;
                    pts[i] = ImVec2(plotPos.x + t * plotSize.x, plotPos.y + plotSize.y * (1.0f - v));
                }

                // Fill: one convex trapezoid per segment (always convex even
                // when the curve itself isn't, unlike a single
                // AddConvexPolyFilled over the whole area-under-curve shape).
                float baseline = plotPos.y + plotSize.y;
                for (int i = 0; i + 1 < count; i++)
                {
                    ImVec2 a = pts[i], b = pts[i + 1];
                    dl->AddQuadFilled(a, b, ImVec2(b.x, baseline), ImVec2(a.x, baseline), fillColor);
                }

                // Line: a wide faint pass underneath a crisp one on top, cheap
                // stand-in for the site's drop-shadow glow on its accent lines.
                ImU32 glow = (lineColor & 0x00FFFFFF) | (0x50u << 24);
                dl->AddPolyline(pts.data(), count, glow, 0, 6.0f);
                dl->AddPolyline(pts.data(), count, lineColor, 0, 2.2f);
            }
        }

        ImGui::Dummy(size);
    }

    void BackgroundGlow(ImDrawList* dl, ImVec2 center, float radius, ImU32 color)
    {
        // Cheap radial-gradient approximation: concentric filled circles,
        // outer ones fainter, since ImDrawList has no native radial fill.
        const int steps = 5;
        ImU32 baseAlpha = (color >> 24) & 0xFF;
        for (int i = steps; i >= 1; i--)
        {
            float t = (float)i / (float)steps;
            ImU32 a = (ImU32)(baseAlpha * (1.0f - t) * (1.0f - t));
            ImU32 col = (color & 0x00FFFFFF) | (a << 24);
            dl->AddCircleFilled(center, radius * t, col, 64);
        }
    }
}
