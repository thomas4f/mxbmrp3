// ============================================================================
// hud/session_best_hud.h
// Displays session best split times with comparison to personal best
// ============================================================================
#pragma once

#include "base_hud.h"
#include "../core/plugin_constants.h"
#include "../core/widget_constants.h"

class SessionBestHud : public BaseHud {
public:
    SessionBestHud();
    virtual ~SessionBestHud() = default;

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;
    void resetToDefaults();

    // Row flags - each bit represents a row that can be toggled
    enum RowFlags : uint32_t {
        ROW_S1    = 1 << 0,  // Sector 1 time
        ROW_S2    = 1 << 1,  // Sector 2 time
        ROW_S3    = 1 << 2,  // Sector 3 time
        ROW_LAST  = 1 << 3,  // Last lap time
        ROW_BEST  = 1 << 4,  // Best lap
        ROW_IDEAL = 1 << 5,  // Ideal lap time

        ROW_REQUIRED = 0,    // No required rows
        ROW_DEFAULT  = 0x3F  // All 6 rows enabled (binary: 111111)
    };

    // Allow SettingsHud and SettingsManager to access private members
    friend class SettingsHud;
    friend class SettingsManager;

protected:
    // Override for optimized layout rebuild (just update positions)
    void rebuildLayout() override;

private:
    void rebuildRenderData() override;

    // Check if row is enabled
    bool isRowEnabled(RowFlags row) const {
        return (m_enabledRows & row) != 0;
    }

    // Count enabled rows (for height calculation)
    int getEnabledRowCount() const;

    // HUD positioning constants
    // Positioned in bottom-left area
    static constexpr float START_X = HudPositions::LEFT_EDGE_X;
    static constexpr float START_Y = HudPositions::LOWER_Y;
    static constexpr int BACKGROUND_WIDTH_CHARS = 26;  // Optimized for max content: "Ideal" + "99:59.999" + "+99:59.999"
    static constexpr int NUM_ROWS = 7;  // Title + S1 + S2 + S3 + Last + Best + Ideal

    // Column width constants (in character counts)
    // Width = max_length + 1 for spacing, except last column
    static constexpr int COL_LABEL_WIDTH = 6;   // Label column ("Ideal" = 5 chars + 1 gap)
    static constexpr int COL_TIME_WIDTH = 10;   // Time column (M:SS.mmm = 9 chars + 1 gap)
    static constexpr int COL_DIFF_WIDTH = 10;   // Diff column (+/-M:SS.mmm = 10 chars, last column, no gap)

    // Column positions - cached to avoid recalculation
    struct ColumnPositions {
        float label;   // Label column (left-aligned split names)
        float time;    // Time column
        float diff;    // Diff column (comparison to PB)

        ColumnPositions(float contentStartX, float scale = 1.0f);
    };

    ColumnPositions m_columns;
    uint32_t m_enabledRows = ROW_DEFAULT;  // Bitfield of enabled rows
};
