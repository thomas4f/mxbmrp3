// ============================================================================
// hud/helmet_overlay_hud.h
// First-person helmet overlay for immersion.
//
// Renders up to three full-screen quads. The tint layer position depends on
// the visor mode setting:
//   Visor mode:   tint → helmet lower → helmet upper  (tint behind, bleeds through visor cutout)
//   Goggles mode: helmet lower → helmet upper → tint  (tint in front, like a tinted lens)
//   Off:          helmet lower → helmet upper          (no tint)
//
// The tint quad is always full-screen (0,0 to 1,1). In Visor mode the opaque
// helmet layers cover it everywhere except the transparent visor cutout. In
// Goggles mode it overlays everything like a tinted goggle lens.
//
// Both helmet parts are translated vertically by a telemetry-driven
// "vibration" signal and rotated together around a bottom-center pivot to
// simulate natural head tilt. The source telemetry differs by vehicle type:
//
//   Bikes (GAME_HAS_SUSPENSION): tilt = lean angle (roll),
//                                vibration = suspension-length delta.
//   Karts (no suspension):       tilt = lateral G (rider braces head into
//                                       the corner against centripetal load),
//                                vibration = vertical G delta (chassis bumps,
//                                       kerb strikes) PLUS a slow low-pass on
//                                       longitudinal G so the helmet dips
//                                       under sustained braking and rises
//                                       under throttle (compensating for the
//                                       lack of a fork-dive signal).
//
// The two tilt mappings produce opposite on-screen rotations in the same
// turn direction, which is the physically correct behaviour: the bike rider's
// head counter-tilts against the bike's roll to stay closer to vertical (head
// stabilisation against lean), while the kart driver's head braces INTO the
// corner against centripetal load. Vibration always pushes UP on bumps.
//
// Texture authoring contract: helmet TGAs should be authored at screen size
// with ~10% bleed on all sides recommended. The overlay translates and
// rotates the quads within a small envelope (max ~15 deg tilt, ~8%
// vibration). Bleed covers gaps from translation and rotation corners.
//
// Hidden automatically during spectate/replay and when the player is crashed
// (the game forces an external camera view during crashes).
// ============================================================================
#pragma once

#include <cmath>

#include "base_hud.h"

// Strength is "active" when the rounded display percentage is non-zero.
// Mirrors the UI's "Off" condition (round(strength*100) == 0) so settings
// display always matches runtime behaviour - a strength labelled "Off" must
// not produce any visible tilt or vibration.
inline bool helmetStrengthActive(float strength) {
    return static_cast<int>(std::round(strength * 100.0f)) != 0;
}

class HelmetOverlayHud : public BaseHud {
public:
    HelmetOverlayHud();
    virtual ~HelmetOverlayHud() = default;

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;
    void resetToDefaults();

    // Rotation/translation tuning (static so the settings tab can show units/limits)
    static constexpr float MAX_TILT_DEG = 15.0f;          // Absolute tilt cap
    static constexpr float MAX_VIBRATION_Y = 0.08f;       // Max Y offset from vibration (normalized)
    static constexpr float MAX_HELMET_OFFSET_Y = 0.50f;   // Max user-configurable Y offset for helmet parts
    static constexpr float MAX_OVERLAY_ZOOM = 0.50f;       // Max zoom for helmet (-MAX = out, 0 = 100%, +MAX = in)

    // Texture authoring safe area: oversized by this much on every side
    static constexpr float TEXTURE_BLEED = 0.15f;

    // Vibration signal tuning (see rebuildRenderData for usage)
    static constexpr float VIB_DECAY_RATE = 12.0f;        // Exponential decay rate (per second)
    static constexpr float VIB_SCALE_MAX = 150.0f;        // Max input gain (at 100% sensitivity)

    // Longitudinal-G shift (karts only). Low-pass time constant is slower than
    // the bump channel so the offset persists while the brake is held instead
    // of decaying out like a jolt. Scale chosen so ~1.5g braking at full
    // vibration strength saturates at MAX_VIBRATION_Y.
    static constexpr float LONG_G_DECAY_RATE = 3.0f;
    static constexpr float LONG_G_SCALE = 0.05f;

