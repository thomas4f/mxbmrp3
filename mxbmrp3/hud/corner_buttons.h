// ============================================================================
// hud/corner_buttons.h
// Where the two CORNER BUTTONS sit: the settings gear and the director camera.
//
// WHY THEY SHARE A HEADER. They are one row of two identical boxes, and their
// defaults are related -- the camera sits one cell left of the gear. Written out
// separately, that relationship is a comment, and a comment does not survive the
// box changing size. It did not: both were authored against a 6-cell box, then
// fitPanelToGrid rounded the box onto the lattice at 8 cells and neither default
// moved. The gear's right edge went off the screen by 1.9px, and the camera
// overlapped it by a full cell.
//
// THE OVERLAP WAS NOT COSMETIC. isClicked() is a const edge test (isPressed &&
// !wasPressed) that consumes nothing, and HudManager polls both buttons
// unconditionally on the same frame, so a cursor in the shared strip satisfied
// both hit-tests: one click toggled the auto-director AND opened the settings
// panel. That is why the fix is a derivation rather than two corrected numbers --
// DIRECTOR_X is computed FROM the gear's position and the box width, so no future
// resize can put them on top of each other again.
//
// BOX_CELLS_W is the one value that has to be kept true by hand, because the box
// is sized by BaseHud::fitPanelToGrid() from the icon and the panel padding
// rather than stated here. tests/unit/corner_button_test.cpp recomputes that
// expression from LayoutMetrics and fails if the two disagree, which is the check
// that would have caught the original drift.
// ============================================================================
#pragma once

namespace CornerButtons {

// The shared box, in grid cells. Pinned to fitPanelToGrid's real output by
// tests/unit/corner_button_test.cpp -- change one and that test names the other.
inline constexpr int BOX_CELLS_W = 8;
inline constexpr int BOX_CELLS_H = 6;

// One cell of air between the two, matching the gap any two tiled widgets keep.
inline constexpr int GAP_CELLS = 1;

// The gear's left edge. Chosen so its RIGHT edge lands on 180 cells (0.990),
// just inside the corner -- the position it has always drawn at. The screen is
// 181.818 cells wide, so this is the largest whole-cell origin that keeps the
// whole box on screen; the unit test pins that too.
inline constexpr int SETTINGS_X = 180 - BOX_CELLS_W;

// The camera, one gap to its left. DERIVED, never restated.
inline constexpr int DIRECTOR_X = SETTINGS_X - BOX_CELLS_W - GAP_CELLS;

// Both sit one cell down from the top.
inline constexpr int BUTTON_Y = 1;

static_assert(DIRECTOR_X + BOX_CELLS_W + GAP_CELLS == SETTINGS_X,
              "the camera button must end exactly GAP_CELLS left of the gear");
static_assert(DIRECTOR_X > 0, "the camera button must start on screen");

}  // namespace CornerButtons
