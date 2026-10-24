// ============================================================================
// hud/impact_vignette_hud.cpp
// ============================================================================
#include "impact_vignette_hud.h"
#include "../core/plugin_data.h"
#include "../core/plugin_utils.h"
#include "../core/asset_manager.h"
#include "../core/ui_config.h"
#include "../core/hud_manager.h"
#include "helmet_overlay_hud.h"

ImpactVignetteHud::ImpactVignetteHud() {
    // No caption on this panel -- see BaseHud::m_titleSupported.
    disableTitle();
    setDraggable(false);
    m_bShowTitle = false;
    m_bVisible = true;   // Off is the strength, not this flag
    m_quads.reserve(1);
}

bool ImpactVignetteHud::handlesDataType(DataChangeType) const {
    // Driven from update() every frame; no change type needs a rebuild.
    return false;
}

void ImpactVignetteHud::settle() {
    m_envelope.reset();
    m_hasLastStep = false;
    m_wasCrashed = false;
    m_quads.clear();
}

void ImpactVignetteHud::refreshVariants() {
    // Per frame while the vignette can show, so the lookups run only when the
    // assets were rediscovered.
    const AssetManager& assets = AssetManager::getInstance();
    if (m_variantGen == assets.themeGeneration()) return;
    m_variantGen = assets.themeGeneration();
    m_variantCount = 0;
    for (int v = 1; v <= MAX_VARIANTS; ++v) {
        const int sprite = assets.getSpriteIndex(TEX_VIGNETTE, v);
        if (sprite > 0) m_variants[m_variantCount++] = sprite;
    }
}

void ImpactVignetteHud::pickVariant() {
    refreshVariants();
    const int* sprites = m_variants;
    const int count = m_variantCount;
    if (count == 0) { m_sprite = 0; return; }
    // Never the same one twice running: step 1..count-1 on from the last.
    ++m_hitCount;
    // The first hit of a session starts anywhere, not always on variant 1.
    int last = static_cast<int>(std::chrono::steady_clock::now().time_since_epoch().count() % count);
    for (int i = 0; i < count; ++i) if (sprites[i] == m_sprite) last = i;
    const int stepBy = count > 1 ? 1 + static_cast<int>((m_hitCount * 2654435761u) >> 16) % (count - 1) : 0;
    m_sprite = sprites[(last + stepBy) % count];
}

void ImpactVignetteHud::update() {
    clearDataDirty();
    clearLayoutDirty();

    PluginData& pluginData = PluginData::getInstance();
    // Drained every frame, so a hit taken while Off does not fire on switching it on.
    const float peakG = pluginData.takeImpactPeakG();

    // On only while Settings > Helmet is, as the visor: the helmet art itself
    // need not be shown.
    const float strength = UiConfig::getInstance().getImpactVignette();
    const bool helmetOn = HudManager::getInstance().getHelmetOverlayHud().isVisible();
    if (!isVisibleAnySurface() || strength <= 0.0f || !helmetOn ||
        pluginData.getDrawState() != PluginConstants::ViewState::ON_TRACK) {
        if (m_hasLastStep || !m_quads.empty()) settle();
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    // The first frame back on track has no previous one: count it as a 60 fps
    // frame, so a hit taken on it shows on it rather than one frame later.
    const float dt = m_hasLastStep ? std::chrono::duration<float>(now - m_lastStep).count()
                                   : 1.0f / 60.0f;
    m_lastStep = now;
    m_hasLastStep = true;

    // Crashed: the game cuts to an outside camera, so the tumble that follows
    // is not felt from the helmet. The hit that crashed you fades out as usual,
    // even when the flag rises on the same frame as it (the peak covers every
    // sample since the last frame); the knocks after it add nothing.
    const RiderTrackState* player = pluginData.getPlayerTrackPosition();
    const bool crashed = player && player->crashed;
    const float hit = (crashed && m_wasCrashed) ? 0.0f : ImpactVignette::hitFromG(peakG);
    m_wasCrashed = crashed;
    if (hit > 0.0f && m_envelope.idle()) pickVariant();
    m_envelope.step(hit, dt, UiConfig::getInstance().getImpactVignetteFade());
    if (m_envelope.idle()) {
        m_quads.clear();
        return;
    }
    rebuildAndRecord();
}

void ImpactVignetteHud::rebuildRenderData() {
    m_quads.clear();

    if (m_sprite <= 0) return;   // art missing: draw nothing rather than a black screen

    const float level = m_envelope.level;
    const float alpha = ImpactVignette::opacity(level, UiConfig::getInstance().getImpactVignette());
    const float half = ImpactVignette::halfExtent(level);
    const float l = 0.5f - half, r = 0.5f + half, t = 0.5f - half, b = 0.5f + half;

    // Vertex order (matches BaseHud::setQuadPositions):
    //   0 = top-left, 1 = bottom-left, 2 = bottom-right, 3 = top-right
    SPluginQuad_t quad{};
    quad.m_aafPos[0][0] = l; quad.m_aafPos[0][1] = t;
    quad.m_aafPos[1][0] = l; quad.m_aafPos[1][1] = b;
    quad.m_aafPos[2][0] = r; quad.m_aafPos[2][1] = b;
    quad.m_aafPos[3][0] = r; quad.m_aafPos[3][1] = t;
    quad.m_iSprite = m_sprite;
    quad.m_ulColor = PluginUtils::applyOpacity(PluginUtils::makeColor(255, 255, 255, 255), alpha);
    m_quads.push_back(quad);
}
