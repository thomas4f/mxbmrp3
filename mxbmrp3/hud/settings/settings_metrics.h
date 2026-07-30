// ============================================================================
// hud/settings/settings_metrics.h
// The settings panel's layout measurements, in CHARACTERS.
//
// WHY CHARACTERS AND NOT PIXELS. The panel is laid out in a monospace font, so
// every horizontal measurement is a whole number of character cells that only
// becomes a float at the point of emission. Keeping the character arithmetic
// here — rather than deriving it back out of the emitted floats — means the
// derived widths are exact.
//
// THAT IS NOT A STYLE POINT. The tooltip box used to recover its width by
// dividing a float span by the width of one character. Real arithmetic makes
// that exactly `tooltipCharsPerLine()`; IEEE floats made it that at most HUD
// scales and ONE LESS at 0.70, so the tooltip box quietly lost a character for
// anyone running the menu small — a truncated tooltip at one scale and not
// another, with nothing in the code saying so. The integer form cannot do that.
//
// These are also what tests/unit/test_tooltip_length.cpp measures shipped
// tooltips against, so the guard tracks the real box instead of a magic number:
// change the panel width here and the guard follows on the next run.
// ============================================================================
#pragma once

namespace SettingsMetrics {

// Total panel width (sized to fit the Rumble effects table).
constexpr int PANEL_WIDTH = 71;
// Vertical tab column on the left (fits "[X] Ideal Lap").
constexpr int TAB_WIDTH = 16;
// Gap between the tab column and the content area.
constexpr int TAB_CONTENT_GAP = 2;
// Left/right control column offsets within the content area.
constexpr int LEFT_COLUMN = 2;
constexpr int RIGHT_COLUMN = 28;

// Content area width: what is left of the panel once the tab column and its gap
// are taken out.
constexpr int contentAreaChars() {
    return PANEL_WIDTH - TAB_WIDTH - TAB_CONTENT_GAP;
}

// Characters per line in the tooltip/description box, which starts at the label
// column and runs to the content edge.
constexpr int tooltipCharsPerLine() {
    return contentAreaChars() - LEFT_COLUMN;
}

}  // namespace SettingsMetrics
