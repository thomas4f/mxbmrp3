// ============================================================================
// hud/gamepad_geometry.h
// The gamepad widget's unit system. Pure arithmetic — no rendering, no PluginData.
//
// THE WIDGET IS A PICTURE. Its background is a photograph of a controller, and
// every distance inside it — the three rows, the gaps, and the ~30 hand-placed
// per-pad offsets in PadGeometry below — was measured against that picture at one
// set of metrics. So the only honest reference for the interior is
// the picture's own width, not the global grid, which moves for reasons that have
// nothing to do with the artwork.
//
// The frame's width is set from the type ([Advanced] uiFontSize, the widget's own
// scale slider, and a theme's content inset all feed it). interiorEm() INVERTS that
// relation: given the width the frame actually came out, it returns the em that
// produced it, and every interior distance is spent in that em. Three inputs, one
// number, and similarity across all of them by construction.
//
// It used to be otherwise, and the bug is the reason this file exists: the offsets
// were scaled by the widget's scale slider ALONE while the frame around them also
// grew with uiFontSize. Raise the font size and the controller got bigger while
// every button stayed where it was — sticks, d-pad and face buttons walking off
// their sockets. A second symptom of the same split: the row pitch was pinned to
// `fontSize * 1.11` to survive a grid retune that had grown the rows relative to
// the frame. Both are gone; kLineRatio is that same 1.11, now just another authored
// constant spent through the same em.
//
// Pinned by tests/unit/test_gamepad_geometry.cpp.
// ============================================================================
#pragma once

namespace GamepadLayout {

// ---- The metrics this controller layout was authored against (cbbd1a2) ----
// Normalized screen units and ratios, at scale 1.
constexpr int kFrameChars = 43;        // frame width, in characters of the authored font
constexpr float kPadChars = 2.0f;      // [panel] padding-x at authoring, in cells (== chars)
constexpr float kCharRatio = 0.275f;   // font.char-width at authoring
constexpr float kLineRatio = 1.11f;    // row pitch / font size (0.0222 / 0.0200)
constexpr float kFontSize = 0.0200f;   // [Advanced] uiFontSize at authoring

// Frame width in ems of the authored font: text, plus padding at both ends.
constexpr float kFrameEm = (kFrameChars + 2.0f * kPadChars) * kCharRatio;

// The em the interior must be drawn at for the frame to come out `frameWidth` wide.
// Feed it the frame width the widget actually computed and it hands back the font
// size that frame corresponds to — at the shipped metrics, exactly uiFontSize.
inline float interiorEm(float frameWidth) {
    return frameWidth / kFrameEm;
}

// Authored normalized distance -> screen, at that em. This is what the per-pad
// offsets are multiplied by; it is 1 when the widget is drawn at authoring size.
inline float unitScale(float em) {
    return em / kFontSize;
}

// ---------------------------------------------------------------------------
// ONE PAD'S GEOMETRY -- the numbers that describe where the buttons sit on one
// specific controller photograph.
//
// This is per-pad DATA, not shared layout: an Xbox pad and a DualShock put their
// sticks, d-pad and menu buttons in materially different places, and essentially
// every field below differs between the two shipped packs. That is the whole
// reason a pad is a PACK (art + ini) rather than a texture variant -- the picture
// alone was never enough to place anything on it.
//
// Sizes are in the background artwork's own pixels and are read as RATIOS against
// `backgroundWidth`, so re-exporting the art at a different resolution needs no
// other change. Offsets are authored normalized units, multiplied by unitScale()
// so they track the frame however the type is sized.
//
// The defaults here are the LAST-RESORT fallback used when a pad has no ini at
// all; the shipped values live in gamepads/<name>/gamepad.ini. They deliberately
// match the shipped Xbox pack: the previous fallback was a hand-copied struct
// that had drifted from it (dpad 34x56 / face 53 against the real 32x53 / 47),
// so an unrecognised pad drew at subtly wrong sizes.
struct PadGeometry {
    // Reference background dimensions (the artwork's pixel size).
    float backgroundWidth = 750.0f;
    float backgroundHeight = 630.0f;

    // Sprite sizes, on the backgroundWidth reference.
    float triggerWidth = 89.0f, triggerHeight = 61.0f;
    float bumperWidth = 171.0f, bumperHeight = 63.0f;
    float dpadWidth = 32.0f, dpadHeight = 53.0f;
    float faceButtonSize = 47.0f;   // Square buttons
    float menuButtonWidth = 33.0f, menuButtonHeight = 33.0f;   // Can be non-square
    float stickSize = 83.0f;

    // Position offsets, applied after the base layout calculation.
    float leftTriggerX = 0.041f, leftTriggerY = 0.0143f;
    float rightTriggerX = -0.041f, rightTriggerY = 0.0143f;
    float leftBumperX = -0.01f, leftBumperY = 0.0573f;
    float rightBumperX = 0.01f, rightBumperY = 0.0573f;
    float leftStickX = 0.015f, leftStickY = 0.0563f;
    float rightStickX = -0.049f, rightStickY = 0.1263f;
    float dpadX = 0.0473f, dpadY = 0.0408f;
    float faceButtonsX = -0.0162f, faceButtonsY = -0.0343f;
    float menuButtonsX = 0.0004f, menuButtonsY = -0.0393f;

