// ============================================================================
// core/widget_constants.h
// Centralized positioning constants for widgets and HUDs to eliminate duplication
// ============================================================================
#pragma once

namespace WidgetPositions {
    // Widget stack position (right side of screen, grid-aligned)
    // All minimal widgets align to this X position
    constexpr float WIDGET_STACK_X = 0.4675f;  // 85 char widths (0.0055 * 85)

    // Vertical spacing for widget stack (line heights)
    constexpr float WIDGET_SPACING = 0.0555f;  // 2.5 line heights

    // Widget Y positions (grid-aligned, in line heights)
    constexpr float LAP_Y = 0.0000f;           // 0 line heights (top of stack)
    constexpr float POSITION_Y = 0.0555f;      // 2.5 line heights
    constexpr float TIME_Y = 0.1110f;          // 5 line heights
    constexpr float SESSION_Y = 0.1110f;       // 5 line heights (same as time)
    constexpr float SPEED_Y = 0.1665f;         // 7.5 line heights
    constexpr float GEAR_Y = 0.2776f;          // 12.5 line heights
}

namespace WidgetDimensions {
    // Widget dimensions (character widths for monospace text)
    constexpr int STANDARD_WIDTH = 12;       // Standard widget width (lap, position, duration, timing)
    constexpr int SPEED_WIDTH = 8;           // Speed widget width
    constexpr int SESSION_WIDTH = 44;        // Session widget width (was 28, increased by 16 chars)
    constexpr int BARS_WIDTH = 12;           // Bars widget width (6 bars * 1 char + 5 spaces * 0.5 char + padding)

    // Speed widget specific dimensions
    constexpr int SPEED_GEAR_OFFSET = 11;    // Gear offset in chars from left edge
}

namespace CenterDisplayPositions {
    // Horizontal center for center-screen widgets
    constexpr float CENTER_X = 0.5f;

    // Divider line between notices and timing display
    // NoticesWidget: bottom edge at this line, grows UP
    // TimingWidget: top edge at this line, grows DOWN
    constexpr float TIMING_DIVIDER_Y = 0.1665f;

    // Small gap between divider and widget edges
    constexpr float DIVIDER_GAP = 0.005f;
}

namespace HudPositions {
    // Common HUD positions (grid-aligned)
    // Left side positions
    constexpr float LEFT_EDGE_X = 0.0110f;     // 2 char widths (session_best, lap_log)
    constexpr float LEFT_SIDE_X = 0.0495f;     // 9 char widths (input_visualizer, stick_input)

    // Center positions
    constexpr float CENTER_LEFT_X = 0.2420f;   // 44 char widths
    constexpr float CENTER_RIGHT_X = 0.5555f;  // 101 char widths (standings)

    // Right side position
    constexpr float RIGHT_SIDE_X = 0.7535f;    // 137 char widths (performance)

    // Common Y positions
    constexpr float TOP_Y = 0.1110f;           // 5 line heights (performance)
    constexpr float UPPER_Y = 0.1554f;         // 7 line heights (available)
    constexpr float MID_UPPER_Y = 0.3108f;     // 14 line heights
    constexpr float MID_LOWER_Y = 0.5106f;     // 23 line heights (standings)
    constexpr float LOWER_Y = 0.6660f;         // 30 line heights (session_best, stick_input)
    constexpr float BOTTOM_Y = 0.7770f;        // 35 line heights (lap_log)
}
