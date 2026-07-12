// ============================================================================
// hud/helmet_overlay_hud.cpp
// ============================================================================
#include "helmet_overlay_hud.h"
#include "../core/plugin_data.h"
#include "../core/plugin_utils.h"
#include "../core/asset_manager.h"
#include "../core/color_config.h"
#include "../diagnostics/logger.h"
#include <algorithm>
#include <cmath>
#include <vector>

using namespace PluginConstants;

HelmetOverlayHud::HelmetOverlayHud() {
    DEBUG_INFO("HelmetOverlayHud created");

    // The overlay is non-interactive: no dragging, no title, zero bounds.
    setDraggable(false);
    m_bShowTitle = false;
    m_fBackgroundOpacity = 1.0f;

    // Max 3 quads: visor tint + lower + upper.
    m_quads.reserve(3);

    resetToDefaults();
    rebuildRenderData();
}

void HelmetOverlayHud::update() {
    // Always rebuild when visible: tilt + vibration follow telemetry every frame.
    if (!isVisibleAnySurface()) {
        clearDataDirty();
        clearLayoutDirty();
        return;
    }

    rebuildRenderData();
    clearDataDirty();
    clearLayoutDirty();
}

bool HelmetOverlayHud::handlesDataType(DataChangeType dataType) const {
    // Telemetry drives tilt + vibration; InputTelemetry fires at physics rate.
    // SessionData resets vibration state on new session (avoids one-frame jolt).
    return dataType == DataChangeType::InputTelemetry ||
           dataType == DataChangeType::SessionData;
}

void HelmetOverlayHud::resetToDefaults() {
    m_bVisible = false;
    m_bShowTitle = false;
    setTextureVariant(0);
    m_fBackgroundOpacity = 1.0f;
    m_fScale = 1.0f;

    m_helmetEnabled = true;
    m_visorMode = VISOR_OFF;

    m_helmetUpperVariant = 1;
    m_helmetLowerVariant = 1;

    m_helmetUpperOffsetY = 0.10f;
    m_helmetLowerOffsetY = 0.05f;
    m_helmetTiltStrength = 0.25f;
    m_helmetVibrationStrength = 0.5f;
#if GAME_HAS_SUSPENSION
    m_helmetVibrationSensitivity = 0.5f;
#else
    // Karts: vertical-G deltas are ~10x larger in raw units than suspension
    // deltas, so a much lower sensitivity gives a comparable bump intensity.
    m_helmetVibrationSensitivity = 0.05f;
#endif
    m_helmetZoom = 0.10f;

    m_visorTintColor = ColorPalette::RED;
    m_visorTintOpacity = 0.10f;

    m_prevVibSource = 0.0f;
    m_smoothedVibration = 0.0f;
    m_hasVibBaseline = false;
    m_smoothedLongG = 0.0f;
    m_cachedSessionGeneration = -1;

    setDataDirty();
}

void HelmetOverlayHud::setHelmetUpperVariant(int variant) {
    if (variant < 0) variant = 0;
    if (m_helmetUpperVariant != variant) {
        m_helmetUpperVariant = variant;
        setDataDirty();
    }
}

void HelmetOverlayHud::setHelmetLowerVariant(int variant) {
    if (variant < 0) variant = 0;
    if (m_helmetLowerVariant != variant) {
        m_helmetLowerVariant = variant;
        setDataDirty();
    }
}

static void cycleVariantGeneric(int& currentVariant, const char* baseName, bool forward) {
    auto variants = AssetManager::getInstance().getAvailableVariants(baseName);
    if (variants.empty()) {
        currentVariant = 0;
        return;
    }

    // Cycle order: 0 (Off) then all variants in discovery order
    std::vector<int> cycleOrder = {0};
    cycleOrder.insert(cycleOrder.end(), variants.begin(), variants.end());

    int currentIndex = 0;
    for (size_t i = 0; i < cycleOrder.size(); ++i) {
        if (cycleOrder[i] == currentVariant) {
            currentIndex = static_cast<int>(i);
            break;
        }
    }

    const int cycleSize = static_cast<int>(cycleOrder.size());
    int newIndex = forward
        ? (currentIndex + 1) % cycleSize
        : (currentIndex - 1 + cycleSize) % cycleSize;

    currentVariant = cycleOrder[newIndex];
}

void HelmetOverlayHud::cycleHelmetUpperVariant(bool forward) {
    cycleVariantGeneric(m_helmetUpperVariant, TEX_HELMET_UPPER, forward);
    setDataDirty();
}

