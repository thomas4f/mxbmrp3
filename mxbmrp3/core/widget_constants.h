// ============================================================================
// core/widget_constants.h
// Centralized dimension constants for widgets
// ============================================================================
#pragma once

namespace WidgetDimensions {
    // Widget dimensions (character widths for monospace text)
    constexpr int STANDARD_WIDTH = 12;       // Standard widget width (lap, position, duration, timing)
    constexpr int SPEED_WIDTH = 5;           // Speed widget width
    constexpr int GEAR_WIDTH = 5;            // Gear widget width
    constexpr int LEAN_WIDTH = 8;            // Lean widget width
    constexpr int SESSION_WIDTH = 43;        // Session widget width

    // TimingHud column widths (per-column character counts)
    constexpr int TIMING_LABEL_WIDTH = 7;    // "Split 1", "Lap 99", "Finish"
    constexpr int TIMING_TIME_WIDTH = 8;     // "1:23.456"
    constexpr int TIMING_GAP_WIDTH = 10;     // "+0:12.526", "INVALID"
    constexpr int TIMING_GAP_WITH_REF_WIDTH = 20;  // "+0:12.526 (0:34.649)"
    constexpr int TIMING_CHIP_WIDTH = 13;    // "AT:+0:12.526" compact chip format
    constexpr int TIMING_CHIP_WITH_REF_WIDTH = 21; // "AT:+0:12.526 (0:34.6)"
}