    // Texture base names (AssetManager discovery)
    static constexpr const char* TEX_HELMET_UPPER = "helmet_upper";
    static constexpr const char* TEX_HELMET_LOWER = "helmet_lower";

    // Getters for the settings tab (variant cycling goes through these helpers)
    int getHelmetUpperVariant() const { return m_helmetUpperVariant; }
    int getHelmetLowerVariant() const { return m_helmetLowerVariant; }

    void setHelmetUpperVariant(int variant);
    void setHelmetLowerVariant(int variant);

    // Cycle helpers: Off -> 1 -> 2 -> ... -> Off
    void cycleHelmetUpperVariant(bool forward);
    void cycleHelmetLowerVariant(bool forward);

    // Public for settings access (config-style fields, same pattern as RumbleHud/LeanWidget)
    bool m_helmetEnabled = true;

    // Visor/tint mode: 0=Off, 1=Visor (tint behind helmet), 2=Goggles (tint in front)
    static constexpr int VISOR_OFF = 0;
    static constexpr int GOGGLES_MODE = 1;
    static constexpr int VISOR_MODE = 2;
    static constexpr int VISOR_MODE_COUNT = 3;
    int m_visorMode = VISOR_OFF;

    float m_helmetUpperOffsetY = 0.10f;      // -MAX_HELMET_OFFSET_Y .. +MAX_HELMET_OFFSET_Y
    float m_helmetLowerOffsetY = 0.05f;
    float m_helmetTiltStrength = 0.25f;      // -1..1 -> scales to MAX_TILT_DEG (negative = inverted)
    float m_helmetVibrationStrength = 0.5f;  // -1..1 -> amplitude and direction (negative inverts)
    // Default sensitivity differs per game: see resetToDefaults() for rationale.
#if GAME_HAS_SUSPENSION
    float m_helmetVibrationSensitivity = 0.5f; // 0..1 -> input gain (scales VIB_SCALE_MAX)
#else
    float m_helmetVibrationSensitivity = 0.05f;
#endif
    float m_helmetZoom = 0.10f;              // -MAX_OVERLAY_ZOOM..+MAX_OVERLAY_ZOOM (0 = 100%)

    // Visor tint — full-screen color applied behind the helmet.
    // Bleeds through the helmet's transparent visor cutout.
    unsigned long m_visorTintColor = ColorPalette::RED;
    float m_visorTintOpacity = 0.10f;        // 0..1

    // Texture variants (0 = off, 1 = shipped texture). Public for settings access + persistence.
    int m_helmetUpperVariant = 1;
    int m_helmetLowerVariant = 1;

private:
    void rebuildRenderData() override;

    // Look up sprite index for a variant (0 if not found / variant == 0)
    static int resolveSpriteIndex(const char* baseName, int variant);

    // Emit a full-screen textured quad with the given transforms.
    // cosA/sinA are precomputed from the tilt angle (shared across all quads).
    // translateY shifts the quad vertically after rotation (used for vibration/offsets).
    void addOverlayQuad(int spriteIndex,
                        float baseLeft, float baseTop, float baseRight, float baseBottom,
                        float cosA, float sinA, float pivotX, float pivotY,
                        float translateY,
                        unsigned long color);

    // Telemetry state for vibration smoothing. The "source" here is whichever
    // signal feeds the vibration channel for the current game (suspension total
    // for bikes, vertical G for karts) — same delta-and-smooth pipeline either way.
    float m_prevVibSource = 0.0f;
    float m_smoothedVibration = 0.0f;
    bool  m_hasVibBaseline = false;
    std::chrono::steady_clock::time_point m_lastVibrationTime{};
    int   m_cachedSessionGeneration = -1;

    // Sustained longitudinal-G shift state (karts only). Separate from the bump
    // smoother because it's a low-pass on accelZ itself, not a delta.
    float m_smoothedLongG = 0.0f;
};
