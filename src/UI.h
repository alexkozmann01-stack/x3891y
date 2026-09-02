#pragma once

#include "imgui.h"

// Custom-drawn (ImDrawList) UI pieces used instead of Dear ImGui's bare
// default widgets — plain ImGui::Button/PlotLines reads as programmer-art.
// Icons are real Font Awesome glyphs (see src/Icons.h) merged into the text
// fonts, so anywhere an `icon` parameter appears it's one of the ICON_*
// UTF-8 string literals from that header.
namespace NasakiUI
{
    // Frame-rate-independent ease toward `target`, with the current value
    // kept in ImGui's per-window state storage under `id` — the immediate
    // mode equivalent of an animated property. First frame starts at the
    // target so nothing flashes in from zero on appear.
    float AnimateTo(ImGuiID id, float target, float speed = 14.0f);

    // Draws an icon glyph centered on `center` using the current font.
    void DrawIconAt(ImDrawList* dl, const char* icon, ImVec2 center, ImU32 color);

    // A sidebar-style nav row: icon + label, rounded hover background, a
    // left accent bar when active. Returns true the frame it's clicked.
    bool NavItem(const char* id, const char* label, const char* icon, bool active);

    // A window-corner-button style control (minimize/close in the custom
    // title bar): transparent until hovered, then a soft rounded highlight.
    bool TitleBarButton(const char* id, const char* icon, ImVec2 size, ImU32 hoverBg);

    // A filled, accent-gradient primary button — the one loud call to
    // action per screen (activate license, start session).
    bool GradientButton(const char* label, ImVec2 size);

    // A rounded search field with a leading magnifier icon. Returns true
    // when the text changed.
    bool SearchField(const char* id, const char* hint, char* buffer, size_t bufferSize, float width);

    struct ChartSeries
    {
        const char* name;
        const float* values;
        ImU32 color;
    };

    // Several series in one card with a legend, instead of a stack of
    // separate chart boxes — three bordered panels down the page read as
    // clutter, one panel with three lines reads as a chart.
    void MultiAreaChart(
        ImVec2 pos, ImVec2 size, const char* label,
        const ChartSeries* series, int seriesCount,
        int count, int offset, float minV, float maxV);

    // Soft radial-ish glow (a few overlapping faint filled circles), behind
    // content, mirroring the site's `.bg-glow` background — call before
    // drawing anything else in that region.
    void BackgroundGlow(ImDrawList* dl, ImVec2 center, float radius, ImU32 color);

    // A pill-shaped switch with a sliding knob, colored with the accent
    // when on — drop-in replacement for ImGui::Checkbox (same signature:
    // mutates *value, returns true the frame it's toggled) but doesn't look
    // like a stock ImGui checkbox square.
    bool Toggle(const char* label, bool* value);

    // A small rounded pill (e.g. "Nové", "Notebook"), drawn at a screen
    // position. Returns its width so callers can right-align a row of them.
    float BadgeAt(ImDrawList* dl, ImVec2 pos, const char* text, ImU32 fg, ImU32 bg);

    // A settings card: icon + title (+ optional badge), a wrapped
    // description, and a toggle in the bottom-right corner. The whole card
    // is the click target. Returns true the frame it's toggled.
    // Occupies exactly (width x SettingCardHeight()) — position it yourself
    // (App.cpp lays these out as an explicit grid) rather than relying on
    // ImGui's cursor flow.
    float SettingCardHeight();
    bool SettingCard(
        const char* id, const char* icon, const char* title, const char* description,
        bool* value, float width, const char* badge = nullptr, float alpha = 1.0f);

    // What the user clicked on a game card, if anything.
    enum class GameCardAction
    {
        None,
        Launch,        // start the game through its launcher
        StartSession,  // it's already running — optimize it
    };

    // A game-library entry: name, launcher badge, install path, and an
    // action button whose meaning depends on whether the game is running.
    float GameCardHeight();
    GameCardAction GameCard(
        const char* id, const char* name, const char* source, const char* path,
        bool running, bool launchable, float width, float alpha = 1.0f);

    // Dim, small, letter-spaced-looking group label for sidebar sections
    // ("Optimalizácie", "Nástroje").
    void SectionLabel(const char* text);
}
