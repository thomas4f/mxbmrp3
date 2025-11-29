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

    // Colorize riders toggle - show riders in bike brand colors vs uniform gray
    void setColorizeRiders(bool colorize) {
        if (m_bColorizeRiders != colorize) {
            m_bColorizeRiders = colorize;
            setDataDirty();
        }
    }
    bool getColorizeRiders() const { return m_bColorizeRiders; }

    // Track line width configuration (in world meters)
    void setTrackLineWidthMeters(float widthMeters);
    float getTrackLineWidthMeters() const { return m_fTrackLineWidthMeters; }

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
    static constexpr float DEFAULT_TRACK_LINE_WIDTH = 10.0f;  // Default 10 meters
    static constexpr float MIN_TRACK_LINE_WIDTH = 5.0f;       // Min 5 meters
    static constexpr float MAX_TRACK_LINE_WIDTH = 15.0f;     // Max 15 meters

    // Allow SettingsHud and SettingsManager to access private members
    friend class SettingsHud;
    friend class SettingsManager;

protected:
    void rebuildRenderData() override;
    // Note: rebuildLayout() uses base class default (full rebuild)
    // This is appropriate for HUDs with many dynamically-generated quads

private:
    // Track segment storage
    std::vector<SPluginsTrackSegment_t> m_trackSegments;

    // Rider position storage (updated frequently)
    std::vector<SPluginsRaceTrackPosition_t> m_riderPositions;

    // Map rendering configuration
    static constexpr float MAP_HEIGHT = 0.33f;  // Map height as fraction of screen (0.33 = 33%)
    static constexpr float MAP_PADDING = 0.01f;  // Padding from screen edge
    static constexpr float PIXEL_SPACING = 2.0f;  // Distance between pixels in world meters (smaller = denser)

    // Configurable track line width (in world meters)
    float m_fTrackLineWidthMeters;

    // Memory reservation sizes (optimize initial allocation)
    static constexpr size_t RESERVE_TRACK_SEGMENTS = 200;  // Typical track has 100-200 segments
    static constexpr size_t RESERVE_MAX_RIDERS = 40;       // Max riders in a race
    static constexpr size_t RESERVE_QUADS = 1000;          // 1 background + ~400-1000 track pixels (2x density)
    static constexpr size_t RESERVE_STRINGS = 50;          // Title + optional message + rider numbers (max ~40 riders)

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
    bool m_bColorizeRiders;  // Show riders in bike brand colors (false = uniform gray)

    // Rider label display mode
    LabelMode m_labelMode;

    // Anchor point for positioning
    AnchorPoint m_anchorPoint;
    float m_fAnchorX;  // Desired anchor position in screen space
    float m_fAnchorY;

    // Calculate track bounds from segments
    void calculateTrackBounds();

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
    void renderTrack(float rotationAngle, unsigned long trackColor, float widthMultiplier);

    // Render start marker (takes pre-calculated rotation angle)
    void renderStartMarker(float rotationAngle);

    // Render rider positions as strings (takes pre-calculated rotation angle)
    void renderRiders(float rotationAngle);
};
