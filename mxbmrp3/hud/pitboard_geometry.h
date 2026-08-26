// ============================================================================
// hud/pitboard_geometry.h
// One pit board's geometry: the artwork's proportions plus where each row of
// text sits on it. Pure data and arithmetic -- no rendering, no PluginData.
//
// THE BOARD IS A PICTURE, like the gamepad widget's controller (see
// gamepad_geometry.h, which this deliberately mirrors). The panel is sized from
// the type -- rows of text at the current line height -- and the ARTWORK's aspect
// is what turns that height into a width, so the drawn board keeps its
// proportions instead of being stretched to whatever the rows happened to need.
//
// That aspect used to be PitboardHud::TEXTURE_ASPECT_RATIO, a compiled
// 1920.0/1080.0 named after the one shipped .tga. It is per-board data, and
// having it compiled in was a real limit rather than an untidiness: a user who
// drew a board at any other aspect got it stretched, and NO amount of adjusting
// the offsets below could correct it -- they move text within the panel, they do
// not reshape the panel. Reading it from the pack is what makes a custom board
// actually portable.
//
// The offsets are FRACTIONS of the background's own width/height, so they are
// resolution- and scale-independent by construction: the same numbers describe
// the same spot on the board whatever size it is drawn at. Zero everywhere means
// "use the coded row positions", which is what the shipped board wants -- a pack
// only states an offset for a row its artwork puts somewhere else.
// ============================================================================
#pragma once

#include <cstdint>   // uint32_t textColor -- the unit build compiles this header standalone

namespace PitboardLayout {

struct BoardGeometry {
    // The artwork's pixel size. Only the RATIO is used (see the note above), but
    // both are stated because that is how a board author reads them off the file.
    float artWidth = 1920.0f;
    float artHeight = 1080.0f;

    // Per-row nudges, as a fraction of the background's width/height. +x right,
    // +y down. All zero = the coded positions, unchanged.
    float riderIdX = 0.0f, riderIdY = 0.0f;      // Row 1: rider id
    float sessionX = 0.0f, sessionY = 0.0f;      // Row 2: session name
    float positionX = 0.0f, positionY = 0.0f;    // Row 3 left: position (P1)
    float timeX = 0.0f, timeY = 0.0f;            // Row 3 centre: time
    float lapX = 0.0f, lapY = 0.0f;              // Row 3 right: lap (L2)
    float lastLapX = 0.0f, lastLapY = 0.0f;      // Row 4: last lap time
    float gapX = 0.0f, gapY = 0.0f;              // Row 5: gap comparison

    // The colour every row (and the caption) is written in, as the game's ABGR
    // word. A property of the ARTWORK, not the theme: the text must contrast
    // with this pack's own background, and the board should look the same
    // under every UI theme - a dark board states light text here and stays
    // readable wherever it travels. Default is the marker black the shipped
    // classic board was drawn for. Set from the ini's [text] color, which is
    // authored as #rrggbb like a theme's colours (parseRgbHex converts); it is
    // NOT in kBoardGeometryIni below because that table parses floats, and a
    // 32-bit colour word does not survive a float round-trip.
    uint32_t textColor = 0xFF000000;

    // Width from height, at this board's proportions. uiAspect is the global
    // pixel-aspect correction; the caller passes it so this stays free of the
    // layout headers. Guarded because both come from a hand-edited ini: a zero or
    // negative art height would otherwise produce a zero-width or inverted panel.
    float widthForHeight(float height, float uiAspect) const {
        if (!(artHeight > 0.0f) || !(artWidth > 0.0f) || !(uiAspect > 0.0f)) return height;
        return (height * (artWidth / artHeight)) / uiAspect;
    }
};

// The board ini's numeric surface: one row per authored key, scoped
// "section.property" exactly as layoutForEachIniPairRaw hands them over.
//
// Lives here beside the struct rather than in AssetManager's discovery walk for
// the same reason kPadGeometryIni does: a unit test can then drive the real
// mapping over the real shipped ini without a filesystem.
struct BoardGeometryIniEntry {
    const char* key;
    float BoardGeometry::* field;
};

inline constexpr BoardGeometryIniEntry kBoardGeometryIni[] = {
    { "art.width",         &BoardGeometry::artWidth },
    { "art.height",        &BoardGeometry::artHeight },
    { "offset.rider-id-x", &BoardGeometry::riderIdX },
    { "offset.rider-id-y", &BoardGeometry::riderIdY },
    { "offset.session-x",  &BoardGeometry::sessionX },
    { "offset.session-y",  &BoardGeometry::sessionY },
    { "offset.position-x", &BoardGeometry::positionX },
    { "offset.position-y", &BoardGeometry::positionY },
    { "offset.time-x",     &BoardGeometry::timeX },
    { "offset.time-y",     &BoardGeometry::timeY },
    { "offset.lap-x",      &BoardGeometry::lapX },
    { "offset.lap-y",      &BoardGeometry::lapY },
    { "offset.last-lap-x", &BoardGeometry::lastLapX },
    { "offset.last-lap-y", &BoardGeometry::lastLapY },
    { "offset.gap-x",      &BoardGeometry::gapX },
    { "offset.gap-y",      &BoardGeometry::gapY },
};

// Apply one scoped key to `g`. False when the key names nothing, so the caller
// can report the typo -- a silently ignored offset looks exactly like art that
// will not line up. A non-finite value is rejected rather than stored, for the
// same reason the settings loader guards persisted floats.
inline bool applyBoardGeometryIni(BoardGeometry& g, const char* key, float value) {
    for (const BoardGeometryIniEntry& e : kBoardGeometryIni) {
        bool same = true;
        for (int i = 0; ; ++i) {
            if (e.key[i] != key[i]) { same = false; break; }
            if (e.key[i] == '\0') break;
        }
        if (!same) continue;
        // isfinite without <cmath>: NaN is the only value unequal to itself, and
        // the infinities are the only finite-comparison failures. Keeping this
        // header include-free is what lets AssetManager hold a BoardGeometry
        // without pulling the HUD layer in behind it.
        if (value != value || value > 3.4e38f || value < -3.4e38f) return true;
        g.*(e.field) = value;
        return true;
    }
    return false;
}

}  // namespace PitboardLayout
