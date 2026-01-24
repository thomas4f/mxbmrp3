// ============================================================================
// hud/tyre_temp_widget.h
// Tyre Temperature Widget - displays tyre temperatures for front and rear wheels
// GP Bikes only - shows left/middle/right tread temperatures as colored blocks
// ============================================================================
#pragma once

#include "base_hud.h"
#include "../core/plugin_data.h"
#include "../core/plugin_constants.h"
#include "../core/widget_constants.h"
#include "../game/game_config.h"

#if GAME_HAS_TYRE_TEMP

class TyreTempWidget : public BaseHud {
public:
    // Row visibility flags (bitfield) - configurable via INI
    enum RowFlags : uint32_t {
        ROW_BARS    = 1 << 0,  // Temperature color bars
        ROW_VALUES  = 1 << 1,  // Numeric temperature values
        ROW_DEFAULT = ROW_BARS | ROW_VALUES
    };

    TyreTempWidget();
    virtual ~TyreTempWidget() = default;

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;
    void resetToDefaults();

    // Row visibility (configurable via INI)
    uint32_t m_enabledRows = ROW_DEFAULT;

    // Temperature thresholds (configurable via INI)
    // Temperatures below coldThreshold are blue (no grip)
    // Temperatures above hotThreshold are red (overheating)
    // Temperatures in between use gradient (blue -> green -> yellow -> red)
    float getColdThreshold() const { return m_coldThreshold; }
    float getHotThreshold() const { return m_hotThreshold; }
    void setColdThreshold(float temp) { m_coldThreshold = temp; setDataDirty(); }
    void setHotThreshold(float temp) { m_hotThreshold = temp; setDataDirty(); }

    // Default thresholds (Celsius)
    static constexpr float DEFAULT_COLD_THRESHOLD = 80.0f;   // Below this = too cold
    static constexpr float DEFAULT_HOT_THRESHOLD = 130.0f;   // Above this = too hot

    // Allow SettingsHud and SettingsManager to access private members
    friend class SettingsHud;
    friend class SettingsManager;

protected:
    void rebuildLayout() override;

private:
    void rebuildRenderData() override;

    // Calculate tyre temperature color based on value relative to thresholds
    // Returns gradient color: blue (cold) -> green (warm) -> yellow -> red (hot)
    unsigned long calculateTyreTemperatureColor(float temp) const;

    // Layout constants
    static constexpr int NUM_WHEELS = 2;       // Front (0) and Rear (1)
    static constexpr int NUM_SECTIONS = 3;     // Left (0), Middle (1), Right (2)
    static constexpr float LABEL_HEIGHT_LINES = 1.0f; // Height for L/M/R labels

    // Temperature thresholds (user-configurable via INI)
    float m_coldThreshold = DEFAULT_COLD_THRESHOLD;
    float m_hotThreshold = DEFAULT_HOT_THRESHOLD;
};

#endif // GAME_HAS_TYRE_TEMP