void HelmetOverlayHud::cycleHelmetLowerVariant(bool forward) {
    cycleVariantGeneric(m_helmetLowerVariant, TEX_HELMET_LOWER, forward);
    setDataDirty();
}

int HelmetOverlayHud::resolveSpriteIndex(const char* baseName, int variant) {
    if (variant <= 0 || !baseName) return 0;
    return AssetManager::getInstance().getSpriteIndex(baseName, variant);
}

void HelmetOverlayHud::addOverlayQuad(int spriteIndex,
                                      float baseLeft, float baseTop, float baseRight, float baseBottom,
                                      float cosA, float sinA, float pivotX, float pivotY,
                                      float translateY,
                                      unsigned long color) {
    SPluginQuad_t quad{};

    // Vertex order (matches BaseHud::setQuadPositions):
    //   0 = top-left, 1 = bottom-left, 2 = bottom-right, 3 = top-right
    quad.m_aafPos[0][0] = baseLeft;   quad.m_aafPos[0][1] = baseTop;
    quad.m_aafPos[1][0] = baseLeft;   quad.m_aafPos[1][1] = baseBottom;
    quad.m_aafPos[2][0] = baseRight;  quad.m_aafPos[2][1] = baseBottom;
    quad.m_aafPos[3][0] = baseRight;  quad.m_aafPos[3][1] = baseTop;

    // Apply rotation around pivot (skip if no rotation: cosA==1, sinA==0).
    if (std::fabs(sinA) > 0.0001f) {
        for (int i = 0; i < 4; ++i) {
            const float dx = (quad.m_aafPos[i][0] - pivotX) * UI_ASPECT_RATIO;
            const float dy = (quad.m_aafPos[i][1] - pivotY);
            const float rx = dx * cosA - dy * sinA;
            const float ry = dx * sinA + dy * cosA;
            quad.m_aafPos[i][0] = pivotX + rx / UI_ASPECT_RATIO;
            quad.m_aafPos[i][1] = pivotY + ry;
        }
    }

    // Apply vertical translation (vibration + user Y offset).
    if (std::fabs(translateY) > 0.0001f) {
        for (int i = 0; i < 4; ++i) {
            quad.m_aafPos[i][1] += translateY;
        }
    }

    quad.m_iSprite = spriteIndex;
    quad.m_ulColor = color;
    m_quads.push_back(quad);
}

