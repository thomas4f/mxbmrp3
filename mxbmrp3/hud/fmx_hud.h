// ============================================================================
// hud/fmx_hud.h
// FMX (Freestyle Motocross) trick display HUD with rotation arcs and scoring
//
// Display settings (enabledRows, maxChainDisplayRows, debug logging) are
// per-profile — stored on the HUD instance, managed by SettingsManager.
// Detection/scoring state lives on FmxManager (singleton).
// ============================================================================
#pragma once

#include "base_hud.h"
#include "../core/fmx_types.h"

class FmxManager;

class FmxHud : public BaseHud {
public:
    FmxHud();

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;

    // Settings reset (all per-profile: position, visibility, scale, opacity, display elements)
    void resetToDefaults();

    // Row visibility constants (moved from FmxManager — these are display config, not detection)
    static constexpr uint32_t ROW_ARCS         = 1 << 0;
    static constexpr uint32_t ROW_DEBUG_VALUES = 1 << 1;
    static constexpr uint32_t ROW_COMBO_ARC    = 1 << 2;
    static constexpr uint32_t ROW_TRICK_STATS  = 1 << 3;
    static constexpr uint32_t ROW_DEFAULT = ROW_COMBO_ARC | ROW_TRICK_STATS;

    // Allow SettingsHud and SettingsManager to access private members
    friend class SettingsHud;
    friend class SettingsManager;

    // Max tricks shown in chain stack (0 = off, 1 = active only, 2+ = active + history)
    bool isTrickStackEnabled() const { return m_maxChainDisplayRows > 0; }

private:
    void rebuildRenderData() override;
    void rebuildLayout() override;

    // Arc rendering helpers
    void addArcSegment(float centerX, float centerY, float innerRadius, float outerRadius,
                       float startAngleRad, float endAngleRad, unsigned long color, int numSegments);
    void addRotationArc(float centerX, float centerY, float radius, float thickness,
                        float startAngle, float accumulatedAngle, float peakAngle,
                        unsigned long bgColor, unsigned long fillColor, unsigned long markerColor);

    // Calculate actual content height (accounts for variable-height sections)
    float getContentHeight() const;

    // Trick stack display entries (fixed buffer — no heap allocation per rebuild)
    struct TrickStackEntry {
        char name[64] = {};
        unsigned long color = 0;
    };
    std::vector<TrickStackEntry> m_trickStack;

    // Arc display snapshot (holds values during grace/chain/bounce to prevent jitter)
    struct ArcSnapshot {
        float startPitch = 0.0f, startYaw = 0.0f, startRoll = 0.0f;
        float accumulatedPitch = 0.0f, accumulatedYaw = 0.0f, accumulatedRoll = 0.0f;
        float peakPitch = 0.0f, peakYaw = 0.0f, peakRoll = 0.0f;
        float currentPitch = 0.0f, currentYaw = 0.0f, currentRoll = 0.0f;
        bool hasData = false;  // True when snapshot contains meaningful display data
    };
    ArcSnapshot m_arcSnapshot;

    // Trick stats snapshot (frozen during grace/chain like arcs)
    struct StatsSnapshot {
        float duration = 0.0f;
        float distance = 0.0f;
        float rotation = 0.0f;  // Peak rotation in degrees (max of |pitch|, |yaw|, |roll|)
        bool hasData = false;
    };
    StatsSnapshot m_statsSnapshot;

    // Combo arc animated fill (fills during grace period, retreats during chain)
    float m_comboArcFill = 0.0f;
    float m_comboArcGraceStartFill = -1.0f;  // Fill value when GRACE began (-1 = not in grace)
    float m_comboArcFailStartFill = -1.0f;   // Fill value when failure began (-1 = not in failure)

    // Per-profile display settings (managed by SettingsManager profile system)
    uint32_t m_enabledRows = ROW_DEFAULT;      // Bitmask of ROW_* constants
    int m_maxChainDisplayRows = 3;             // Default: active trick + 2 history (0 = off, max 10)
    bool m_showDebugLogging = false;           // Debug logging toggle

    // Visual constants — rotation arcs (pitch/yaw/roll)
    static constexpr int ARC_SEGMENTS = 32;
    static constexpr float ARC_RADIUS = 0.035f;
    static constexpr float ARC_THICKNESS = 0.006f;
    static constexpr float ARC_PEAK_MARKER_HALF_WIDTH = 0.05f;    // ~2.9 degrees angular half-width
    static constexpr float ARC_START_MARKER_HALF_WIDTH = 0.02f;   // ~1.1 degrees angular half-width
    static constexpr float ARC_MARKER_OVERSHOOT = 0.3f;           // Marker extends 30% beyond arc edge
    static constexpr float ARC_START_MARKER_OVERSHOOT = 0.2f;     // Start tick extends 20% beyond arc edge
    static constexpr float ARC_MAX_FILL_ROTATIONS = 2.0f;         // Clamp fill to prevent overdraw
    static constexpr int ARC_MIN_FILL_SEGMENTS = 3;               // Minimum segments for any fill arc

    // Visual constants — combo arc (chain countdown donut)
    static constexpr int COMBO_ARC_SEGMENTS = 30;  // Match LeanWidget
};
