// ============================================================================
// hud/dropdown_list.h
// The DROPDOWN's drawing, shared by every HUD that offers one: the closed box
// (a tinted field with a caret) and the open list under or over it. The
// settings panel was its first host (settings_controls.h); the Records HUD's
// category filter is the second. Each host keeps its own state and click
// regions -- the two speak different region types -- and hands this the
// geometry, the entries and the colours, so a dropdown looks the same
// wherever it is.
// ============================================================================
#pragma once

#include <functional>
#include <string>
#include <vector>

class BaseHud;

class DropdownList {
public:
    // The colours and face a host draws its dropdown in. The settings panel
    // passes the global palette; a HUD passes its own (getColor/getFont).
    struct Style {
        unsigned long primary = 0, secondary = 0, muted = 0, accent = 0, background = 0;
        int font = 0;
    };
    // The global palette and the normal face: the settings panel's style.
    static Style globalStyle();

    // The closed control: the box around a value field `width` wide at x (the
    // box takes a little air to the value's left) and the caret in its last
    // cell, pointing down while shut and up while `open`.
    static void drawBox(BaseHud& hud, float x, float rowY, float width, float rowH,
                        float fontSize, bool enabled, bool open, const Style& style);

    // The open list's anchor: the box (x, y, w) and where its text starts.
    struct Anchor {
        float x = 0.0f, y = 0.0f, w = 0.0f, textX = 0.0f, rowH = 0.0f, fontSize = 0.0f;
    };
    // The space the list must stay inside: the host's panel.
    struct Bounds {
        float left = 0.0f, top = 0.0f, right = 0.0f, bottom = 0.0f;
    };
    // The entries. nameOf is required; spriteOf opens the list as a grid of
    // icons, swatchOf leads each name with its colour, fontOf previews a face.
    struct Entries {
        int count = 0;
        int current = -1;   // drawn selected
        int hovered = -1;   // drawn lit (an icon grid's caption names it)
        std::function<std::string(int)> nameOf;
        std::function<int(int)> spriteOf;
        std::function<unsigned long(int)> swatchOf;
        std::function<int(int)> fontOf;
    };
    struct Rect {
        float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
    };

    // Draw the open list, LAST, over the host's rows, and return each entry's
    // rect (build space, index = entry) for the host's click regions. See the
    // definition for placement and how it covers the text beneath.
    static void build(BaseHud& hud, const Anchor& anchor, const Bounds& bounds,
                      const Entries& entries, const Style& style, std::vector<Rect>& outEntries);
};
