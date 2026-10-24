// ============================================================================
// hud/impact_vignette_hud.h
// Impact vignette: one full-screen textured quad that darkens the screen edges
// on a hard hit while Settings > Helmet is on (Settings > Helmet > Impact
// vignette). The envelope and the numbers are core/impact_vignette.h; this
// only feeds it and draws it.
//
// The art is mxbmrp3_data/textures/impact_vignette_N.tga (tools/vignette_gen.py):
// black, with streaky or smoky edges in the alpha. Each hit that starts from a
// clear screen picks another variant, so hits do not all look alike; a hit
// during a fade keeps the one showing, so it never pops. Stretched over the
// screen each is an ellipse, as a lens vignette is. One quad, so it costs the
// same on the game's renderer, Direct GL and at any hit size.
//
// Not a panel: no title, no dragging, never on the companion window, and no
// Motion fade (it animates itself). Visible always; it draws nothing while
// Settings > Helmet or the strength is Off, off track, or between hits.
// Registered right after the helmet so every HUD stays readable over it.
// ============================================================================
#pragma once

#include <chrono>

#include "base_hud.h"
#include "../core/impact_vignette.h"

class ImpactVignetteHud : public BaseHud {
public:
    bool rendersOnCompanion() const override { return false; }
    MotionStyle motionStyle() const override { return MotionStyle::NONE; }

    ImpactVignetteHud();
    virtual ~ImpactVignetteHud() = default;

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;

    static constexpr const char* TEX_VIGNETTE = "impact_vignette";

private:
    void rebuildRenderData() override;
    void settle();   // drop the envelope and anything drawn
    void pickVariant();   // a new hit from a clear screen: another variant's sprite
    void refreshVariants();   // the variants' sprites, again only when the assets change

    static constexpr int MAX_VARIANTS = 8;
    int m_variants[MAX_VARIANTS] = {};
    int m_variantCount = 0;
    unsigned int m_variantGen = 0xFFFFFFFFu;   // AssetManager::themeGeneration they were read under

    ImpactVignette::Envelope m_envelope;
    std::chrono::steady_clock::time_point m_lastStep{};
    bool m_hasLastStep = false;
    // Crashed as of the last frame: the frame the flag rises still takes its
    // hit, which can be the one that threw the rider.
    bool m_wasCrashed = false;
    int m_sprite = 0;        // the variant this hit draws; 0 = art missing
    unsigned m_hitCount = 0; // drives the variant pick
};
