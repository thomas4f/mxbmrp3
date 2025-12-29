// ============================================================================
// hud/map_hud.h
// Map HUD that displays track layout and rider positions
// ============================================================================
#pragma once

#include "base_hud.h"
#include "../vendor/piboso/mxb_api.h"
#include <vector>

class MapHud : public BaseHud {
public:
    MapHud();

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;
    void resetToDefaults();

    // Override mouse input to update anchor when dragging ends
    bool handleMouseInput(bool allowInput = true) override;

    // Update track centerline data
    void updateTrackData(int numSegments, const SPluginsTrackSegment_t* segments);

    // Update rider positions (called frequently - must be fast)
    void updateRiderPositions(int numVehicles, const SPluginsRaceTrackPosition_t* positions);

    // Rotation mode - rotate map so local player always points up
    void setRotateToPlayer(bool rotate) {
        if (m_bRotateToPlayer != rotate) {
            m_bRotateToPlayer = rotate;
            setDataDirty();
        }
    }
    bool getRotateToPlayer() const { return m_bRotateToPlayer; }

    // Track outline toggle - show white outline around black track
    void setShowOutline(bool show) {
        if (m_bShowOutline != show) {
            m_bShowOutline = show;
            setDataDirty();
        }
    }
    bool getShowOutline() const { return m_bShowOutline; }

    // Rider color mode - how to color other riders on the map
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

    // Track line width scale (percentage multiplier, 0.5-2.0)
    void setTrackWidthScale(float scale);
    float getTrackWidthScale() const { return m_fTrackWidthScale; }

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

    // Rider shape index (0=OFF, 1-N=icons from AssetManager)
    void setRiderShape(int shapeIndex);
    int getRiderShape() const { return m_riderShapeIndex; }

    // Anchor point for positioning (determines how map grows when dimensions change)
    enum class AnchorPoint {
        TOP_LEFT = 0,
        TOP_RIGHT = 1,
        BOTTOM_LEFT = 2,
        BOTTOM_RIGHT = 3
    };

    void setAnchorPoint(AnchorPoint anchor) { m_anchorPoint = anchor; }
    AnchorPoint getAnchorPoint() const { return m_anchorPoint; }

    // Update position based on anchor point (call after dimension changes)
    void updatePositionFromAnchor();

    // Public constants for settings UI
    static constexpr float DEFAULT_TRACK_WIDTH_SCALE = 1.0f;  // Default 100%
    static constexpr float MIN_TRACK_WIDTH_SCALE = 0.5f;      // Min 50%
    static constexpr float MAX_TRACK_WIDTH_SCALE = 3.0f;      // Max 300%

    // Zoom mode constants (Range setting)
    static constexpr float DEFAULT_ZOOM_DISTANCE = 100.0f;   // Default when zoom first enabled
    static constexpr float MIN_ZOOM_DISTANCE = 50.0f;        // Min 50 meters
    static constexpr float MAX_ZOOM_DISTANCE = 500.0f;       // Max 500 meters

    // Marker scale constants (independent icon/label scaling)
    static constexpr float DEFAULT_MARKER_SCALE = 1.0f;      // Default 100%
    static constexpr float MIN_MARKER_SCALE = 0.5f;          // Min 50%
    static constexpr float MAX_MARKER_SCALE = 3.0f;          // Max 300%

    // Pixel spacing constants (track rendering density)
    static constexpr float DEFAULT_PIXEL_SPACING = 2.0f;     // Default 2 meters between quads
    static constexpr float MIN_PIXEL_SPACING = 0.5f;         // Min 0.5m (very dense, high GPU)
    static constexpr float MAX_PIXEL_SPACING = 8.0f;         // Max 8m (sparse, low GPU)

    // Zoom mode - follow player showing limited track distance
    void setZoomEnabled(bool enabled) {
        if (m_bZoomEnabled != enabled) {
            m_bZoomEnabled = enabled;
            setDataDirty();
        }
    }
    bool getZoomEnabled() const { return m_bZoomEnabled; }

    void setZoomDistance(float meters);
    float getZoomDistance() const { return m_fZoomDistance; }

    // Marker scale - independently scale rider icons and labels
    void setMarkerScale(float scale);
    float getMarkerScale() const { return m_fMarkerScale; }

    // Pixel spacing - track rendering density (lower = more quads, higher GPU usage)
    void setPixelSpacing(float spacing);
    float getPixelSpacing() const { return m_fPixelSpacing; }

    // Allow SettingsHud and SettingsManager to access private members
    friend class SettingsHud;
    friend class SettingsManager;

protected:
    void rebuildRenderData() override;
    // Note: rebuildLayout() uses base class default (full rebuild)
    // This is appropriate for HUDs with many dynamically-generated quads

private:
    // Click region for rider selection (spectator switching)
    struct RiderClickRegion {
        float x, y, width, height;
        int raceNum;
    };

