// ============================================================================
// hud/marker_label.h
// Rider marker labels — the pure formatting/color core shared by MapHud,
// RadarHud and GapBarHud.
//
// WHAT THIS IS. The three HUDs that draw per-rider marker icons (map, radar,
// gap bar) label them identically: a mode (position / race number / both), a
// "P%d [#%d]" text format, and podium colors for P1/P2/P3. One copy of the
// LabelMode enum, the format switch and the podium-color pick keeps the three
// agreeing by construction rather than by review.
//
// WHY IT LIVES HERE AND NOT IN BaseHud. Everything here is a pure function of
// a few numbers, so it compiles with a plain g++ and no game and is exercised by
// tests/unit/test_marker_label.cpp in ~1s instead of only through the DLL under
// Wine. BaseHud would drag the whole render stack in for arithmetic that needs
// none of it.
//
// GEOMETRY LIVES HERE TOO. All three HUDs take the shared drop shadow (which
// modulates by the string's own alpha, so the radar's fade survives without an
// outline), the gap is one formula, and the HUDs differ only in two PARAMETERS
// -- which anchor, and whether the local player's boost applies. Those are
// arguments, so place() takes them.
//
// What is per-HUD is genuinely per-HUD: where the icon IS (a track
// position, a radar bearing, a gap along a bar) and what colour it fades to.
// Nothing about that decides where the text sits relative to the icon.
//
// The per-HUD `LabelMode` names are aliases of Mode so existing call sites
// (`RadarHud::LabelMode::POSITION`) and the settings serde int casts keep
// working unchanged. Numeric values are the on-disk INI representation — don't
// renumber.
// ============================================================================
#pragma once

#include <cstdio>

#include "../core/plugin_constants.h"

namespace MarkerLabel {

    enum class Mode {
        NONE = 0,       // No labels
        POSITION = 1,   // Show position (P1, P2, etc.)
        RACE_NUM = 2,   // Show race number
        BOTH = 3        // Show both (P1 #5)
    };

    // Builds the label text for a rider marker. position <= 0 means "no
    // position data": POSITION mode then renders nothing, BOTH falls back to
    // the bare race number. Returns true when buf holds a non-empty label;
    // false means skip rendering (buf is set to "").
    inline bool format(Mode mode, int position, int raceNum,
                       char* buf, size_t bufSize) {
        buf[0] = '\0';
        switch (mode) {
            case Mode::POSITION:
                if (position > 0) {
                    snprintf(buf, bufSize, "P%d", position);
                }
                break;
            case Mode::RACE_NUM:
                snprintf(buf, bufSize, "%d", raceNum);
                break;
            case Mode::BOTH:
                if (position > 0) {
                    snprintf(buf, bufSize, "P%d #%d", position, raceNum);
                } else {
                    snprintf(buf, bufSize, "#%d", raceNum);
                }
                break;
            default:
                break;
        }
        return buf[0] != '\0';
    }

    // Label color: podium gold/silver/bronze for P1/P2/P3 when the mode shows
    // positions, otherwise the caller's default. Callers post-process (opacity
    // fade on the radar) — this only picks the base color.
    inline unsigned long color(Mode mode, int position, unsigned long defaultColor) {
        using namespace PluginConstants;
        if (mode == Mode::POSITION || mode == Mode::BOTH) {
            if (position == Position::FIRST) return PodiumColors::GOLD;
            if (position == Position::SECOND) return PodiumColors::SILVER;
            if (position == Position::THIRD) return PodiumColors::BRONZE;
        }
        return defaultColor;
    }

    // WHERE THE ICON IS relative to its label. Numeric values are the on-disk INI
    // representation (MapHud persists this setting as a NAME rather than a number,
    // but keep them stable anyway) -- don't renumber.
    enum class Anchor {
        BELOW = 0,   // Centred under the icon (default)
        ABOVE = 1,   // Centred over the icon
        LEFT  = 2,   // Left of the icon, right-aligned
        RIGHT = 3    // Right of the icon, left-aligned
    };