    // Spacing multipliers (1.0 = neutral).
    float dpadSpacing = 0.95f;
    float faceButtonSpacing = 1.07f;
    float menuButtonSpacing = 1.14f;
};

// The pad ini's numeric surface: one row per authored key, scoped "section.property"
// exactly as layoutForEachIniPairRaw hands them over.
//
// It lives HERE, beside the struct, rather than in AssetManager's discovery walk so
// that the mapping is reachable without a filesystem: tests/unit/test_asset_packs.cpp
// feeds the shipped inis through this same table and checks they still produce the
// geometry that used to be hardcoded in the widget. A copy of this table inside the
// discovery code could not be checked that way, which is exactly how a moved
// constant goes quietly wrong.
struct PadGeometryIniEntry {
    const char* key;
    float PadGeometry::* field;
    // MUST BE > 0. Every art.* and size.* is: five of them are DIVISORS in
    // gamepad_widget.cpp (art.width scales the whole pad; trigger-w, bumper-w, dpad-w
    // and menu-button-w each scale their own element), and the rest would draw a
    // degenerate or inverted quad. offset.* and spacing.* are free -- a negative offset
    // is a legitimate way to place a control, and the table says which is which rather
    // than the apply function knowing a list of names.
    bool positive;
};

inline constexpr PadGeometryIniEntry kPadGeometryIni[] = {
    { "art.width",               &PadGeometry::backgroundWidth,     true  },
    { "art.height",              &PadGeometry::backgroundHeight,    true  },
    { "size.trigger-w",          &PadGeometry::triggerWidth,        true  },
    { "size.trigger-h",          &PadGeometry::triggerHeight,       true  },
    { "size.bumper-w",           &PadGeometry::bumperWidth,         true  },
    { "size.bumper-h",           &PadGeometry::bumperHeight,        true  },
    { "size.dpad-w",             &PadGeometry::dpadWidth,           true  },
    { "size.dpad-h",             &PadGeometry::dpadHeight,          true  },
    { "size.face-button",        &PadGeometry::faceButtonSize,      true  },
    { "size.menu-button-w",      &PadGeometry::menuButtonWidth,     true  },
    { "size.menu-button-h",      &PadGeometry::menuButtonHeight,    true  },
    { "size.stick",              &PadGeometry::stickSize,           true  },
    { "offset.left-trigger-x",   &PadGeometry::leftTriggerX,        false },
    { "offset.left-trigger-y",   &PadGeometry::leftTriggerY,        false },
    { "offset.right-trigger-x",  &PadGeometry::rightTriggerX,       false },
    { "offset.right-trigger-y",  &PadGeometry::rightTriggerY,       false },
    { "offset.left-bumper-x",    &PadGeometry::leftBumperX,         false },
    { "offset.left-bumper-y",    &PadGeometry::leftBumperY,         false },
    { "offset.right-bumper-x",   &PadGeometry::rightBumperX,        false },
    { "offset.right-bumper-y",   &PadGeometry::rightBumperY,        false },
    { "offset.left-stick-x",     &PadGeometry::leftStickX,          false },
    { "offset.left-stick-y",     &PadGeometry::leftStickY,          false },
    { "offset.right-stick-x",    &PadGeometry::rightStickX,         false },
    { "offset.right-stick-y",    &PadGeometry::rightStickY,         false },
    { "offset.dpad-x",           &PadGeometry::dpadX,               false },
    { "offset.dpad-y",           &PadGeometry::dpadY,               false },
    { "offset.face-buttons-x",   &PadGeometry::faceButtonsX,        false },
    { "offset.face-buttons-y",   &PadGeometry::faceButtonsY,        false },
    { "offset.menu-buttons-x",   &PadGeometry::menuButtonsX,        false },
    { "offset.menu-buttons-y",   &PadGeometry::menuButtonsY,        false },
    { "spacing.dpad",            &PadGeometry::dpadSpacing,         false },
    { "spacing.face-button",     &PadGeometry::faceButtonSpacing,   false },
    { "spacing.menu-button",     &PadGeometry::menuButtonSpacing,   false },
};

// Apply one scoped key to `g`. Returns false when the key names nothing, so the
// caller can report the typo -- a silently ignored offset looks exactly like art
// that will not line up, which is the whole reason unknown keys are reported.
//
// A non-finite value is REJECTED rather than stored: the ini is hand-edited, and a
// NaN propagates into every derived position, so the pad vanishes instead of merely
// misdrawing. Same rule the settings loader applies to persisted floats.
//
// So is a non-positive art.*/size.* -- see PadGeometryIniEntry::positive. `art.width =
// 0` (a typo, or a value commented out so it parses as absent-but-present) divides the
// whole pad by zero: backgroundHeight becomes inf, fitPanelToGrid returns inf/NaN, and
// NaN vertices go to the game's DrawQuad for every quad the widget owns. These were
// compile-time constants before the geometry moved into packs, so the ini is a new
// trust boundary and this is the guard CLAUDE.md asks every parse site to carry.
//
// REJECTED, not clamped: the field keeps its shipped default, which is the same way an
// unknown pack name degrades. A clamp would invent a number the author never wrote.
inline bool applyPadGeometryIni(PadGeometry& g, const char* key, float value) {
    for (const PadGeometryIniEntry& e : kPadGeometryIni) {
        bool same = true;
        for (int i = 0; ; ++i) {
            if (e.key[i] != key[i]) { same = false; break; }
            if (e.key[i] == '\0') break;
        }
        if (!same) continue;
        // isfinite without <cmath>: NaN is the only value unequal to itself, and the
        // infinities are the only finite-comparison failures. Keeping this header
        // include-free is what lets AssetManager hold a PadGeometry without pulling
        // the HUD layer in behind it.
        if (value != value || value > 3.4e38f || value < -3.4e38f) return true;
        if (e.positive && !(value > 0.0f)) return true;
        g.*(e.field) = value;
        return true;
    }
    return false;
}

}  // namespace GamepadLayout
