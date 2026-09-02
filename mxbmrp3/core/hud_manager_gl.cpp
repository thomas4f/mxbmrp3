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
#include "gl_probe.h"
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
                                      const SPluginString_t* strings, int stringCount) const {
    hudsw::Frame f;
    f.quads = quads; f.quadCount = quadCount;
    f.strings = strings; f.stringCount = stringCount;
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

int HudManager::glStatusCode() const {
    if (!UiConfig::getInstance().getGlInGame()) return 0;
    if (m_glLatchedOff.load(std::memory_order_relaxed)) return 1;
    return m_glEverDrew.load(std::memory_order_relaxed) ? 2 : 3;
}

// See hud_manager.h for the contract. Game thread only.
bool HudManager::renderInContextGl(const SPluginQuad_t* quads, int quadCount,
                                   const SPluginString_t* strings, int stringCount) {
    // This function OWNS m_glDrewLastFrame, and is therefore called on every
    // frame including empty ones - the callers no longer gate it. A test hook
    // that can report a stale true is the same failure family as the sweep that
    // measured a draw which never happened.
    m_glDrewLastFrame.store(false, std::memory_order_relaxed);
    if (!UiConfig::getInstance().getGlInGame() ||
        m_glLatchedOff.load(std::memory_order_relaxed)) return false;

    // glProbe=2 WINS over glInGame, and they must never both take effect.
    // The probe's whole method is a PAIR of bars - one drawn by the engine, one
    // drawn by us into the GL context - stacked flush so any disagreement in the
    // two coordinate mappings is visible in one look. Suppressing the engine
    // frame routes the engine's own reference bar through this backend too, and
    // the comparison silently degenerates to GL against GL: it would still look
    // like agreement, and would prove nothing at all. Refusing here keeps the
    // pairing honest and costs only that glInGame does not apply while probing.
    if (UiConfig::getInstance().getGlProbe() >= 2) {
        if (!m_glProbeConflictLogged) {
            m_glProbeConflictLogged = true;
            DEBUG_WARN("hudgl: [Advanced] glProbe=2 and glInGame=1 are both set - the "
                       "in-context renderer is STAYING OFF for this session. The probe "
                       "compares an engine-drawn bar against a GL-drawn one; suppressing "
                       "the engine frame would draw both through GL and prove nothing. "
                       "Set glProbe=0 to use glInGame.");
        }
        return false;
    }
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

    const hudsw::Frame f = buildGlFrame(quads, quadCount, strings, stringCount);

    if (!gl->render(f, vw, vh, static_cast<float>(ui.x), static_cast<float>(ui.y),
                              static_cast<float>(ui.w), static_cast<float>(ui.h))) {
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
