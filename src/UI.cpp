#include "UI.h"
#include "Theme.h" // NasakiFonts, for the card title font
#include "Icons.h" // ICON_* glyphs

#include <vector>
#include <string>
#include <cmath>

namespace
{
    // Routes a hardcoded color through ImGui so it picks up the pushed
    // ImGuiStyleVar_Alpha (used for the view fade-in) plus any per-widget
    // alpha the caller passes for staggered entrances.
    ImU32 Col(ImU32 c, float alpha = 1.0f)
    {
        return ImGui::GetColorU32(c, alpha);
    }

    ImU32 Lerp(ImU32 a, ImU32 b, float t)
    {
        ImVec4 av = ImGui::ColorConvertU32ToFloat4(a);
        ImVec4 bv = ImGui::ColorConvertU32ToFloat4(b);
        ImVec4 out(
            av.x + (bv.x - av.x) * t,
            av.y + (bv.y - av.y) * t,
            av.z + (bv.z - av.z) * t,
            av.w + (bv.w - av.w) * t);
        return ImGui::ColorConvertFloat4ToU32(out);
    }
}

namespace NasakiUI
{
    void DrawIconAt(ImDrawList* dl, const char* icon, ImVec2 center, ImU32 col)
    {
        ImVec2 size = ImGui::CalcTextSize(icon);
        dl->AddText(ImVec2(center.x - size.x * 0.5f, center.y - size.y * 0.5f), col, icon);
    }

    float AnimateTo(ImGuiID id, float target, float speed)
    {
        ImGuiStorage* storage = ImGui::GetStateStorage();
        float current = storage->GetFloat(id, target);
        float dt = ImGui::GetIO().DeltaTime;
        if (dt > 0.0f)
        {
            current += (target - current) * (1.0f - std::exp(-speed * dt));
        }
        storage->SetFloat(id, current);
        return current;
    }

    bool NavItem(const char* id, const char* label, const char* icon, bool active)
    {
        ImVec2 size(ImGui::GetContentRegionAvail().x, 42);
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImVec2 p1(p0.x + size.x, p0.y + size.y);

        ImGui::InvisibleButton(id, size);
        bool hovered = ImGui::IsItemHovered();
        bool clicked = ImGui::IsItemClicked();

        ImGuiID animId = ImGui::GetID(id);
        float activeT = AnimateTo(animId, active ? 1.0f : 0.0f, 14.0f);
        float hoverT = AnimateTo(animId + 1, hovered ? 1.0f : 0.0f, 14.0f);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImU32 bg = Lerp(IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, 10), hoverT);
        bg = Lerp(bg, IM_COL32(127, 214, 255, 26), activeT);
        dl->AddRectFilled(p0, p1, Col(bg), 8.0f);
        if (activeT > 0.01f)
        {
            dl->AddRectFilled(ImVec2(p0.x, p0.y + 8), ImVec2(p0.x + 3, p1.y - 8),
                Col(IM_COL32(127, 214, 255, 255), activeT), 2.0f);
        }

        ImU32 fg = Col(Lerp(IM_COL32(139, 150, 179, 255), IM_COL32(127, 214, 255, 255), activeT));
        DrawIconAt(dl, icon, ImVec2(p0.x + 26, p0.y + size.y * 0.5f), fg);

        ImU32 textCol = Col(Lerp(IM_COL32(139, 150, 179, 255), IM_COL32(238, 243, 251, 255), activeT));
        ImVec2 textSize = ImGui::CalcTextSize(label);
        dl->AddText(ImVec2(p0.x + 48, p0.y + (size.y - textSize.y) * 0.5f), textCol, label);

