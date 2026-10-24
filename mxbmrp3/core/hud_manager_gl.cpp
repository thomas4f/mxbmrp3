// ============================================================================
// core/hud_manager_gl.cpp
// HudManager's IN-CONTEXT GL ARBITRATION ([Advanced] glInGame): the decision to
// draw the HUD inside the game's own GL context, the frame description handed
// to the backend, and the fail/reload gestures around it.
//
// Extracted VERBATIM from hud_manager_render.cpp when that file crossed its
// 1000-line budget - the budget's own rule is that an oversized file wants a
// split rather than a bigger number. Method bodies moved unchanged; the class
// definition and public API are untouched.
//
// It is also the right seam independently of the budget: this is the only
// EXPERIMENTAL surface in HudManager, and keeping it in one TU means a kill
// decision after Round 4 deletes a file rather than unpicking a render pipeline.
// ============================================================================

#include "hud_manager.h"

#include <algorithm>

#include "../hud/gl_confirm_hud.h"
#include "layout_config.h"
#include "../diagnostics/logger.h"
#include "../diagnostics/timer.h"
#include "asset_manager.h"
#include "companion_window.h"
#include "input_manager.h"
#include "xinput_reader.h"
#include "plugin_data.h"
#include "plugin_manager.h"
#include "settings_manager.h"
#include "director_manager.h"
#include "profile_manager.h"
#include "ui_config.h"
#include "render_probe_sweep.h"
#include "font_config.h"
#include "hud_gl_renderer.h"
#include "ui_viewport.h"
#include "../hud/base_hud.h"
#include "../hud/standings_hud.h"
#include "../hud/performance_hud.h"
#include "../hud/telemetry_hud.h"
#include "../hud/ideal_lap_hud.h"
#include "../hud/lap_log_hud.h"
#include "../hud/friends_hud.h"
#include "../hud/time_widget.h"
#include "../hud/position_widget.h"
#include "../hud/lap_widget.h"
#include "../hud/session_hud.h"
#include "../hud/speed_widget.h"
#include "../hud/gear_widget.h"
#include "../hud/speedo_widget.h"

void HudManager::clearGlFailLatch() {
    m_glLatchedOff.store(false, std::memory_order_relaxed);
}

void HudManager::requestGlArtReload() {
    // Reached from the WORKER thread in pluginThread mode (processKeyboardInput
    // runs inside produceFrame), which is why the pointer is atomic - see the
    // member declaration.
    if (hudgl::Renderer* r = m_glRenderer.load(std::memory_order_acquire)) r->requestArtReload();
}

// The ONE place the in-context GL frame is described. It exists as a function
// rather than inline so a test can inspect exactly what the renderer is handed:
// the defect it pins is not visible in any pixel assertion that builds its own
// frame, which is precisely how it survived a passing render suite.
hudsw::Frame HudManager::buildGlFrame(const SPluginQuad_t* quads, int quadCount,
                                      const SPluginString_t* strings, int stringCount,
                                      const hudlayers::Layers& layers) const {
    hudsw::Frame f;
    f.quads = quads; f.quadCount = quadCount;
    f.strings = strings; f.stringCount = stringCount;
    f.layers = layers;
    // The BASE tables, not m_fontNames/m_spriteNames. Those hold full paths with
    // extensions because that is what the game's DrawInit wants; hudsw::Frame
    // wants render names to resolve against assetRoot. Passing the paths here
    // is a SILENT failure - every lookup misses, the batcher skips the
    // primitive, and no GL error is raised - so the HUD loses all text and every
    // textured sprite while untextured fills keep drawing perfectly, which reads
    // as "the renderer works" right up until you look for a glyph. It shipped
    // that way on this branch; see the note at setupDefaultResources().
    f.fontNames = &m_fontBases;
    f.spriteNames = &m_spriteBases;
    f.firstIcon = AssetManager::getInstance().getFirstIconSpriteIndex();
    f.assetRoot = "plugins/mxbmrp3_data";
    return f;
}