    // The local player's own marker -- and the label with it -- draws a touch larger
    // than the pack so it is easy to pick out at a glance; the accent colour alone can
    // be hard to spot on a busy map. Applied to the ICON size, which the label's own
    // size and offsets are then measured from, so the two grow together.
    //
    // RadarHud never draws the player (the player IS the radar's centre, and its rider
    // loop skips displayRaceNum), so the boost is simply never asked for there. That is
    // not an exception to the rule, it is the rule with no player to apply it to.
    constexpr float PLAYER_BOOST = 1.35f;
    inline float boost(bool isLocalPlayer) { return isLocalPlayer ? PLAYER_BOOST : 1.0f; }

    // The gap between the icon's edge and the label, as a fraction of the icon's
    // half-size. Of the ICON, not of the font: the label hangs off the icon, so that
    // is what it should track -- and an icon-relative gap picks up marker scale and
    // HUD scale for free, both of which a font-relative one would miss.
    constexpr float GAP_RATIO = 0.2f;

    // A side anchor centres the label vertically on the icon. The string's y is the
    // LINE BOX top and the glyph sits below it by the font's leading, so 0.625 (not
    // 0.5) of the em lands the visible text on the centreline. Empirical, and the one
    // number here that is not derived.
    constexpr float SIDE_CENTER_RATIO = 0.625f;

    // HOW FAR TO MOVE THE ICON so the icon and its label sit centred in their box
    // TOGETHER, rather than the icon centred and the label hanging off one end.
    //
    // For a box with room to spare this is not worth doing -- the map and the radar
    // draw markers on open ground and an overhanging label costs nothing. It matters
    // where the box is TIGHT: the gap bar's is one text row tall, so a BELOW label
    // half-hangs out of the panel it belongs to.
    //
    // Only the stacked anchors shift. A side label is already centred on the icon's
    // own line, so the pair is centred exactly when the icon is.
    inline float blockCenterShift(Anchor anchor, float iconHalfSize, float fontSize) {
        const float half = (iconHalfSize * GAP_RATIO + fontSize) * 0.5f;
        switch (anchor) {
            case Anchor::BELOW: return -half;   // label below -> icon rides up
            case Anchor::ABOVE: return  half;   // ...and the other way round
            default:            return 0.0f;
        }
    }

    struct Placement {
        float x = 0.0f;
        float y = 0.0f;
        int justify = PluginConstants::Justify::CENTER;
    };

    // WHERE TO DRAW A MARKER'S LABEL. `iconHalfSize` is the icon's half-HEIGHT in
    // screen units, already carrying marker scale, HUD scale and the player boost;
    // `fontSize` is the label's, already carrying the same boost. Icons are
    // aspect-corrected, so the horizontal half is the vertical one divided by the
    // aspect -- which is why a side anchor cannot just reuse iconHalfSize.
    inline Placement place(Anchor anchor, float iconCenterX, float iconCenterY,
                           float iconHalfSize, float fontSize) {
        using namespace PluginConstants;
        const float gap = iconHalfSize * GAP_RATIO;
        const float halfWidth = iconHalfSize / UI_ASPECT_RATIO;
        Placement p;
        p.x = iconCenterX;
        switch (anchor) {
            case Anchor::ABOVE:
                p.y = iconCenterY - iconHalfSize - gap - fontSize;
                break;
            case Anchor::LEFT:
                p.x = iconCenterX - halfWidth - gap;
                p.y = iconCenterY - fontSize * SIDE_CENTER_RATIO;
                p.justify = Justify::RIGHT;
                break;
            case Anchor::RIGHT:
                p.x = iconCenterX + halfWidth + gap;
                p.y = iconCenterY - fontSize * SIDE_CENTER_RATIO;
                p.justify = Justify::LEFT;
                break;
            case Anchor::BELOW:
            default:
                p.y = iconCenterY + iconHalfSize + gap;
                break;
        }
        return p;
    }

}  // namespace MarkerLabel
