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

    // Override setScale to grow from center instead of top-left
    void setScale(float scale);

    // Update rider positions (called frequently - must be fast)
    void updateRiderPositions(int numVehicles, const SPluginsRaceTrackPosition_t* positions);

    // Distance/range configuration (in meters)
    void setRadarRange(float rangeMeters);
    float getRadarRange() const { return m_fRadarRangeMeters; }

    // Rider color mode - how to color other riders on the radar
    enum class RiderColorMode {
        UNIFORM = 0,        // Gray for all riders
        BRAND = 1,          // Bike brand colors
        RELATIVE_POS = 2    // Color based on position relative to player
    };

    void setRiderColorMode(RiderColorMode mode) {
        if (m_riderColorMode != mode) {
            m_riderColorMode = mode;
            setDataDirty();
        }
    }
    RiderColorMode getRiderColorMode() const { return m_riderColorMode; }

    // Show player arrow toggle - display or hide the local player's arrow
    void setShowPlayerArrow(bool show) {
        if (m_bShowPlayerArrow != show) {
            m_bShowPlayerArrow = show;
            setDataDirty();
        }
    }
    bool getShowPlayerArrow() const { return m_bShowPlayerArrow; }

    // Radar display mode - controls visibility behavior
    enum class RadarMode {
        OFF = 0,        // Radar hidden (proximity arrows still work)
        ON = 1,         // Radar always visible
        AUTO_HIDE = 2   // Radar fades when no riders nearby
    };

    void setRadarMode(RadarMode mode) {
        if (m_radarMode != mode) {
            m_radarMode = mode;
            setDataDirty();
        }
    }
    RadarMode getRadarMode() const { return m_radarMode; }

    // Proximity arrows toggle - show arrows at screen edges pointing to nearby riders
    enum class ProximityArrowMode {
        OFF = 0,     // No proximity arrows
        EDGE = 1,    // Arrows follow screen edges (rectangular path)
        CIRCLE = 2   // Arrows follow circular path around screen center
    };

    void setProximityArrowMode(ProximityArrowMode mode) {
        if (m_proximityArrowMode != mode) {
            m_proximityArrowMode = mode;
            setDataDirty();
        }
    }
    ProximityArrowMode getProximityArrowMode() const { return m_proximityArrowMode; }

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

    // Rider shape index (1-N=icons from AssetManager)
    void setRiderShape(int shapeIndex);
    int getRiderShape() const { return m_riderShapeIndex; }

    // Proximity arrow shape index (1-N=icons from AssetManager)
    void setProximityArrowShape(int shapeIndex);
    int getProximityArrowShape() const { return m_proximityArrowShapeIndex; }

    // Proximity arrow scale - independently scale proximity arrow icons
    void setProximityArrowScale(float scale);
    float getProximityArrowScale() const { return m_fProximityArrowScale; }

    // Proximity arrow color mode - how to color proximity arrows
    enum class ProximityArrowColorMode {
        DISTANCE = 0,   // Color based on distance (red=close, yellow=mid, green=far)
        POSITION = 1    // Color based on position relative to player (same as radar)
    };

    void setProximityArrowColorMode(ProximityArrowColorMode mode) {
        if (m_proximityArrowColorMode != mode) {
            m_proximityArrowColorMode = mode;
            setDataDirty();
        }
    }
    ProximityArrowColorMode getProximityArrowColorMode() const { return m_proximityArrowColorMode; }

    // Marker scale - independently scale rider icons and labels
    void setMarkerScale(float scale);
    float getMarkerScale() const { return m_fMarkerScale; }

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

    // Marker scale constants (independent icon/label scaling)
    static constexpr float DEFAULT_MARKER_SCALE = 1.0f;     // Default 100%
    static constexpr float MIN_MARKER_SCALE = 0.5f;         // Min 50%
    static constexpr float MAX_MARKER_SCALE = 3.0f;         // Max 300%

    // Proximity arrow scale constants
    static constexpr float DEFAULT_PROXIMITY_ARROW_SCALE = 1.0f;  // Default 100%
    static constexpr float MIN_PROXIMITY_ARROW_SCALE = 0.5f;      // Min 50%
    static constexpr float MAX_PROXIMITY_ARROW_SCALE = 3.0f;      // Max 300%

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
    static constexpr size_t RESERVE_QUADS = 60;            // Background + rider arrows
    static constexpr size_t RESERVE_STRINGS = 60;          // Title + rider labels

    // Rider colorization
    RiderColorMode m_riderColorMode;

    // Show player's own arrow
    bool m_bShowPlayerArrow;

    // Radar display mode (Off, On, Auto-hide)
    RadarMode m_radarMode;

    // Proximity arrow display mode
    ProximityArrowMode m_proximityArrowMode;

    // Proximity alert distance (triangles only light up within this distance)
    float m_fAlertDistance;

    // Rider label display mode
    LabelMode m_labelMode;

    // Rider shape index (1-N=icons from AssetManager)
    int m_riderShapeIndex;

    // Proximity arrow shape index (1-N=icons from AssetManager)
    int m_proximityArrowShapeIndex;

    // Proximity arrow scale (independent of HUD scale)
    float m_fProximityArrowScale;

    // Proximity arrow color mode
    ProximityArrowColorMode m_proximityArrowColorMode;

    // Marker scale (independent of HUD scale)
    float m_fMarkerScale;

    // Helper: Render a rider sprite at radar coordinates with rotation
    // shapeOverride: -1 = use m_riderShape, 1=Circle, 2=Triangle, 3=Wedge (TrackedRidersManager values)
    void renderRiderSprite(float radarX, float radarY, float yaw, unsigned long color,
                           float centerX, float centerY, float radarRadius,
                           int shapeOverride = -1);

    // Helper: Render rider label
    void renderRiderLabel(float radarX, float radarY, int raceNum, int position,
                          float centerX, float centerY, float radarRadius, float opacity);

    // Helper: Render proximity arrows at screen edges
    void renderProximityArrows(const SPluginsRaceTrackPosition_t* localPlayer,
                               float playerX, float playerZ,
                               float cosYaw, float sinYaw);
};