// HUD STACKING (core/render_layers.h). Called from collectSurface once per
// drawn HUD, ghost and the debug grid, so the spans tile both arrays in order.
void HudManager::recordLayerSpan(SurfaceLayers& layers,
                                 const std::vector<SPluginQuad_t>& quads, size_t quadStart,
                                 const std::vector<SPluginString_t>& strings, size_t stringStart,
                                 const BaseHud* hud, bool companion, bool everywhere,
                                 const std::vector<int>* iconShifts, bool stringShadows) {
    if (quads.size() == quadStart && strings.size() == stringStart) return;   // drew nothing
    hudlayers::Rect rect;
    rect.everywhere = everywhere || !hud;
    if (!rect.everywhere) {
        // The drag bounds: the box the user sees and moves, at this surface's offset.
        hud->panelRect(rect.l, rect.t, rect.r, rect.b);
        const float ox = companion ? hud->getCompanionOffsetX() : hud->getOffsetX();
        const float oy = companion ? hud->getCompanionOffsetY() : hud->getOffsetY();
        rect.l += ox; rect.r += ox; rect.t += oy; rect.b += oy;
        if (rect.r <= rect.l || rect.b <= rect.t) {
            // No bounds (a full-screen effect, a HUD that never sets them): what it
            // actually drew. String extents are unknown without the font, so a
            // string counts as its anchor and one line of height.
            rect.l = rect.t = 1e9f; rect.r = rect.b = -1e9f;
            for (size_t i = quadStart; i < quads.size(); ++i)
                for (int c = 0; c < 4; ++c) {
                    rect.l = std::min(rect.l, quads[i].m_aafPos[c][0]);
                    rect.r = std::max(rect.r, quads[i].m_aafPos[c][0]);
                    rect.t = std::min(rect.t, quads[i].m_aafPos[c][1]);
                    rect.b = std::max(rect.b, quads[i].m_aafPos[c][1]);
                }
            for (size_t i = stringStart; i < strings.size(); ++i) {
                rect.l = std::min(rect.l, strings[i].m_afPos[0]);
                rect.r = std::max(rect.r, strings[i].m_afPos[0]);
                rect.t = std::min(rect.t, strings[i].m_afPos[1]);
                rect.b = std::max(rect.b, strings[i].m_afPos[1] + strings[i].m_fSize);
            }
        }
    }
    auto close = [&](size_t q0, size_t q1, size_t s0, size_t s1) {
        if (q0 == q1 && s0 == s1) return;
        hudlayers::Span span;
        span.q0 = static_cast<uint32_t>(q0); span.q1 = static_cast<uint32_t>(q1);
        span.s0 = static_cast<uint32_t>(s0); span.s1 = static_cast<uint32_t>(s1);
        layers.spans.push_back(span);
        layers.rects.push_back(rect);
        layers.owners.push_back(hud);
    };

    // The HUD's overlay (the open dropdown list) closes a span of its own, so it
    // stacks over this HUD's text. Its local indices map through the shadow
    // copies inserted in front of them.
    size_t quadSplit = quads.size(), stringSplit = strings.size();
    if (hud && hud->m_overlayQuadFirst >= 0 && hud->m_overlayStringFirst >= 0 &&
        hud->m_overlayQuadFirst <= static_cast<int>(hud->getQuads().size()) &&
        hud->m_overlayStringFirst <= static_cast<int>(hud->getStrings().size())) {
        const int lq = hud->m_overlayQuadFirst;
        quadSplit = quadStart + static_cast<size_t>(lq) + iconShiftsUpTo(iconShifts, lq);
        stringSplit = stringStart + static_cast<size_t>(hud->m_overlayStringFirst);
        if (stringShadows) {
            const auto& skip = hud->getStringSkipShadow();
            stringSplit = stringStart;
            for (int i = 0; i < hud->m_overlayStringFirst; ++i)
                stringSplit += (static_cast<size_t>(i) < skip.size() && skip[static_cast<size_t>(i)]) ? 1 : 2;
        }
        quadSplit = std::min(quadSplit, quads.size());
        stringSplit = std::min(stringSplit, strings.size());
    }
    close(quadStart, quadSplit, stringStart, stringSplit);
    close(quadSplit, quads.size(), stringSplit, strings.size());
}

