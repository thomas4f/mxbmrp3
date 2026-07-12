// ============================================================================
// core/widget_constants.h
// Centralized dimension constants for widgets
// ============================================================================
#pragma once

namespace WidgetDimensions {
    // Widget dimensions (character widths for monospace text)
    constexpr int STANDARD_WIDTH = 12;       // Standard widget width (lap, position, duration, timing)
    constexpr int SPEED_WIDTH = 8;           // Speed widget width (matches Lean/GForce/Fuel)
    constexpr int GEAR_WIDTH = 8;            // Gear widget width (matches Lean/GForce/Fuel)
    constexpr int LEAN_WIDTH = 8;            // Lean widget width
    constexpr int GFORCE_WIDTH = 8;          // G-force widget width (matches Lean/Fuel)
    constexpr int COMPASS_WIDTH = 8;         // Compass widget width (matches Lean/GForce/Fuel)
    constexpr int SESSION_WIDTH = 43;        // Session widget width
    // Shared width (chars, measured at the LARGE font) of the centered top-of-screen stack
    // (Timing / Notices) so they line up. NoticesHud sizes "DEFAULT SETUP" at 14; Timing matches.
    constexpr int CENTER_STACK_WIDTH_CHARS = 14;
}
