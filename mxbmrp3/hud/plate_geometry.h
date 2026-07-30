// ============================================================================
// hud/plate_geometry.h
// The standings race-number plate: box size, and the nudge that centres the
// number inside it. Pure arithmetic — no rendering, no PluginData.
//
// WHY THE NUDGE EXISTS. A row's text is drawn with its glyph CELL top-aligned to
// the row origin, so the cell's centre sits (lineHeight - fontSize)/2 ABOVE the
// row's centre. For ordinary text that reads correctly: a descender ('g', 'y')
// fills the space below, so the ink looks balanced in a row highlight. Digits
// have no descender — the generator centres the cap/digit band inside the cell
// (mxbmrp3_fontgen `normalize`), so a race number occupies only that band and
// ends up sitting exactly that offset high.
//
// Invisible against a plain row, obvious inside the plate, which is a tight box
// (0.8 * lineHeight) centred on the row. Measured before this existed: the ink
// sat ~9% of plate height high — 1px of grey above the digits and 5px below, on
// a 19px plate — and identically for all six shipped fonts, which is what ruled
// the fonts out as the cause. See tests/unit/test_plate_geometry.cpp.
//
// THIS IS DELIBERATELY NOT A GLOBAL TEXT CHANGE. Shifting every row's text down
// would move all the text that currently looks right, to fix the one case that
// does not. The plate is the only place a box is drawn tightly around digits.
// ============================================================================
#pragma once

// Namespace name deliberately NOT PlateGeometry: StandingsHud already has a nested
// struct by that name, and an unqualified reference inside the class resolves to the
// struct, not this. Same two-things-one-name trap that cost this branch two wrong
// claims before RiderTrackState was renamed.
namespace PlateLayout {

// Plate box height as a fraction of the row: a little inset top and bottom.
constexpr float kPlateHeightFrac = 0.8f;

inline float plateHeight(float lineHeight) {
    return lineHeight * kPlateHeightFrac;
}

// Vertical inset of the plate inside its row — the plate is centred, so this is
// half the leftover.
inline float platePadY(float lineHeight) {
    return (lineHeight - plateHeight(lineHeight)) * 0.5f;
}

// How far DOWN to nudge the race number so its cap/digit band centres on the
// plate rather than on the top-aligned text cell. Zero when the row is exactly as
// tall as the font, which is the degenerate case where the two already coincide.
inline float numberCenteringOffsetY(float lineHeight, float fontSize) {
    const float slack = lineHeight - fontSize;
    return slack > 0.0f ? slack * 0.5f : 0.0f;
}

}  // namespace PlateLayout