bool HudManager::surfaceStacks(bool companion) const {
    return companion || (UiConfig::getInstance().getGlInGame() &&
                         !m_glLatchedOff.load(std::memory_order_relaxed));
}

void HudManager::finishLayers(SurfaceLayers& layers) {
    layers.count = hudlayers::assignLayers(layers.spans.data(), layers.rects.data(),
                                           static_cast<int>(layers.spans.size()));
}

bool HudManager::GlWarmList::sameAs(const GlWarmList& o) const {
    return fontCount == o.fontCount && std::equal(fonts, fonts + fontCount, o.fonts);
}

void HudManager::publishGlWarmList() {
    GlWarmList next;
    auto add = [&next](int idx) {
        if (idx <= 0 || next.fontCount >= GlWarmList::kMax) return;
        for (int i = 0; i < next.fontCount; ++i) if (next.fonts[i] == idx) return;
        next.fonts[next.fontCount++] = idx;
    };
    // Every font category: a category only one HUD uses (the pit board's MARKER)
    // is otherwise first decoded - a 2048x2048 atlas and its mip chain - on the
    // frame that HUD appears. Then each HUD's own override, shown or not. Not
    // every discovered font: each atlas is megabytes of texture, and a face no
    // setting names is never drawn. The sprites need no list (Warm::allSprites).
    const FontConfig& fonts = FontConfig::getInstance();
    for (int c = 0; c < static_cast<int>(FontCategory::COUNT); ++c)
        add(fonts.getFont(static_cast<FontCategory>(c)));
    for (const auto& hud : m_huds) {
        if (!hud) continue;
        for (int c = 0; c < static_cast<int>(FontCategory::COUNT); ++c)
            if (hud->hasFontOverride(static_cast<FontCategory>(c))) add(hud->getFont(static_cast<FontCategory>(c)));
    }
    if (next.sameAs(m_glWarmBuilt)) return;
    m_glWarmBuilt = next;
    {
        MutexLock lock(m_glWarmMutex);
        m_glWarmShared = next;
    }
    m_glWarmGen.fetch_add(1, std::memory_order_release);
}

#if defined(MXBMRP3_TEST_BUILD)
int HudManager::testGlHasTexture(const char* renderName) const {
    const hudgl::Renderer* gl = m_glRenderer.load(std::memory_order_acquire);
    if (!gl || !renderName) return -1;
    return gl->hasTexture(renderName) ? 1 : 0;
}
#endif

int HudManager::glStatusCode() const {
    if (!UiConfig::getInstance().getGlInGame()) return 0;
    if (m_glLatchedOff.load(std::memory_order_relaxed)) return 1;
    return m_glEverDrew.load(std::memory_order_relaxed) ? 2 : 3;
}

