// ============================================================================
// hud/radar_hud.h
// Radar HUD that displays a top-down view of the player and nearby riders
// ============================================================================
#pragma once

#include "base_hud.h"
#include "../vendor/piboso/mxb_api.h"
#include <vector>

class RadarHud : public BaseHud {
public:
    RadarHud();

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;
    void resetToDefaults();

    // Update rider positions (called frequently - must be fast)
    void updateRiderPositions(int numVehicles, const SPluginsRaceTrackPosition_t* positions);

    // Distance/range configuration (in meters)
    void setRadarRange(float rangeMeters);
    float getRadarRange() const { return m_fRadarRangeMeters; }

    // Colorize riders toggle - show riders in bike brand colors vs uniform gray
    void setColorizeRiders(bool colorize) {
        if (m_bColorizeRiders != colorize) {
            m_bColorizeRiders = colorize;
            setDataDirty();
        }
    }
    bool getColorizeRiders() const { return m_bColorizeRiders; }

    // Show player arrow toggle - display or hide the local player's arrow
    void setShowPlayerArrow(bool show) {
        if (m_bShowPlayerArrow != show) {
            m_bShowPlayerArrow = show;
            setDataDirty();
        }
    }
    bool getShowPlayerArrow() const { return m_bShowPlayerArrow; }

    // Fade when empty toggle - fade out radar when no riders are nearby
    void setFadeWhenEmpty(bool fade) {
        if (m_bFadeWhenEmpty != fade) {
            m_bFadeWhenEmpty = fade;
            setDataDirty();
        }
    }
    bool getFadeWhenEmpty() const { return m_bFadeWhenEmpty; }

    // Proximity alert distance - at what distance should warning triangles light up (in meters)
    void setAlertDistance(float meters);
    float getAlertDistance() const { return m_fAlertDistance; }

    // Rider label display mode
    enum class LabelMode {
        NONE = 0,       // No labels
        POSITION = 1,   // Show position (P1, P2, etc.)
        RACE_NUM = 2,   // Show race number
        BOTH = 3        // Show both (P1 #5)
    };

    void setLabelMode(LabelMode mode) {
        if (m_labelMode != mode) {
            m_labelMode = mode;
            setDataDirty();
        }
    }
    LabelMode getLabelMode() const { return m_labelMode; }

    // Public constants for settings UI
    static constexpr float DEFAULT_RADAR_RANGE = 50.0f;   // Default 50 meters
    static constexpr float MIN_RADAR_RANGE = 10.0f;       // Min 10 meters
    static constexpr float MAX_RADAR_RANGE = 200.0f;      // Max 200 meters
    static constexpr float RADAR_RANGE_STEP = 10.0f;      // Step increment for range

    // Proximity alert distance constants (in meters)
    static constexpr float DEFAULT_ALERT_DISTANCE = 50.0f;  // Default 50 meters
    static constexpr float MIN_ALERT_DISTANCE = 10.0f;      // Min 10 meters (matches RADAR_RANGE step)
    static constexpr float MAX_ALERT_DISTANCE = 100.0f;     // Max 100 meters
    static constexpr float ALERT_DISTANCE_STEP = 10.0f;     // 10 meter increments (matches RADAR_RANGE_STEP)

    // Allow SettingsHud and SettingsManager to access private members
    friend class SettingsHud;
    friend class SettingsManager;

protected:
    void rebuildRenderData() override;

private:
    // Rider position storage (updated frequently)
    std::vector<SPluginsRaceTrackPosition_t> m_riderPositions;

    // Radar configuration
    float m_fRadarRangeMeters;  // How far the radar can see (radius in meters)

    // Radar rendering configuration
    static constexpr float RADAR_SIZE = 0.20f;       // Radar diameter as fraction of screen height

    // Memory reservation sizes
    static constexpr size_t RESERVE_MAX_RIDERS = 40;
    static constexpr size_t RESERVE_QUADS = 50;      // Background + rider arrows
    static constexpr size_t RESERVE_STRINGS = 50;    // Title + rider labels

    // Rider colorization
    bool m_bColorizeRiders;

    // Show player's own arrow
    bool m_bShowPlayerArrow;

    // Fade radar when no riders nearby
    bool m_bFadeWhenEmpty;

    // Proximity alert distance (triangles only light up within this distance)
    float m_fAlertDistance;

    // Rider label display mode
    LabelMode m_labelMode;

    // Helper: Render a rider arrow at radar coordinates
    void renderRiderArrow(float radarX, float radarY, float yaw, unsigned long color,
                          float centerX, float centerY, float radarRadius);

    // Helper: Render rider label
    void renderRiderLabel(float radarX, float radarY, int raceNum, int position,
                          float centerX, float centerY, float radarRadius);
};