    // Track segment storage
    std::vector<SPluginsTrackSegment_t> m_trackSegments;

    // Rider position storage (updated frequently)
    std::vector<SPluginsRaceTrackPosition_t> m_riderPositions;

    // Click regions for rider selection (populated during renderRiders)
    std::vector<RiderClickRegion> m_riderClickRegions;

    // Map rendering configuration
    static constexpr float MAP_HEIGHT = 0.33f;  // Map height as fraction of screen (0.33 = 33%)
    static constexpr float MAP_PADDING = 0.01f;  // Padding from screen edge
    static constexpr float PIXEL_SPACING = 2.0f;  // Distance between pixels in world meters (smaller = denser)

    // Configurable track line width scale (percentage multiplier)
    float m_fTrackWidthScale;

    // Memory reservation sizes (optimize initial allocation)
    static constexpr size_t RESERVE_TRACK_SEGMENTS = 200;  // Typical track has 100-200 segments
    static constexpr size_t RESERVE_QUADS = 1000;          // 1 background + ~400-1000 track pixels (2x density)
    static constexpr size_t RESERVE_STRINGS = 60;          // Title + optional message + rider labels

    // Map bounds (calculated from track data)
    float m_minX, m_maxX, m_minY, m_maxY;
    float m_fTrackScale;     // Scale factor to fit track in map area
    float m_fBaseMapWidth;   // Base (unscaled) map width
    float m_fBaseMapHeight;  // Base (unscaled) map height
    bool m_bHasTrackData;

    // Rotation mode
    bool m_bRotateToPlayer;  // Rotate map so local player always points up
    float m_fLastRotationAngle;  // Last rotation angle before crash (keeps map orientation stable)
    float m_fLastPlayerX;  // Last player X position before crash (keeps screen position stable)
    float m_fLastPlayerZ;  // Last player Z position before crash (keeps screen position stable)

    // Track outline
    bool m_bShowOutline;  // Show white outline around black track for visual clarity

    // Rider colorization
    RiderColorMode m_riderColorMode;  // How to color other riders on the map

    // Rider label display mode
    LabelMode m_labelMode;

    // Rider shape index (0=OFF, 1-N=icons from AssetManager)
    int m_riderShapeIndex;

    // Anchor point for positioning
    AnchorPoint m_anchorPoint;
    float m_fAnchorX;  // Desired anchor position in screen space
    float m_fAnchorY;

    // Zoom mode configuration
    bool m_bZoomEnabled;         // Follow player with limited view distance
    float m_fZoomDistance;       // Total view distance in meters (Range setting)

    // Marker scale (independent of HUD scale)
    float m_fMarkerScale;        // Scale factor for rider icons and labels

    // Pixel spacing (track rendering density)
    float m_fPixelSpacing;       // Distance in meters between track quads

    // Calculate track bounds from segments
    void calculateTrackBounds();

    // Calculate zoom bounds centered on player position
    // Returns true if player found, false otherwise (falls back to full track)
    bool calculateZoomBounds(float& zoomMinX, float& zoomMaxX, float& zoomMinY, float& zoomMaxY) const;

    // Calculate which corner to anchor to based on current position
    AnchorPoint calculateAnchorFromPosition() const;

    // Update anchor position from current HUD position
    void updateAnchorFromCurrentPosition();

    // Helper: Calculate track screen bounds at given rotation angle
    void calculateTrackScreenBounds(float rotationAngle, float& minX, float& maxX, float& minY, float& maxY) const;

    // Calculate rotation angle for map rotation mode (caches player position when active)
    float calculateRotationAngle();

    // Convert world coordinates to map screen coordinates
    // If rotation mode is enabled, also applies rotation around local player
    void worldToScreen(float worldX, float worldY, float& screenX, float& screenY, float rotationAngle = 0.0f) const;

    // Render the track as quads (takes pre-calculated rotation angle, color, and width multiplier)
    void renderTrack(float rotationAngle, unsigned long trackColor, float widthMultiplier,
                     float clipLeft, float clipTop, float clipRight, float clipBottom);

    // Render start marker (takes pre-calculated rotation angle and clip bounds)
    void renderStartMarker(float rotationAngle,
                          float clipLeft, float clipTop, float clipRight, float clipBottom);

    // Render rider positions as strings (takes pre-calculated rotation angle and clip bounds)
    void renderRiders(float rotationAngle,
                     float clipLeft, float clipTop, float clipRight, float clipBottom);

    // Handle click on rider marker to switch spectator target
    void handleClick(float mouseX, float mouseY);
};
