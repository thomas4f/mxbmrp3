// ============================================================================
// hud/default_slots.h
// Where the default-off HUDs open: one spot per group, so a player clicking
// through them from the Toggles strip sees each one land where the last one was,
// never under a HUD that ships on.
//
// The HUDs that ship on set the edges (Standings, Map, Dash, Gear/Speed/RPM, the
// Toggles strip); the groups align to them, a two-cell gap apart like the spot
// under the Dash (center_stack.h, which places the Gap Bar, Notices and Timing):
//
//   RIGHT COLUMN  under the Map, each panel's RIGHT edge on the Map's, which is
//                 one cell clear of the Toggles strip: Friends, Telemetry, Delta
//                 Trace, Rumble, Event Log, Stream Chat, Lap Log, Ideal Lap,
//                 Now Playing, Speedo, Tacho -- the panels short enough to end above Gear and
//                 Speed -- and the taller Charts, Performance, Stats, FMX and
//                 Records, which reach into them (the column has 24 cells above
//                 Gear; accepted for 1.33, default_positions_test names them). One
//                 column, not two: previewed from the Toggles strip, HUDs that
//                 opened left, then right, then left were hard to follow.
//   CENTRE        centred under the spot under the Dash: Radar.
//   LOW LEFT      the bottom-left corner, on Standings' left edge: the Gamepad,
//                 which at a third of the screen wide would otherwise open
//                 behind the settings menu where it is switched on.
//   LOW CENTRE    centred under the widget strip, where subtitles go: the Spotter
//                 (ships on, draws only while a cue is up).
//
// Members of a group SHARE their spot: two switched on overlap until the player
// drags one, which is what a player does to any HUD anyway. Only the widget
// strip gives each member a slot of its own.
//
// DEFAULT POSITIONS, unthemed and title-off like center_stack.h: written once into
// each HUD's offset at reset, after which a drag owns them. The cell numbers are
// the shipped-on HUDs' edges, which have no closed form here (the Map's box, the
// Toggles strip's plan), so default_positions_test pins every one against the
// drawn panels: move a shipped-on HUD and the test names the line to change.
// Files from before settings v11 are moved here by migrateDefaultPositions()
// (settings_default_positions.cpp), but only where they still sat on the old spot.
// ============================================================================
#pragma once

#include "../core/layout_config.h"

namespace DefaultSlots {

// The Toggles strip's left edge (its right is on cell 180) less one cell of air.
// The Map's anchorX, and the right edge of every right-column panel.
constexpr float RIGHT_EDGE_CELLS = 175.71f;
// Two cells under the Map's TALLEST box. The Map sizes its box to the track's
// shape, up to a square (MapHud::MAP_HEIGHT plus padding: cell 46 at the bottom),
// so the column clears every track, not only a long thin one.
constexpr int RIGHT_TOP_CELL = 48;
// Two cells under Timing's bottom, the tallest box in the spot under the Dash
// (cell 20).
constexpr int CENTRE_TOP_CELL = 22;
// Two cells under the widget strip's bottom (Gear's row, cells 74 to 82).
constexpr int LOW_CENTRE_TOP_CELL = 84;
// Standings' left edge (one cell in), and the top that ends the default pad's
// 36-cell box on cell 90, the screen's last whole cell, one cell clear of
// Standings above it.
constexpr float LEFT_EDGE_CELLS = 1.0f;
constexpr int LOW_LEFT_TOP_CELL = 54;

// The standard panel widths a right-column member right-aligns by.
constexpr float WIDE_CELLS = 45.0f;
constexpr float NARROW_CELLS = 29.0f;

inline float rightEdge() { return RIGHT_EDGE_CELLS * layoutDefaults().cellW; }
// The left edge that puts a panel this many cells wide on the right edge.
inline float rightColumnX(float widthCells) {
    return (RIGHT_EDGE_CELLS - widthCells) * layoutDefaults().cellW;
}
inline float rightColumnY() { return static_cast<float>(RIGHT_TOP_CELL) * layoutDefaults().cellH; }
// The left edge that centres a panel this wide (screen units) on the screen.
inline float centreX(float width) { return 0.5f - width * 0.5f; }
inline float centreY() { return static_cast<float>(CENTRE_TOP_CELL) * layoutDefaults().cellH; }
inline float lowCentreY() { return static_cast<float>(LOW_CENTRE_TOP_CELL) * layoutDefaults().cellH; }
inline float lowLeftX() { return LEFT_EDGE_CELLS * layoutDefaults().cellW; }
inline float lowLeftY() { return static_cast<float>(LOW_LEFT_TOP_CELL) * layoutDefaults().cellH; }

}  // namespace DefaultSlots
