// ============================================================================
// core/widget_constants.h
// Centralized dimension constants for widgets
// ============================================================================
#pragma once

namespace WidgetDimensions {
    // Widget dimensions (character widths for monospace text)
    constexpr int STANDARD_WIDTH = 12;       // Standard widget width (lap, position, duration, timing)
    constexpr int SPEED_WIDTH = 8;           // Speed widget width
    constexpr int SESSION_WIDTH = 46;        // Session widget width

    // Speed widget specific dimensions
    constexpr int SPEED_GEAR_OFFSET = 11;    // Gear offset in chars from left edge
}