        return clicked;
    }

    bool TitleBarButton(const char* id, const char* icon, ImVec2 size, ImU32 hoverBg)
    {
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImVec2 p1(p0.x + size.x, p0.y + size.y);
        ImVec2 center((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f);

        ImGui::InvisibleButton(id, size);
        bool hovered = ImGui::IsItemHovered();
        bool clicked = ImGui::IsItemClicked();

        float hoverT = AnimateTo(ImGui::GetID(id), hovered ? 1.0f : 0.0f, 16.0f);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p0, p1, Col(hoverBg, hoverT), 7.0f);
        DrawIconAt(dl, icon, center, Col(IM_COL32(238, 243, 251, 255)));

        return clicked;
    }

    bool GradientButton(const char* label, ImVec2 size)
    {
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        if (size.x <= 0.0f) size.x = ImGui::GetContentRegionAvail().x;
        if (size.y <= 0.0f) size.y = 40.0f;
        ImVec2 p1(p0.x + size.x, p0.y + size.y);

        ImGui::InvisibleButton(label, size);
        bool hovered = ImGui::IsItemHovered();
        bool clicked = ImGui::IsItemClicked();
        float hoverT = AnimateTo(ImGui::GetID(label), hovered ? 1.0f : 0.0f, 16.0f);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        // Horizontal accent -> accent-2 gradient, brightened on hover.
        ImU32 left = Col(Lerp(IM_COL32(47, 127, 252, 255), IM_COL32(86, 158, 255, 255), hoverT));
        ImU32 right = Col(Lerp(IM_COL32(127, 214, 255, 255), IM_COL32(168, 230, 255, 255), hoverT));
        dl->AddRectFilledMultiColor(p0, p1, left, right, right, left);
        // AddRectFilledMultiColor can't round corners, so re-cut them by
        // overdrawing the corner gaps with the panel background.
        dl->AddRect(p0, p1, Col(IM_COL32(127, 214, 255, 90)), 10.0f, 0, 1.0f);

        ImVec2 textSize = ImGui::CalcTextSize(label);
        dl->AddText(
            ImVec2(p0.x + (size.x - textSize.x) * 0.5f, p0.y + (size.y - textSize.y) * 0.5f),
            Col(IM_COL32(4, 7, 15, 255)), label);

        return clicked;
    }

    bool SearchField(const char* id, const char* hint, char* buffer, size_t bufferSize, float width)
    {
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        const float height = 38.0f;

        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p0, ImVec2(p0.x + width, p0.y + height), Col(IM_COL32(14, 20, 36, 255)), 10.0f);
        dl->AddRect(p0, ImVec2(p0.x + width, p0.y + height), Col(IM_COL32(28, 39, 64, 255)), 10.0f, 0, 1.0f);
        DrawIconAt(dl, ICON_SEARCH, ImVec2(p0.x + 20, p0.y + height * 0.5f), Col(IM_COL32(110, 122, 150, 255)));

        // The actual input sits inside the drawn box, borderless.
        ImGui::SetCursorScreenPos(ImVec2(p0.x + 38, p0.y + (height - ImGui::GetFrameHeight()) * 0.5f));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::SetNextItemWidth(width - 50.0f);
        bool changed = ImGui::InputTextWithHint(id, hint, buffer, bufferSize);
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();

        ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y));
        ImGui::Dummy(ImVec2(width, height));
        return changed;
    }

    void MultiAreaChart(
        ImVec2 pos, ImVec2 size, const char* label,
        const ChartSeries* series, int seriesCount,
        int count, int offset, float minV, float maxV)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p1(pos.x + size.x, pos.y + size.y);

        dl->AddRectFilled(pos, p1, Col(IM_COL32(14, 20, 36, 255)), 14.0f);
        dl->AddRect(pos, p1, Col(IM_COL32(28, 39, 64, 255)), 14.0f, 0, 1.0f);

        if (label && *label)
        {
            dl->AddText(ImVec2(pos.x + 20, pos.y + 16), Col(IM_COL32(139, 150, 179, 255)), label);
        }

        // Legend, right-aligned on the header row: a dot plus the series name.
        float legendX = p1.x - 20.0f;
        for (int s = seriesCount - 1; s >= 0; s--)
        {
            ImVec2 nameSize = ImGui::CalcTextSize(series[s].name);
            legendX -= nameSize.x;
            dl->AddText(ImVec2(legendX, pos.y + 16), Col(IM_COL32(139, 150, 179, 255)), series[s].name);
            legendX -= 10.0f;
            dl->AddCircleFilled(ImVec2(legendX, pos.y + 16 + nameSize.y * 0.5f), 4.0f, Col(series[s].color), 12);
            legendX -= 18.0f;
        }

        ImVec2 plotPos(pos.x + 20, pos.y + 46);
        ImVec2 plotSize(size.x - 40, size.y - 46 - 20);
        if (plotSize.x <= 1.0f || plotSize.y <= 1.0f)
        {
            ImGui::Dummy(size);
            return;
        }

        for (int g = 1; g <= 3; g++)
        {
            float y = plotPos.y + plotSize.y * g / 4.0f;
            dl->AddLine(ImVec2(plotPos.x, y), ImVec2(plotPos.x + plotSize.x, y), Col(IM_COL32(255, 255, 255, 16)));
        }

        if (count >= 2)
        {
            float range = maxV - minV;
            if (range <= 0.0f) range = 1.0f;
            float baseline = plotPos.y + plotSize.y;
            std::vector<ImVec2> pts(count);

            for (int s = 0; s < seriesCount; s++)
            {
                for (int i = 0; i < count; i++)
                {
                    float t = (float)i / (float)(count - 1);
                    float v = (series[s].values[(i + offset) % count] - minV) / range;
                    if (v < 0.0f) v = 0.0f;
                    if (v > 1.0f) v = 1.0f;
                    pts[i] = ImVec2(plotPos.x + t * plotSize.x, plotPos.y + plotSize.y * (1.0f - v));
                }

                // Fills are lighter here than in the single-series chart —
                // three stacked translucent areas would otherwise muddy into
                // one another.
                ImU32 fill = (series[s].color & 0x00FFFFFF) | (0x22u << 24);
                for (int i = 0; i + 1 < count; i++)
                {
                    dl->AddQuadFilled(pts[i], pts[i + 1],
                        ImVec2(pts[i + 1].x, baseline), ImVec2(pts[i].x, baseline), Col(fill));
                }
                dl->AddPolyline(pts.data(), count, Col(series[s].color), 0, 2.0f);
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

    bool Toggle(const char* label, bool* value)
    {
        ImVec2 size(40, 22);
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImVec2 p1(p0.x + size.x, p0.y + size.y);

        ImGui::InvisibleButton(label, size); // `label` doubles as the ImGui ID, same as ImGui::Checkbox
        bool changed = ImGui::IsItemClicked();
        bool hovered = ImGui::IsItemHovered();
        if (changed)
        {
            *value = !*value;
        }

        ImDrawList* dl = ImGui::GetWindowDrawList();
        // Slide/tint between states instead of snapping.
        float t = AnimateTo(ImGui::GetID(label), *value ? 1.0f : 0.0f, 16.0f);
        ImU32 onCol = hovered ? IM_COL32(85, 152, 255, 255) : IM_COL32(47, 127, 252, 255);
        ImU32 offCol = hovered ? IM_COL32(42, 56, 88, 255) : IM_COL32(28, 39, 64, 255);
        float radius = size.y * 0.5f;
        dl->AddRectFilled(p0, p1, Col(Lerp(offCol, onCol, t)), radius);

        float knobR = radius - 3.0f;
        float knobX = (p0.x + radius) + t * (size.x - radius * 2.0f);
        dl->AddCircleFilled(ImVec2(knobX, (p0.y + p1.y) * 0.5f), knobR, Col(IM_COL32(238, 243, 251, 255)));

        ImGui::SameLine();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (size.y - ImGui::GetTextLineHeight()) * 0.5f);
        ImGui::TextUnformatted(label);

        return changed;
    }

    float BadgeAt(ImDrawList* dl, ImVec2 pos, const char* text, ImU32 fg, ImU32 bg)
    {
        ImVec2 textSize = ImGui::CalcTextSize(text);
        const float padX = 9.0f;
        const float padY = 4.0f;
        ImVec2 p1(pos.x + textSize.x + padX * 2, pos.y + textSize.y + padY * 2);
        dl->AddRectFilled(pos, p1, bg, (p1.y - pos.y) * 0.5f);
        dl->AddText(ImVec2(pos.x + padX, pos.y + padY), fg, text);
        return p1.x - pos.x;
    }

    void SectionLabel(const char* text)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(110, 122, 150, 255));
        ImGui::TextUnformatted(text);
        ImGui::PopStyleColor();
    }

    float SettingCardHeight()
    {
        return 156.0f;
    }

    bool SettingCard(
        const char* id, const char* icon, const char* title, const char* description,
        bool* value, float width, const char* badge, float alpha)
    {
        const float height = SettingCardHeight();
        const float pad = 22.0f;

        ImVec2 localStart = ImGui::GetCursorPos();
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImVec2 p1(p0.x + width, p0.y + height);

        ImGui::InvisibleButton(id, ImVec2(width, height));
        bool hovered = ImGui::IsItemHovered();
        bool changed = ImGui::IsItemClicked();
        if (changed)
        {
            *value = !*value;
        }
        ImVec2 afterCursor = ImGui::GetCursorPos();

        ImGuiID baseId = ImGui::GetID(id);
        float hoverT = AnimateTo(baseId, hovered ? 1.0f : 0.0f, 12.0f);
        float onT = AnimateTo(baseId + 1, *value ? 1.0f : 0.0f, 16.0f);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImU32 bg = Lerp(IM_COL32(14, 20, 36, 255), IM_COL32(21, 30, 52, 255), hoverT);
        ImU32 border = Lerp(IM_COL32(28, 39, 64, 255), IM_COL32(47, 127, 252, 150), onT);
        dl->AddRectFilled(p0, p1, Col(bg, alpha), 14.0f);
        dl->AddRect(p0, p1, Col(border, alpha), 14.0f, 0, 1.0f);

        // Icon in a soft accent-tinted rounded square, like the reference app.
        ImVec2 iconBoxMin(p0.x + pad, p0.y + pad);
        ImVec2 iconBoxMax(iconBoxMin.x + 34, iconBoxMin.y + 34);
        dl->AddRectFilled(iconBoxMin, iconBoxMax, Col(IM_COL32(47, 127, 252, 38), alpha), 10.0f);
        DrawIconAt(dl, icon,
            ImVec2((iconBoxMin.x + iconBoxMax.x) * 0.5f, (iconBoxMin.y + iconBoxMax.y) * 0.5f),
            Col(IM_COL32(127, 214, 255, 255), alpha));

        if (badge && *badge)
        {
            ImVec2 badgeSize = ImGui::CalcTextSize(badge);
            float badgeW = badgeSize.x + 18.0f;
            BadgeAt(dl, ImVec2(p1.x - pad - badgeW, p0.y + pad + 4), badge,
                Col(IM_COL32(107, 227, 163, 255), alpha), Col(IM_COL32(107, 227, 163, 30), alpha));
        }

        // Title and description go through ImGui's text API (not
        // ImDrawList::AddText) so wrapping works; the cursor is put back
        // afterwards so the caller's layout isn't disturbed.
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * alpha);
        ImGui::SetCursorScreenPos(ImVec2(iconBoxMax.x + 12, p0.y + pad + 7));
        ImGui::PushFont(NasakiFonts::Heading());
        ImGui::TextUnformatted(title);
        ImGui::PopFont();

        ImGui::SetCursorScreenPos(ImVec2(p0.x + pad, p0.y + pad + 46));
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(139, 150, 179, 255));
        ImGui::PushTextWrapPos(localStart.x + width - pad);
        ImGui::TextUnformatted(description);
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();

        // Toggle, bottom-right.
        const ImVec2 tSize(42, 23);
        ImVec2 t0(p1.x - pad - tSize.x, p1.y - pad - tSize.y);
        ImVec2 t1(t0.x + tSize.x, t0.y + tSize.y);
        float radius = tSize.y * 0.5f;
        ImU32 track = Lerp(IM_COL32(30, 41, 66, 255), IM_COL32(47, 127, 252, 255), onT);
        dl->AddRectFilled(t0, t1, Col(track, alpha), radius);
        float knobX = (t0.x + radius) + onT * (tSize.x - radius * 2.0f);
        dl->AddCircleFilled(ImVec2(knobX, (t0.y + t1.y) * 0.5f), radius - 3.0f,
            Col(IM_COL32(238, 243, 251, 255), alpha), 20);

        ImGui::SetCursorPos(afterCursor);
        return changed;
    }

    float GameCardHeight()
    {
        return 116.0f;
    }

    GameCardAction GameCard(
        const char* id, const char* name, const char* source, const char* path,
        bool running, bool launchable, float width, float alpha)
    {
        const float height = GameCardHeight();
        const float pad = 20.0f;

        ImVec2 localStart = ImGui::GetCursorPos();
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImVec2 p1(p0.x + width, p0.y + height);

        ImGui::InvisibleButton(id, ImVec2(width, height));
        bool hovered = ImGui::IsItemHovered();
        ImVec2 afterCursor = ImGui::GetCursorPos();

        float hoverT = AnimateTo(ImGui::GetID(id), hovered ? 1.0f : 0.0f, 12.0f);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImU32 bg = Lerp(IM_COL32(14, 20, 36, 255), IM_COL32(21, 30, 52, 255), hoverT);
        ImU32 border = running ? IM_COL32(107, 227, 163, 130) : IM_COL32(28, 39, 64, 255);
        dl->AddRectFilled(p0, p1, Col(bg, alpha), 14.0f);
        dl->AddRect(p0, p1, Col(border, alpha), 14.0f, 0, 1.0f);

        ImVec2 iconBoxMin(p0.x + pad, p0.y + pad);
        ImVec2 iconBoxMax(iconBoxMin.x + 32, iconBoxMin.y + 32);
        dl->AddRectFilled(iconBoxMin, iconBoxMax, Col(IM_COL32(47, 127, 252, 38), alpha), 10.0f);
        DrawIconAt(dl, ICON_NAV_GAMES,
            ImVec2((iconBoxMin.x + iconBoxMax.x) * 0.5f, (iconBoxMin.y + iconBoxMax.y) * 0.5f),
            Col(IM_COL32(127, 214, 255, 255), alpha));

        // Launcher badge, and a "running" one next to it when live.
        float badgeX = p1.x - pad;
        if (running)
        {
            const char* runLabel = "Beží";
            float w = ImGui::CalcTextSize(runLabel).x + 18.0f;
            badgeX -= w;
            BadgeAt(dl, ImVec2(badgeX, p0.y + pad + 3), runLabel,
                Col(IM_COL32(107, 227, 163, 255), alpha), Col(IM_COL32(107, 227, 163, 30), alpha));
            badgeX -= 8.0f;
        }
        if (source && *source)
        {
            float w = ImGui::CalcTextSize(source).x + 18.0f;
            badgeX -= w;
            BadgeAt(dl, ImVec2(badgeX, p0.y + pad + 3), source,
                Col(IM_COL32(139, 150, 179, 255), alpha), Col(IM_COL32(139, 150, 179, 28), alpha));
        }

        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * alpha);
        ImGui::SetCursorScreenPos(ImVec2(iconBoxMax.x + 12, p0.y + pad + 6));
        ImGui::PushFont(NasakiFonts::Heading());
        ImGui::TextUnformatted(name);
        ImGui::PopFont();

        ImGui::SetCursorScreenPos(ImVec2(p0.x + pad, p0.y + pad + 44));
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(110, 122, 150, 255));
        ImGui::PushTextWrapPos(localStart.x + width - pad);
        ImGui::TextUnformatted((path && *path) ? path : "Cesta neznáma");
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();

        ImGui::PopStyleVar();

        // Action button, bottom-right: launch the game, or — if it's already
        // running — optimize it. Drawn after the card's own InvisibleButton
        // so it sits on top and wins the click.
        GameCardAction action = GameCardAction::None;
        const char* actionLabel = running ? (ICON_BOLT "  Optimalizovať") : (ICON_NAV_GAMES "  Spustiť");
        bool actionEnabled = running || launchable;
        if (actionEnabled)
        {
            ImVec2 btnSize(ImGui::CalcTextSize(actionLabel).x + 28.0f, 30.0f);
            ImVec2 b0(p1.x - pad - btnSize.x, p1.y - pad - btnSize.y + 4.0f);
            ImGui::SetCursorScreenPos(b0);

            std::string btnId = std::string(id) + "_action";
            ImGui::InvisibleButton(btnId.c_str(), btnSize);
            bool btnHover = ImGui::IsItemHovered();
            bool btnClick = ImGui::IsItemClicked();
            float btnT = AnimateTo(ImGui::GetID(btnId.c_str()), btnHover ? 1.0f : 0.0f, 16.0f);

            ImVec2 b1(b0.x + btnSize.x, b0.y + btnSize.y);
            ImU32 base = running ? IM_COL32(107, 227, 163, 40) : IM_COL32(47, 127, 252, 55);
            ImU32 hot = running ? IM_COL32(107, 227, 163, 80) : IM_COL32(47, 127, 252, 110);
            dl->AddRectFilled(b0, b1, Col(Lerp(base, hot, btnT), alpha), 9.0f);

            ImU32 fgCol = running ? IM_COL32(107, 227, 163, 255) : IM_COL32(127, 214, 255, 255);
            ImVec2 labelSize = ImGui::CalcTextSize(actionLabel);
            dl->AddText(
                ImVec2(b0.x + (btnSize.x - labelSize.x) * 0.5f, b0.y + (btnSize.y - labelSize.y) * 0.5f),
                Col(fgCol, alpha), actionLabel);

            if (btnClick)
            {
                action = running ? GameCardAction::StartSession : GameCardAction::Launch;
            }
        }

        ImGui::SetCursorPos(afterCursor);
        return action;
    }
}
