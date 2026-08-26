// ============================================================================
// hud/plate_geometry.h
// The standings race-number plate: how tall the box is and where it sits in its
// row. Pure arithmetic — no rendering, no PluginData.
//
// The plate is the only place in the UI where a box is drawn tightly around
// digits, so it is the only place a few pixels of vertical error are visible. It
// is centred in its row, and the number is centred in its row, so they agree by
// construction. That is the whole design, and it is worth saying because it used
// to be otherwise: this header also carried a numberCenteringOffsetY() that
// dropped the number from the row's top-aligned text cell onto the plate. Glyph
// centring is BaseHud's job now (rowCenterOffset(), applied by addString and
// positionString to every string in the UI), so the local nudge became a second
// helping and put the number on the plate's bottom edge. Deleted; do not
// reintroduce a plate-local vertical offset — if the number looks off, the row
// centring is what is wrong, and it is wrong everywhere.
//
// Pinned by tests/unit/test_plate_geometry.cpp (the box) and the centring case in
// tests/integration/tests/standings_layout_test.cpp (where the number lands).
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
// half the leftover. Centred is load-bearing, not cosmetic: it is what makes a
// row-centred glyph land plate-centred with no correction of its own.
// The brand MARK's height as a fraction of the FONT size — not the row. The
// mark is an icon like the status flags (STATUS_ICON_HALF_RATIO), so it
// tracks the glyphs when uiLineHeight changes instead of growing with the
// row. 0.657 = kPlateHeightFrac * 0.7 * 1.17335 (0.8 * 0.7 * 1.17335), the row
// pitch that was the default when the mark was drawn.
//
// It is a FRACTION OF THE FONT, so it did not move when the default pitch became
// 1.1 -- that is the whole point of the ratio being font-relative: the mark keeps
// its size against the glyphs beside it while the row around it gets tighter. The
// plate does shrink with the row (kPlateHeightFrac * lineHeight), so the mark now
// fills ~75% of the plate's height rather than ~70%. Still inside it, and the
// number stays as drawn rather than being re-derived to chase the default.
constexpr float kBrandMarkHeightRatio = 0.657f;

inline float platePadY(float lineHeight) {
    return (lineHeight - plateHeight(lineHeight)) * 0.5f;
}

}  // namespace PlateLayout
