// ============================================================================
// hud/input_hud.h
// Displays analog stick input trails (left stick and right stick)
// ============================================================================
#pragma once

#include "base_hud.h"
#include "../core/plugin_constants.h"
#include "../core/widget_constants.h"

class InputHud : public BaseHud {
public:
    // PERFORMANCE TEST: Set to false to disable HUD and all calculations
    static constexpr bool ENABLED = true;

    InputHud();
    virtual ~InputHud() = default;

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;
    void resetToDefaults();

    // Element flags - each bit represents an element that can be toggled
    enum ElementFlags : uint32_t {
        ELEM_CROSSHAIRS = 1 << 0,  // Crosshair lines
        ELEM_TRAILS     = 1 << 1,  // Stick movement trails
        ELEM_VALUES     = 1 << 2,  // Numeric X/Y values table

        ELEM_REQUIRED = 0,    // No required elements
        ELEM_DEFAULT  = 0x7   // All 3 elements enabled (binary: 111)
    };

    // Allow SettingsHud and SettingsManager to access private members
    friend class SettingsHud;
    friend class SettingsManager;

private:
    void rebuildRenderData() override;

    // Helper to add analog stick trail visualization
    void addStickTrail(const char* label, const std::deque<HistoryBuffers::StickSample>& stickHistory,
                       float x, float y, float width, float height, unsigned long color, bool xinputConnected);

    // Base position (0,0) - actual position comes from m_fOffsetX/m_fOffsetY
    static constexpr float START_X = 0.0f;
    static constexpr float START_Y = 0.0f;
    static constexpr int BACKGROUND_WIDTH_CHARS = 43;  // Matches Performance/Telemetry graph HUD width

    // Stick trail dimensions - compact 6-line height
    // Spacing: 14 chars (adjusted for 43-char total width)
    // With 6-line height: width per stick ≈ 6 / 2.27 ≈ 13.5 chars (square in pixels)
    // Spacing adjusted to maintain overall 43-char width alignment: 43 - (2 * 13.5) ≈ 16 chars
    static constexpr float STICK_HEIGHT_LINES = 6.0f;  // Compact height (1 line shorter)
    static constexpr int STICK_SPACING_CHARS = 16;     // Spacing to maintain 43-char alignment

    // Pre-allocated buffers for trail rendering (avoid per-frame allocation)
    std::vector<float> m_screenX;
    std::vector<float> m_screenY;
    std::vector<float> m_perpX;
    std::vector<float> m_perpY;
    std::vector<float> m_age;
    std::vector<float> m_alpha;
    std::vector<float> m_scale;

    uint32_t m_enabledElements = ELEM_DEFAULT;  // Bitfield of enabled elements
};