void HelmetOverlayHud::rebuildRenderData() {
    m_quads.clear();

    // Don't render anything when spectating / in replay — the player isn't inside
    // a helmet in those views, overlay would be confusing.
    const PluginData& pluginData = PluginData::getInstance();
    if (pluginData.getDrawState() != ViewState::ON_TRACK) {
        m_prevVibSource = 0.0f;
        m_smoothedVibration = 0.0f;
        m_hasVibBaseline = false;
        m_smoothedLongG = 0.0f;
        return;
    }

    // Hide when crashed — the game forces an external camera so the helmet
    // would float in mid-air. Reset vibration state so there's no jolt on recovery.
    const TrackPositionData* playerPos = pluginData.getPlayerTrackPosition();
    if (playerPos && playerPos->crashed) {
        m_smoothedVibration = 0.0f;
        m_hasVibBaseline = false;
        m_smoothedLongG = 0.0f;
        return;
    }

    // Reset vibration state on new session (avoids one-frame jolt from stale baseline)
    const int sessionGen = pluginData.getSessionData().sessionGeneration;
    if (sessionGen != m_cachedSessionGeneration) {
        m_cachedSessionGeneration = sessionGen;
        m_smoothedVibration = 0.0f;
        m_hasVibBaseline = false;
        m_smoothedLongG = 0.0f;
    }

    const bool anyHelmetPart = m_helmetEnabled &&
        (m_helmetUpperVariant > 0 || m_helmetLowerVariant > 0);
    const bool hasTint = m_visorMode != VISOR_OFF && helmetStrengthActive(m_visorTintOpacity);

    if (!anyHelmetPart && !hasTint) {
        return;
    }

    // ------------------------------------------------------------------------
    // Telemetry -> tilt angle
    // ------------------------------------------------------------------------
    const BikeTelemetryData& telemetry = pluginData.getBikeTelemetry();

    float tiltDeg = 0.0f;
    if (m_helmetEnabled && telemetry.isValid && helmetStrengthActive(m_helmetTiltStrength)) {
#if GAME_HAS_SUSPENSION
        // Bikes: BikeTelemetryData::roll is in degrees (negative = left lean, positive = right).
        // Map [-45, +45] deg lean to [-MAX_TILT_DEG, +MAX_TILT_DEG] at full strength.
        // Negate so the helmet COUNTER-tilts against the bike's roll — i.e. in a
        // right lean (roll > 0) the on-screen helmet top moves left, modelling
        // the rider's head trying to stay vertical against the bike's lean.
        const float rollClamped = std::clamp(telemetry.roll, -45.0f, 45.0f);

        // Gimbal lock: as pitch approaches ±90° (near-vertical wheelie / endo),
        // Euler roll flips sign, causing a jarring snap in helmet tilt. Fade the
        // tilt out as |pitch| goes from 60° to 80° so the overlay settles level
        // through the inversion instead of whipping around.
        const float absPitch = std::fabs(telemetry.pitch);
        float pitchFade = 1.0f;
        if (absPitch > 60.0f) {
            pitchFade = std::clamp(1.0f - (absPitch - 60.0f) / 20.0f, 0.0f, 1.0f);
        }

        tiltDeg = -rollClamped * (MAX_TILT_DEG / 45.0f) * m_helmetTiltStrength * pitchFade;
#else
        // Karts: rigid chassis (roll ≈ 0), so source tilt from lateral G instead.
        // accelX is chassis-local lateral acceleration (positive = chassis
        // accelerating right, i.e. kart turning RIGHT; same convention used by
        // gforce_widget, where it's negated to convert into rider felt-G).
        // Real drivers brace their head INTO the turn against centripetal load,
        // so positive accelX tilts the helmet top RIGHT in a right turn. Note
        // this is the OPPOSITE on-screen direction from the bike branch in the
        // same turn — bikes counter-tilt against roll, karts brace into G —
        // and that's correct: different head-stabilisation models for different
        // vehicles. Map ±1.5g to ±MAX_TILT_DEG at full strength.
        const float lateralG = std::clamp(telemetry.accelX, -1.5f, 1.5f);
        tiltDeg = lateralG * (MAX_TILT_DEG / 1.5f) * m_helmetTiltStrength;
#endif
    }

    // ------------------------------------------------------------------------
    // Telemetry -> vibration Y offset (both helmet parts)
    // ------------------------------------------------------------------------
    float vibY = 0.0f;
    if (m_helmetEnabled && telemetry.isValid && helmetStrengthActive(m_helmetVibrationStrength)) {
#if GAME_HAS_SUSPENSION
        // Bikes: source = combined suspension length. Compression on a bump
        // shortens the total, so delta = prev - curr gives a POSITIVE spike on
        // bump (helmet shifts up with positive strength).
        const float vibSource = telemetry.frontSuspLength + telemetry.rearSuspLength;
        const float deltaSign = +1.0f;  // prev - curr
#else
        // Karts: source = vertical chassis G. The absolute rest value isn't
        // relevant (the pipeline is delta-based and high-passes any DC offset);
        // bumps and kerbs spike accelY upward, so delta = curr - prev gives a
        // POSITIVE spike on bump (same effective direction as the bike branch).
        const float vibSource = telemetry.accelY;
        const float deltaSign = -1.0f;  // prev - curr would invert; use curr - prev
#endif

        if (!m_hasVibBaseline) {
            m_prevVibSource = vibSource;
            m_hasVibBaseline = true;
            m_lastVibrationTime = std::chrono::steady_clock::now();
        }

        // Signed delta in the direction where "bump" → positive.
        const float delta = deltaSign * (m_prevVibSource - vibSource);
        m_prevVibSource = vibSource;

        // Frame-rate-independent exponential low-pass: weight = exp(-decay * dt).
        // Produces consistent feel across 60fps and 240fps.
        const auto now = std::chrono::steady_clock::now();
        const float dt = std::chrono::duration<float>(now - m_lastVibrationTime).count();
        m_lastVibrationTime = now;
        const float dtClamped = std::min(dt, 0.1f);  // Cap at 100ms to avoid huge jumps after stalls
        const float weight = std::exp(-VIB_DECAY_RATE * dtClamped);
        m_smoothedVibration = m_smoothedVibration * weight + delta * (1.0f - weight);

        const float vibScale = m_helmetVibrationSensitivity * VIB_SCALE_MAX;
        vibY = m_smoothedVibration * vibScale * MAX_VIBRATION_Y * m_helmetVibrationStrength;

#if !GAME_HAS_SUSPENSION
        // Karts: sustained longitudinal-G shift. Low-pass directly on accelZ
        // (not a delta) so the offset persists while the brake is held instead
        // of decaying out. Negative accelZ = braking → positive shift (helmet
        // dips down on screen, "camera dip" feel). Positive accelZ = throttle
        // → negative shift (helmet rises). Sensitivity slider is intentionally
        // NOT applied here — that knob governs bump gain; the long-G scale is
        // fixed so the two components don't interfere when sensitivity is
        // dialled up to feel kerbs.
        const float longWeight = std::exp(-LONG_G_DECAY_RATE * dtClamped);
        m_smoothedLongG = m_smoothedLongG * longWeight + telemetry.accelZ * (1.0f - longWeight);
        vibY += -m_smoothedLongG * LONG_G_SCALE * m_helmetVibrationStrength;
#endif

        vibY = std::clamp(vibY, -MAX_VIBRATION_Y, MAX_VIBRATION_Y);
    } else if (std::fabs(m_smoothedVibration) > 0.0001f || std::fabs(m_smoothedLongG) > 0.0001f) {
        // Decay stale vibration state (dt-based for frame-rate independence).
        const auto now = std::chrono::steady_clock::now();
        const float dt = std::chrono::duration<float>(now - m_lastVibrationTime).count();
        m_lastVibrationTime = now;
        const float dtClamped = std::min(dt, 0.1f);
        m_smoothedVibration *= std::exp(-VIB_DECAY_RATE * dtClamped);
        m_smoothedLongG *= std::exp(-LONG_G_DECAY_RATE * dtClamped);
    }

    // Precompute tilt rotation once (shared by all quads this frame).
    const float tiltRad = tiltDeg * 3.14159265358979323846f / 180.0f;
    const float cosT = std::cos(tiltRad);
    const float sinT = std::sin(tiltRad);

    // Tilt pivot: bottom-center of the screen (approximates the rider's neck).
    const float pivotX = 0.5f;
    const float pivotY = 1.0f;

    const unsigned long WHITE = PluginUtils::makeColor(255, 255, 255, 255);

    // ------------------------------------------------------------------------
    // Helmet rest geometry (scaled by helmet zoom around screen center)
    // ------------------------------------------------------------------------
    const float hZoom = std::clamp(m_helmetZoom, -MAX_OVERLAY_ZOOM, MAX_OVERLAY_ZOOM);
    const float hHalf = 0.5f + TEXTURE_BLEED + hZoom;
    const float hL = 0.5f - hHalf;
    const float hR = 0.5f + hHalf;
    const float hT = 0.5f - hHalf;
    const float hB = 0.5f + hHalf;

    // Helper: emit full-screen tint quad (no rotation/translation)
    auto emitTint = [&]() {
        if (hasTint && m_visorTintColor != 0) {
            addOverlayQuad(SpriteIndex::SOLID_COLOR, 0.0f, 0.0f, 1.0f, 1.0f,
                           1.0f, 0.0f, pivotX, pivotY, 0.0f,
                           PluginUtils::applyOpacity(m_visorTintColor, m_visorTintOpacity));
        }
    };

    // Helper: emit both helmet texture quads
    auto emitHelmet = [&]() {
        if (m_helmetEnabled && m_helmetLowerVariant > 0) {
            const int sprite = resolveSpriteIndex(TEX_HELMET_LOWER, m_helmetLowerVariant);
            if (sprite > 0) {
                const float offsetY = std::clamp(m_helmetLowerOffsetY,
                                                 -MAX_HELMET_OFFSET_Y, MAX_HELMET_OFFSET_Y);
                addOverlayQuad(sprite, hL, hT, hR, hB,
                               cosT, sinT, pivotX, pivotY,
                               offsetY + vibY, WHITE);
            }
        }
        if (m_helmetEnabled && m_helmetUpperVariant > 0) {
            const int sprite = resolveSpriteIndex(TEX_HELMET_UPPER, m_helmetUpperVariant);
            if (sprite > 0) {
                const float offsetY = std::clamp(m_helmetUpperOffsetY,
                                                 -MAX_HELMET_OFFSET_Y, MAX_HELMET_OFFSET_Y);
                addOverlayQuad(sprite, hL, hT, hR, hB,
                               cosT, sinT, pivotX, pivotY,
                               offsetY + vibY, WHITE);
            }
        }
    };

    // Draw order depends on visor mode:
    //   Visor:   tint behind helmet (bleeds through visor cutout)
    //   Goggles: tint in front of helmet (tinted lens over everything)
    if (m_visorMode == VISOR_MODE) {
        emitTint();
        emitHelmet();
    } else {
        emitHelmet();
        emitTint();
    }
}
