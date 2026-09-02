#pragma once

// The Font Awesome 6 Free (Solid) glyphs this app uses, as UTF-8 literals.
// The full set is 1400+ icons / 426 KB; assets/fonts/fa-subset.ttf is
// subset to just these (4.5 KB) and compiled in via src/IconFont.h, merged
// into each text font's atlas — so an icon is just another character in a
// string: ImGui::Text(ICON_NAV_GAMES "  Hry").
//
// Codepoints taken from IconFontCppHeaders' IconsFontAwesome6.h
// (third_party/icons). Adding an icon means re-subsetting the TTF with the
// new codepoint and regenerating IconFont.h — see README.
#define ICON_NAV_HOME      "\xef\x80\x95" // U+f015 house
#define ICON_NAV_PERF      "\xee\x91\xb3" // U+e473 chart-simple
#define ICON_NAV_GAMES     "\xef\x84\x9b" // U+f11b gamepad
#define ICON_NAV_SETTINGS  "\xef\x80\x93" // U+f013 gear

#define ICON_BOLT          "\xef\x83\xa7" // U+f0e7 bolt
#define ICON_LAYERS        "\xef\x97\xbd" // U+f5fd layer-group
#define ICON_THERMO        "\xef\x8b\x89" // U+f2c9 temperature-half
#define ICON_POWER         "\xef\x80\x91" // U+f011 power-off
#define ICON_CROSSHAIRS    "\xef\x81\x9b" // U+f05b crosshairs
#define ICON_CHIP          "\xef\x8b\x9b" // U+f2db microchip

#define ICON_SEARCH        "\xef\x80\x82" // U+f002 magnifying-glass
#define ICON_ROTATE        "\xef\x8b\xb1" // U+f2f1 rotate
#define ICON_CHECK         "\xef\x81\x98" // U+f058 circle-check
#define ICON_WARNING       "\xef\x81\xb1" // U+f071 triangle-exclamation
#define ICON_CLOSE         "\xef\x80\x8d" // U+f00d xmark
#define ICON_MINIMIZE      "\xef\x81\xa8" // U+f068 minus

// The merged icon range, for ImFontAtlas::AddFont* calls.
#define ICON_RANGE_MIN 0xe005
#define ICON_RANGE_MAX 0xf8ff
