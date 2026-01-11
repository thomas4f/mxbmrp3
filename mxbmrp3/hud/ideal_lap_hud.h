// ============================================================================
// hud/ideal_lap_hud.h
// Displays ideal lap (best individual sectors) with comparison to current
// ============================================================================
#pragma once

#include "base_hud.h"
#include "../core/plugin_constants.h"
#include "../core/widget_constants.h"
#include <chrono>

class IdealLapHud : public BaseHud {
public:
    IdealLapHud();
    virtual ~IdealLapHud() = default;

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;
    void resetToDefaults();

    // Row flags - groups of rows that can be toggled together
    enum RowFlags : uint32_t {
        ROW_SECTORS = 1 << 0,  // Sector times (S1, S2, S3)
        ROW_LAPS    = 1 << 1,  // Lap times (Last, Best, Ideal)

        ROW_DEFAULT = ROW_SECTORS | ROW_LAPS  // All rows enabled
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

    // Base position (0,0) - actual position comes from m_fOffsetX/m_fOffsetY
    static constexpr float START_X = 0.0f;
    static constexpr float START_Y = 0.0f;
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
        float diff;    // Diff column (comparison to ideal)

        ColumnPositions(float contentStartX, float scale = 1.0f);
    };

    // Override: never needs frequent updates (only updates on split/lap events)
    bool needsFrequentUpdates() const override;

    ColumnPositions m_columns;
    uint32_t m_enabledRows = ROW_DEFAULT;  // Bitfield of enabled rows
};