// See hud_manager.h for the contract. Game thread only.
bool HudManager::renderInContextGl(const SPluginQuad_t* quads, int quadCount,
                                   const SPluginString_t* strings, int stringCount,
                                   const hudlayers::Layers& layers) {
    // This function OWNS m_glDrewLastFrame, and is therefore called on every
    // frame including empty ones - the callers no longer gate it. A test hook
    // that can report a stale true is the same failure family as the sweep that
    // measured a draw which never happened.
    m_glDrewLastFrame.store(false, std::memory_order_relaxed);
    if (!UiConfig::getInstance().getGlInGame() ||
        m_glLatchedOff.load(std::memory_order_relaxed)) return false;

    if (quadCount <= 0 && stringCount <= 0) return false;

    hudgl::Renderer* gl = m_glRenderer.load(std::memory_order_acquire);
    if (!gl) {
        gl = new hudgl::Renderer();
        m_glRenderer.store(gl, std::memory_order_release);
        if (!gl->init()) {
            // Destroy it rather than keeping a dead instance. hudgl::init() caches
            // its result, so a retained failed renderer can NEVER succeed - and one
            // Draw arriving before the context is current (a plausible sequence, not
            // a contrived one) would then cost the feature for the whole session,
            // with clearGlFailLatch()'s documented retry producing only a misleading
            // "render failed". Freeing here is what makes that gesture mean what it
            // says: the next frame constructs a fresh renderer and tries again.
            // Latch and say why ONCE. This is the line a field report from a
            // machine unlike the author's has to be diagnosable from, so it
            // names the missing piece rather than just reporting failure.
            DEBUG_WARN_F("hudgl: in-context renderer unavailable (%s) - the engine "
                         "keeps drawing the HUD", gl->lastError().c_str());
            m_glRenderer.store(nullptr, std::memory_order_release);
            delete gl;
            m_glLatchedOff.store(true, std::memory_order_relaxed);
            return false;
        }
    }

    // Size from the LIVE GL viewport, not from any cached client rect: it is the
    // framebuffer actually being drawn into, and a mismatch would render to one
    // coordinate space while the game presents another.
    // Resolved ONCE, not per Draw. This runs in the callback whose frame time the
    // whole spike exists to reduce, so a GetModuleHandle + GetProcAddress pair every
    // frame just to read GL_VIEWPORT is exactly the kind of cost it is supposed to
    // be removing. opengl32 cannot be unloaded from under us while a context is
    // current on this thread, so caching the pointer is safe.
    static void (WINAPI* s_getIntegerv)(unsigned, int*) = nullptr;
    static bool s_getIntegervResolved = false;
    if (!s_getIntegervResolved) {
        s_getIntegervResolved = true;
        if (HMODULE glLib = GetModuleHandleA("opengl32.dll")) {
            s_getIntegerv = reinterpret_cast<void (WINAPI*)(unsigned, int*)>(
                reinterpret_cast<void*>(GetProcAddress(glLib, "glGetIntegerv")));
        }
    }
    int vw = 0, vh = 0;
    if (s_getIntegerv) {
        int vp[4] = { 0, 0, 0, 0 };
        s_getIntegerv(0x0BA2 /*GL_VIEWPORT*/, vp);
        vw = vp[2]; vh = vp[3];
    }
    if (vw <= 0 || vh <= 0) return false;

    // The same rect the engine itself maps normalized coords through - proven
    // in Phase 0 by drawing our bar and an engine bar at matching coordinates
    // and watching them line up at a non-16:9 client.
    const UiViewport::Rect ui = UiViewport::compute(vw, vh);

    const hudsw::Frame f = buildGlFrame(quads, quadCount, strings, stringCount, layers);

    // Pick up a changed pre-warm list; a steady frame is this one load.
    const unsigned warmGen = m_glWarmGen.load(std::memory_order_acquire);
    if (warmGen != m_glWarmDrawGen) {
        MutexLock lock(m_glWarmMutex);
        m_glWarmDraw = m_glWarmShared;
        m_glWarmDrawGen = warmGen;
    }
    hudgl::Renderer::Warm warm;
    warm.allSprites = true;
    warm.fonts = m_glWarmDraw.fonts;
    warm.fontCount = m_glWarmDraw.fontCount;

    if (!gl->render(f, vw, vh, static_cast<float>(ui.x), static_cast<float>(ui.y),
                              static_cast<float>(ui.w), static_cast<float>(ui.h), &warm)) {
        DEBUG_WARN_F("hudgl: render failed (%s) - falling back to engine rendering "
                     "for the rest of this session", gl->lastError().c_str());
        m_glLatchedOff.store(true, std::memory_order_relaxed);
        // The engine is drawing the HUD again, so it is readable by definition and
        // there is nothing left to ask. Drop the prompt WITHOUT touching the
        // setting: the status row already reports Failed, and silently rewriting
        // the user's choice on a transient failure would be its own surprise.
        if (m_pGlConfirm) m_pGlConfirm->cancel();
        return false;
    }
    m_glDrewLastFrame.store(true, std::memory_order_relaxed);
    m_glEverDrew.store(true, std::memory_order_relaxed);

    return true;
}
