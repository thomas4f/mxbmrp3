// ============================================================================
// core/hud_manager_render.cpp
// HudManager render pipeline — frame production and render-primitive collection
// (draw / produceFrame / updateHuds / collectRenderData / collectSurface, the
// grid overlay, and the test-only produce-delay hook). Extracted verbatim from
// hud_manager.cpp (which keeps lifecycle, resources, data glue and input) when
// that file grew past ~1.6k lines. Class definition, members, and public API
// are unchanged — only where these method bodies live moves. Same
// byte-identical-extraction pattern as the plugin_data / http_server splits.
// The include set mirrors hud_manager.cpp's so every HUD type stays visible.
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
#include "../hud/tacho_widget.h"
#include "../hud/timing_hud.h"
#include "../hud/bars_widget.h"
#include "../hud/breakout_hud.h"
#include "../hud/crash_widget.h"
#include "../hud/rpm_widget.h"
#include "../hud/prestige_widget.h"
#include "../hud/achievement_widget.h"
#include "../hud/notices_hud.h"
#include "../hud/notice_tracker.h"
#include "system_messages.h"
#include "../hud/dash_hud.h"
#include "../hud/settings_hud.h"
#include "../hud/map_hud.h"
#include "../hud/radar_hud.h"
#include "../hud/pitboard_hud.h"
#include "../hud/fuel_widget.h"
#if GAME_HAS_RECORDS_PROVIDER
#include "../hud/records_hud.h"
#endif
#include "../hud/gap_bar_hud.h"
#include "../hud/pointer_widget.h"
#include "../hud/rumble_hud.h"
#include "../hud/gamepad_widget.h"
#include "../hud/lean_widget.h"
#include "../hud/gforce_widget.h"
#include "../hud/compass_widget.h"
#include "../hud/clock_widget.h"
#if GAME_HAS_TYRE_TEMP
#include "../hud/tyre_temp_widget.h"
#endif
#if GAME_HAS_ECU
#include "../hud/ecu_widget.h"
#endif
#include "../hud/session_charts_hud.h"
#include "../hud/helmet_overlay_hud.h"
#include "../hud/fmx_hud.h"
#include "../hud/stats_hud.h"
#include "../hud/event_log_hud.h"
#include "../hud/benchmark_widget.h"
#include "../hud/toggle_strip_widget.h"
#include "hotkey_manager.h"
#if GAME_HAS_HTTP_SERVER
#include "http_server.h"
#endif
#include "../handlers/draw_handler.h"
#include "color_config.h"
#include <windows.h>
#include <algorithm>
#include <memory>
#include <cstring>
#if defined(MXBMRP3_TEST_BUILD)
#include <atomic>
#endif

#if defined(MXBMRP3_TEST_BUILD)
// Test-only: an artificial per-frame stall injected into produceFrame() to stand in
// for a genuinely slow component (e.g. a heavy Map HUD ribbon re-tessellation). Lets
// a headless test prove that such a stall blocks the game's Draw in synchronous mode
// but NOT in plugin-thread mode. Zero by default; never compiled into a shipping DLL.
namespace { std::atomic<int> s_testProduceDelayMs{ 0 }; }
void HudManager::testSetProduceDelayMs(int ms) {
    s_testProduceDelayMs.store(ms < 0 ? 0 : ms, std::memory_order_relaxed);
}
#endif

void HudManager::draw(int iState, int* piNumQuads, void** ppQuad, int* piNumString, void** ppString) {

    if (!m_bInitialized) {
        *piNumQuads = 0;
        *piNumString = 0;
        return;
    }

    // Build this frame (shared with the plugin worker thread — see produceFrame).
    produceFrame(iState);

    // Hand the built in-game frame to the game, honoring the display-target gate that
    // produceFrame() resolved (COMPANION mode ⇒ empty game frame).
    if (m_bSuppressInGame) {
        *piNumQuads = 0; *ppQuad = nullptr;
        *piNumString = 0; *ppString = nullptr;
    } else {
        *piNumQuads = static_cast<int>(m_quads.size());
        *ppQuad = m_quads.empty() ? nullptr : m_quads.data();
        *piNumString = static_cast<int>(m_strings.size());
        *ppString = m_strings.empty() ? nullptr : m_strings.data();
    }
}

bool HudManager::standsDown(const BaseHud* hud) const {
    return m_pGlConfirm && m_pGlConfirm->isActive() && hud && hud == m_pSettingsHud;
}

// EVERY row of the Widgets tab, which is what the master toggle and its hotkey
// mean by "the widgets". The Crash and Prestige widgets were missing and so sat
// through a toggle that took every other one off screen - a hand-kept list, and
// the two newest names are exactly what a hand-kept list loses.
bool HudManager::isWidgetHud(const BaseHud* hud) const {
    return hud == m_pLap || hud == m_pPosition || hud == m_pTime ||
           hud == m_pSpeed || hud == m_pGear || hud == m_pSpeedo || hud == m_pTacho ||
           hud == m_pBars || hud == m_pFuel ||
           hud == m_pGamepad || hud == m_pLean || hud == m_pGforce || hud == m_pCompass ||
           hud == m_pClock || hud == m_pCrash || hud == m_pPrestige || hud == m_pRpm;
}

bool HudManager::isHeldBack(const BaseHud* hud) const {
    if (!hud) return false;
    if (standsDown(hud) || coveredByBorrowedDash(hud)) return true;
    if (hud == m_pBreakout && m_pBreakout->isGameActive()) return false;
    // The hide-all hotkey spares the settings chrome (the menu and the Toggles
    // strip) and the pointer (how the HUD comes back), and its own "All HUDs
    // hidden" card for its few seconds.
    const bool chrome = hud == m_pSettingsHud || hud == m_pToggleStrip || hud == m_pPointer;
    const bool hideAllNotice = hud == m_pAchievement && m_pAchievement->isShowingThroughHideAll();
    if (m_bAllHudsToggledOff && !chrome && !hideAllNotice) return true;
    return m_bAllWidgetsToggledOff && isWidgetHud(hud);
}

void HudManager::produceFrame(int iState) {
    if (!m_bInitialized) {
        m_bSuppressInGame = false;
        m_quads.clear();
        m_strings.clear();
        return;
    }

    // Track draw state for spectate-mode support. Done here (not in DrawHandler) so it
    // runs on whichever thread owns the render build — the game thread in sync mode,
    // the worker thread in plugin-thread mode — and never races PluginData.
    // Bracketed for the benchmark's Draw attribution -- see BenchmarkMetrics.
    const long long frameStart = DrawHandler::getCurrentTimeUs();
    PluginData::getInstance().setDrawState(iState);

#if defined(MXBMRP3_TEST_BUILD)
    // Simulated slow-component stall (see s_testProduceDelayMs). This is the exact
    // cost that, in sync mode, is paid ON the game thread inside Draw — and in
    // plugin-thread mode is paid on the worker instead, so Draw never feels it.
    {
        int delayMs = s_testProduceDelayMs.load(std::memory_order_relaxed);
        if (delayMs > 0) Sleep(static_cast<DWORD>(delayMs));
    }
#endif

    // "Entered the track" detection: the game calls Draw continuously while we're on
    // track / spectating, and stops in menus / loading. A long gap since the previous
    // Draw (or the very first Draw) means drawing just resumed, so flash the Toggles
    // strip's controls into view - they otherwise auto-hide with the idle cursor, and
    // this helps users find the menu without moving the mouse. Time-based so it's
    // FPS-independent.
    {
        auto now = std::chrono::steady_clock::now();
        bool firstDraw = (m_lastDrawTime.time_since_epoch().count() == 0);
        bool resumed = !firstDraw &&
            std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastDrawTime).count()
                > PluginConstants::ENTER_TRACK_GAP_MS;
        if (firstDraw || resumed) {
            if (m_pToggleStrip) m_pToggleStrip->reveal(PluginConstants::WIDGET_REVEAL_MS);
        }
        m_lastDrawTime = now;
        if (firstDraw) announceAllHiddenAtStart();   // m_lastDrawTime never resets: once per session
    }

    auto& bm = PluginData::getInstance().getBenchmarkMetrics();
    if (bm.active) { bm.frameHeadTimeUs += DrawHandler::getCurrentTimeUs() - frameStart; ++bm.frameTimerSamples; }

    // Update input data once per frame at the beginning, then the hotkey manager
    // (checks for triggered actions). Timed together with updateHuds()' own poll
    // block as framePollTimeUs -- see BenchmarkMetrics for why Draw is attributed.
    {
        // ZEROED HERE, then accumulated by both poll sites this frame -- the
        // collect/update timers are single-site and can just assign.
        const long long pollStart = bm.active ? DrawHandler::getCurrentTimeUs() : 0;
        InputManager::getInstance().updateFrame();
        HotkeyManager::getInstance().update();
        SettingsManager::getInstance().pollSaveResult();   // a writer-thread save's outcome
        if (bm.active) bm.framePollTimeUs += DrawHandler::getCurrentTimeUs() - pollStart;
    }

    // Update all HUDs (they will only rebuild if marked dirty)
    // Note: No per-frame callback reset here. Callbacks accumulate across
    // the entire snapshot interval. BenchmarkWidget::takeSnapshot() resets them.
    if (bm.active) {
        const long long updStart = DrawHandler::getCurrentTimeUs();
        updateHuds();
        bm.updateHudsTimeUs += DrawHandler::getCurrentTimeUs() - updStart;
    } else {
        updateHuds();
    }

    // Collect render data from all HUDs
    // Note: PointerWidget is registered last, so pointer renders on top
    if (bm.active) {
        long long collectStart = DrawHandler::getCurrentTimeUs();
        collectRenderData();
        bm.collectRenderTimeUs += DrawHandler::getCurrentTimeUs() - collectStart;
    } else {
        collectRenderData();
    }

    const long long tailStart = bm.active ? DrawHandler::getCurrentTimeUs() : 0;

    // The automatic sweep, if one is running: it steps the probe settings below
    // through the whole matrix on its own schedule. Ticked HERE, immediately before
    // the emission that reads those settings, so a step's configuration is the one
    // its own frames are drawn with -- ticking after would attribute every frame to
    // the previous step's settings for one frame at each boundary.
    RenderProbeSweep::getInstance().tick();

    // The sweep's own status line, drawn as a bare string rather than through a HUD.
    // A HUD would be the wrong vehicle twice over: the sweep is meant to be run with
    // every panel switched OFF (so the probe is nearly the whole load), and it must
    // stay visible under exactly that condition. One string, no panel, no rebuild.
    //
    // It is also the answer to the failure that prompted the sweep: five hand-driven
    // runs produced perfect reports of an experiment that never happened. A sweep you
    // can watch cannot fail that way in silence.
    if (RenderProbeSweep::getInstance().isRunning()) {
        const std::string& status = RenderProbeSweep::getInstance().statusLine();
        if (!status.empty()) {
            SPluginString_t st{};
            strncpy_s(st.m_szString, sizeof(st.m_szString), status.c_str(), _TRUNCATE);
            st.m_afPos[0] = 0.5f; st.m_afPos[1] = 0.04f;
            st.m_iFont = 1;                 // 1-based; font 1 is always registered
            st.m_fSize = 0.03f;
            st.m_iJustify = 1;              // centred
            st.m_ulColor = 0xFF00FFFFul;    // ABGR: yellow, so it reads as instrumentation
            m_strings.push_back(st);
        }
    }

    // Render-load probe ([Advanced] renderProbeQuads, off by default): append N
    // synthetic quads for the ENGINE to draw. The engine's cost to render our
    // primitives happens after Draw() returns and no in-plugin timer can see it — but
    // it IS measurable differentially: sweep N (uncapped FPS, fixed scene) and the
    // frame-time slope is the engine's per-primitive cost. Appended to the GAME frame
    // only, OUTSIDE collectRenderTime (synthetic engine load, not our build cost); the
    // count flows into bm.totalQuads via *piNumQuads, so the benchmark logs (N, fps).
    {
        int probeN = UiConfig::getInstance().getRenderProbeQuads();
        if (probeN > 0) {   // draw() still zeroes the handoff in COMPANION-suppressed frames
            // renderProbeType selects WHICH primitive to stress, since the three the
            // plugin emits have different engine costs: 0=solid-fill quad (cheapest —
            // just fill), 1=sprite quad (textured: adds a texture fetch, and we cycle
            // across ALL registered sprites so it also exercises texture-switch /
            // batch-break cost), 2=text string (glyph-atlas sampling, the text path).
            const int type = UiConfig::getInstance().getRenderProbeType();
            if (type == 2) {
                // Text: N identical short strings (overlap is fine — each is still
                // rasterized). Count flows into bm.totalStrings via *piNumString.
                SPluginString_t st{};
                // Built to renderProbeTextChars, not a fixed literal: the engine bills
                // per GLYPH, so a per-string cost means nothing without the length it
                // was measured at. A hardcoded 15 here is what overstated drop shadow
                // by 1.7x -- the plugin's own strings average about nine.
                const int chars = UiConfig::getInstance().getRenderProbeTextChars();
                char text[UiConfig::MAX_PROBE_TEXT_CHARS + 1];
                for (int i = 0; i < chars; ++i) text[i] = "0123456789:.-"[i % 13];
                text[chars] = '\0';
                strncpy_s(st.m_szString, sizeof(st.m_szString), text, _TRUNCATE);
                st.m_afPos[0] = 0.5f; st.m_afPos[1] = 0.5f;
                st.m_iFont = 1;              // 1-based; font 1 is always registered
                st.m_fSize = 0.02f; st.m_iJustify = 0; st.m_ulColor = 0xFFFFFFFFul;
                m_strings.insert(m_strings.end(), static_cast<size_t>(probeN), st);
            } else {
                SPluginQuad_t q{};
                // ABGR; icons are tinted to this, textures ignore it. Alpha is
                // configurable so alpha-0 can be measured against alpha-64 -- see
                // UiConfig::getRenderProbeAlpha for the question that answers.
                q.m_ulColor = 0x00FFFFFFul |
                    (static_cast<unsigned long>(UiConfig::getInstance().getRenderProbeAlpha()) << 24);
                // Fullscreen applies to SPRITE quads too, not just fill. It used to be
                // type-0-only, which left the one case a themed panel actually is
                // unmeasurable: a nine-slice's centre is a large STRETCHED TEXTURED
                // quad, so "textured fill" is the combination that matters and was the
                // combination the probe could not produce. Text (type 2) has no rect
                // to grow, so it is still excluded.
                const bool fs = (type != 2) && UiConfig::getInstance().getRenderProbeFullscreen();
                if (fs) {
                    q.m_aafPos[0][0]=0.f; q.m_aafPos[0][1]=0.f; q.m_aafPos[1][0]=0.f; q.m_aafPos[1][1]=1.f;
                    q.m_aafPos[2][0]=1.f; q.m_aafPos[2][1]=1.f; q.m_aafPos[3][0]=1.f; q.m_aafPos[3][1]=0.f;
                } else if (UiConfig::getInstance().getRenderProbeTextChars() == 1) {
                    // DEGENERATE (zero-area), selected by the otherwise-meaningless
                    // textChars=1 on a quad type rather than by adding a sixth knob.
                    //
                    // The question it answers is not academic: finalizeThemedFill
                    // reserves fill strips and DEGENERATES the unused ones in place
                    // rather than removing them, so every themed panel submits several
                    // zero-area quads every frame. Alpha 0 turned out to be charged in
                    // full, which says the cost is submission rather than rasterisation
                    // -- but a zero-area quad can be rejected earlier than a
                    // zero-alpha one, and "probably the same" is exactly the kind of
                    // reasoning this whole instrument exists to replace.
                    for (int c = 0; c < 4; ++c) { q.m_aafPos[c][0] = 0.5f; q.m_aafPos[c][1] = 0.5f; }
                } else {
                    // Tiny quad: negligible fill, so this isolates submit / texture-sample cost.
                    q.m_aafPos[0][0]=0.f; q.m_aafPos[0][1]=0.f; q.m_aafPos[1][0]=0.f; q.m_aafPos[1][1]=0.01f;
                    q.m_aafPos[2][0]=0.01f; q.m_aafPos[2][1]=0.01f; q.m_aafPos[3][0]=0.01f; q.m_aafPos[3][1]=0.f;
                }
                const int spriteCount = (type == 1) ? static_cast<int>(m_spriteNames.size()) : 0;
                // renderProbeSprite pins ONE sprite for the whole frame instead of
                // cycling. Cycling measures "textured AND a texture switch per quad",
                // which is the worst case and not the one a themed panel is -- a panel
                // draws its 27 quads from ~9 sprites. Pinning gives texture sampling
                // with no switching; the two runs differ by exactly the switch cost.
                // An out-of-range pin falls back to cycling rather than to sprite 0,
                // because sprite 0 is the UNTEXTURED path: a typo would otherwise
                // produce a run that measured flat fill while the report said sprite.
                const int pin = UiConfig::getInstance().getRenderProbeSprite();
                const bool pinned = (type == 1 && pin > 0 && pin <= spriteCount);
                if (pinned) {
                    q.m_iSprite = pin;
                    m_quads.insert(m_quads.end(), static_cast<size_t>(probeN), q);
                } else if (type == 1 && spriteCount > 0) {
                    // Cycle across every registered sprite to induce texture switches.
                    for (int i = 0; i < probeN; ++i) {
                        q.m_iSprite = 1 + (i % spriteCount);   // 1-based sprite index
                        m_quads.push_back(q);
                    }
                } else {
                    q.m_iSprite = 0;   // 0 = solid fill (fill mode, or sprite mode with no sprites)
                    m_quads.insert(m_quads.end(), static_cast<size_t>(probeN), q);
                }
            }
        }
    }

    // Route this frame to the game and/or the standalone companion window per the
    // display target. The companion always gets the full frame; the in-game HUD is
    // suppressed in COMPANION mode — except while the settings menu is open, so the
    // user can always reopen settings (via the settings hotkey) and switch back.
    DisplayTarget target = UiConfig::getInstance().getDisplayTarget();

    // Feed the companion window whenever it's open (target drives whether it's open;
    // a test hook can also open it directly).
    CompanionWindow& companion = CompanionWindow::getInstance();
    // If the user closed the window with its X button, fall back to In-game so the
    // HUD reappears in the game (COMPANION mode otherwise suppresses it, leaving no
    // HUD anywhere) and persist it so it stays In-game next launch.
    if (companion.consumeUserClosed() && target != DisplayTarget::IN_GAME) {
        UiConfig::getInstance().setDisplayTarget(DisplayTarget::IN_GAME);
        SettingsManager::getInstance().markDirty();
        target = DisplayTarget::IN_GAME;
    }
    if (companion.isEnabled()) {
        // The companion gets its OWN frame (per-HUD companion on/off + position),
        // built by collectRenderData when the window is open.
        companion.submit(m_companionQuads, m_companionStrings, m_fontNames, m_spriteNames,
                         AssetManager::getInstance().getFirstIconSpriteIndex(),
                         m_companionLayers.view());
    }

    // COMPANION mode suppresses the in-game HUD — EXCEPT while the settings menu is
    // showing IN-GAME, so the user can always reopen settings and switch back. The
    // menu renders only on the active surface (see collectSurface), so gate on the
    // menu being on the GAME surface; otherwise opening settings on the companion
    // would flash the game HUDs onto the in-game screen with no menu on them.
    bool activeCompanion =
        InputManager::getInstance().getActiveSurface() == InputManager::Surface::Companion;
    bool settingsOnGame = m_pSettingsHud && m_pSettingsHud->isVisible() && !activeCompanion;

    // Suppression of the engine handoff has ONE source here now: the display
    // target. The in-context GL renderer's own suppression is decided later, on
    // the Draw thread, and gated on whether it actually drew - see
    // renderInContextGl. The overlay window used to add a third arm; it was
    // removed once Direct GL Rendering superseded it (see the plan for why the
    // second-window approach was the wrong shape).
    m_bSuppressInGame = (target == DisplayTarget::COMPANION && !settingsOnGame);

    // The Direct GL Rendering status row reports live state (Off / Failed / On),
    // but the settings panel only rebuilds when it is DIRTY - so without this the
    // row sat on whatever it said when it was last built and only caught up when
    // the user happened to move the mouse. Reported from the field as "On does
    // not go green until I wiggle the mouse".
    //
    // The comparison lives HERE, on the build thread, and deliberately not at the
    // point the state changes: that is the Draw thread, and m_bDataDirty is a
    // plain bool that produceFrame reads on the worker in pluginThread mode.
    // Setting it from Draw would be a data race for a cosmetic refresh. The same
    // compare rebuilds the settings dropdown when surfaceStacks() flips.
    if (m_pSettingsHud) {
        const int glStatus = glStatusCode();
        if (glStatus != m_glStatusLastShown) {
            m_glStatusLastShown = glStatus;
            m_pSettingsHud->setDataDirty();
        }
    }
    if (bm.active) bm.frameTailTimeUs += DrawHandler::getCurrentTimeUs() - tailStart;
}

void HudManager::updateHuds() {
    // When focus moves between the game and companion windows, refresh the settings
    // menu so its per-HUD checkboxes reflect the now-active surface's instance.
    bool activeCompanion = InputManager::getInstance().getActiveSurface() == InputManager::Surface::Companion;
    if (activeCompanion != m_lastActiveCompanion) {
        m_lastActiveCompanion = activeCompanion;
        if (m_pSettingsHud && m_pSettingsHud->isVisible()) m_pSettingsHud->setDataDirty();
    }

    // The poll block below is FRAME work, not HUD work: it is attributed to
    // framePollTimeUs so updateHudsTimeUs reads as the HUDs' own cost.
    auto& bmPoll = PluginData::getInstance().getBenchmarkMetrics();
    const long long pollStart = bmPoll.active ? DrawHandler::getCurrentTimeUs() : 0;

    // Toggles strip click: open the menu, switch the director or a HUD
    handleToggleStrip();

    // Per-frame director manual-control poll (gamepad takeover + auto-resume). Runs
    // here, not only on data callbacks, so it works in lulls / solo / replays. Gates
    // internally (disabled / not spectating / throttled).
    DirectorManager::getInstance().pollManualControl();
    // Per-frame pacing pump so the min/max-shot cadence is enforced on wall-clock time
    // even when timing-data callbacks go quiet (stable formation) - otherwise the
    // current shot overruns the max-shot cap. Coalesced to ~3x/sec inside evaluate().
    DirectorManager::getInstance().pollPacing();

    // Handle keyboard shortcuts for HUD toggles
    processKeyboardInput();

    if (bmPoll.active) bmPoll.framePollTimeUs += DrawHandler::getCurrentTimeUs() - pollStart;

    // Only allow one HUD to be dragged at a time
    // Process HUDs in reverse order (last registered = top layer, gets priority)

    // Drag targets the surface the user is on: on the companion, a HUD sits at its
    // companion offset and uses its companion visibility, so the pick test and the
    // draggable gate must both use those — otherwise a HUD moved on the companion can
    // only be re-grabbed at its GAME position (and one hidden in-game but shown on the
    // companion can't be grabbed at all).
    bool dragCompanion =
        InputManager::getInstance().getActiveSurface() == InputManager::Surface::Companion;

    const long long dragSearchStart = bmPoll.active ? DrawHandler::getCurrentTimeUs() : 0;

    // Use cached dragging HUD if valid, otherwise find new target
    BaseHud* inputTarget = nullptr;

    // Check if cached dragging HUD is still valid and dragging
    if (m_pDraggingHud && m_pDraggingHud->isDragging()) {
        inputTarget = m_pDraggingHud;
    }
    else {
        // Clear cached pointer if drag ended
        m_pDraggingHud = nullptr;

        // Find new target on click (not every frame)
        const InputManager& input = InputManager::getInstance();
        const MouseButton& leftButton = input.getLeftButton();
        const MouseButton& rightButton = input.getRightButton();

        // Check for either LMB (for clicks) or RMB (for dragging)
        if ((leftButton.isClicked() || rightButton.isClicked()) && input.isCursorEnabled()) {
            const CursorPosition& cursor = input.getCursorPosition();
            if (cursor.isValid) {
                // Top first, in the order last frame drew (the stacking order).
                const bool ordered = m_drawOrder.size() == m_huds.size();
                for (size_t k = m_huds.size(); k-- > 0;) {
                    auto& hud = m_huds[ordered ? m_drawOrder[k] : k];
                    if (!hud || !hud->isDraggable() || isHeldBack(hud.get())) continue;
                    bool visibleHere = dragCompanion ? hud->getCompanionVisible() : hud->isVisible();
                    if (!visibleHere) continue;
                    // Hit-test at THIS surface's offset (companion HUDs sit at their
                    // companion position). Avoids calling handleMouseInput twice.
                    float offX = dragCompanion ? hud->getCompanionOffsetX() : hud->getOffsetX();
                    float offY = dragCompanion ? hud->getCompanionOffsetY() : hud->getOffsetY();
                    if (hud->isPointInBoundsAt(cursor.x, cursor.y, offX, offY)) {
                        inputTarget = hud.get();
                        break;
                    }
                }
            }
        }
    }

    // POSITIONING PREVIEW, resolved once per frame: the HUD whose settings tab is
    // open draws placeholder content so it can be dragged into place even when it
    // would normally be empty. One pointer compare per HUD and a single visibility
    // read; setPreviewing() is a no-op unless the answer changed, so a frame with
    // the menu shut costs the walk and nothing else. Here rather than in the panel
    // because the panel has four ways to change the answer (open, close, tab click,
    // the tab's own visibility checkbox) and this has one.
    setPreviewHud(m_pSettingsHud && m_pSettingsHud->isVisible()
                      ? m_pSettingsHud->activeTabHud() : nullptr,
                  m_pToggleStrip && !isHeldBack(m_pToggleStrip) ? m_pToggleStrip->hoverSample() : nullptr);

    // THE NOTICE UP RIGHT NOW, stepped once here, before any HUD reads it: the
    // Notices HUD and the Dash both show NoticeTracker::current(), and its timed
    // notices CONSUME their PluginData flag, so two panels stepping their own copy
    // would race for it. Steps fully while either panel is shown; otherwise only
    // drains the expired flags.
    if (m_pNotices) {
        const bool watched = m_pNotices->isVisibleAnySurface() ||
                             (m_pDash && m_pDash->watchesNotices());
        NoticeTracker::getInstance().step(m_pNotices->getEnabledNotices(),
                                          m_pNotices->getNoticeDurationMs(), watched);
    }

    // THE STARTUP MESSAGES all point at the settings menu, so it opening answers
    // them, whether or not the Dash that shows them is on, and opens on the tab
    // the one showing names. One load and compare per frame.
    SystemMessages& messages = SystemMessages::getInstance();
    const SystemMessages::Startup owed = messages.pendingStartup();
    if (owed != SystemMessages::Startup::None && m_pSettingsHud &&
        m_pSettingsHud->isVisible()) {  // vis-gate: the menu's own state
        if (owed == SystemMessages::Startup::Update) m_pSettingsHud->showUpdatesTab();
        else if (owed == SystemMessages::Startup::Updated) m_pSettingsHud->showWhatsNew();
        messages.onSettingsOpened();
        // What was told persists with the next save (leave-track or Save).
        SettingsManager::getInstance().markDirty();
    }

    // THE GLOW IS BAKED INTO EACH HUD'S QUADS (BaseHud::addGlow), so switching it
    // or retuning it rebuilds every HUD this frame: one that rebuilds only on its
    // own data change (Notices, mid-warning) would otherwise keep the old halo.
    {
        const UiConfig& ui = UiConfig::getInstance();
        const bool glow = BaseHud::glowActive();
        if (glow != m_lastGlowActive || ui.getGlowIntensity() != m_lastGlowIntensity ||
            ui.getGlowWidth() != m_lastGlowWidth) {
            m_lastGlowActive = glow;
            m_lastGlowIntensity = ui.getGlowIntensity();
            m_lastGlowWidth = ui.getGlowWidth();
            markAllHudsDirty();
        }
    }

    // Now update all HUDs
    if (bmPoll.active) bmPoll.frameHudInputTimeUs += DrawHandler::getCurrentTimeUs() - dragSearchStart;

    for (auto& hud : m_huds) {
        if (hud && !standsDown(hud.get())) {
            // Allow mouse input only for the target HUD (and only if visible)
            bool allowInput = (hud.get() == inputTarget);

            // Keep a HUD that's already mid-drag flowing even if the active surface
            // momentarily flips (cursor slips off the window), so the drag isn't cut.
            bool visibleHere = dragCompanion ? hud->getCompanionVisible() : hud->isVisible();
            if (hud->isDraggable() && (visibleHere || hud->isDragging())) {
                if (bmPoll.active) {
                    const long long miStart = DrawHandler::getCurrentTimeUs();
                    hud->handleMouseInput(allowInput);
                    bmPoll.frameHudInputTimeUs += DrawHandler::getCurrentTimeUs() - miStart;
                } else {
                    hud->handleMouseInput(allowInput);
                }

                // Cache the HUD if it just started dragging, and put it on top.
                if (hud->isDragging() && !m_pDraggingHud) {
                    m_pDraggingHud = hud.get();
                    raiseHud(hud.get());
                }
            }

            // Always call update() to handle data/layout dirty flags
            // Per-HUD update timing: `update - rebuild` is what a HUD costs on the
            // frames it is NOT dirty, and the aggregate could not say which HUD.
            if (bmPoll.active && hud->getBenchmarkIndex() >= 0) {
                const long long uStart = DrawHandler::getCurrentTimeUs();
                hud->update();
                bmPoll.recordHudUpdate(hud->getBenchmarkIndex(),
                                       DrawHandler::getCurrentTimeUs() - uStart);
            } else {
                hud->update();
            }
        }
    }

    // Deferred position validation, run HERE because here is the one place it is
    // safe: every HUD's update() has returned, so nothing below us is an input
    // handler. validatePosition() calls update() on a dirty HUD, so doing this from
    // inside a handler re-enters the update that dispatched it -- and the click edge
    // stays true for the rest of the frame, so it dispatches the same button again.
    // The theme-cycle button did exactly that and recursed ~1400 deep into a stack
    // overflow. Enforced by check_hud_helpers.sh rule 9: HUD code REQUESTS.
    if (m_validationPending) {
        m_validationPending = false;
        validateAllHudPositions();
    }
}

void HudManager::collectRenderData() {
    // Game surface: byte-identical to before. Then, only when the companion window
    // is open, build its frame from each HUD's companion instance (own on/off +
    // position; mirrors the game until diverged).
    // HUD stacking only for a renderer that can use it: the engine draws every
    // quad before every string whatever we hand it (core/render_layers.h).
    buildDrawOrder();
    const bool glLayers = surfaceStacks(/*companion=*/false);
    if (!glLayers) m_gameLayers.clear();
    collectSurface(m_quads, m_strings, /*companion=*/false, glLayers ? &m_gameLayers : nullptr);
    if (UiConfig::getInstance().getGlInGame()) publishGlWarmList();
    if (CompanionWindow::getInstance().isEnabled()) {
        // Decouple from the start: the first frame the companion is on, snapshot each
        // HUD's game state into its companion instance so the two are independent
        // immediately, rather than the companion mirroring the game until the user
        // edits each HUD. No-op once a HUD is already configured.
        for (auto& hud : m_huds)
            if (hud) hud->snapshotCompanionFromGame();
        collectSurface(m_companionQuads, m_companionStrings, /*companion=*/true,
                       &m_companionLayers);
    } else if (!m_motionCompanion.slots.empty()) {
        // Its last frame is stale by the time the window reopens: start it settled.
        m_motionCompanion = MotionSurface{};
    }
}

// Build one surface's frame. companion=false reproduces the original game frame
// exactly; companion=true filters by each HUD's companion visibility and, after
// copying a HUD's primitives (reusing all the shadow logic), translates that HUD's
// appended range by its (companion - game) offset delta.
void HudManager::collectSurface(std::vector<SPluginQuad_t>& outQuads,
                                std::vector<SPluginString_t>& outStrings,
                                bool companion, SurfaceLayers* outLayers) {

    // Get drop shadow settings once (avoid repeated singleton calls)
    const UiConfig& uiConfig = UiConfig::getInstance();
    bool dropShadowEnabled = uiConfig.getDropShadow();
    float shadowOffsetXPct = uiConfig.getDropShadowOffsetX();
    float shadowOffsetYPct = uiConfig.getDropShadowOffsetY();
    unsigned long shadowColor = uiConfig.getDropShadowColor();

    // The interactive chrome — the mouse pointer and the OPEN settings menu — belongs
    // to the surface the user is actually on, not both. Otherwise the companion
    // mirrors the game's pointer/menu and the user sees a cursor and a settings menu
    // in both windows. The Toggles strip stays on every surface so settings can be
    // opened from either window. In single-window mode the active surface is Game, so
    // this leaves the game frame unchanged.
    bool activeCompanion =
        InputManager::getInstance().getActiveSurface() == InputManager::Surface::Companion;
    bool surfaceIsActive = (companion == activeCompanion);

    // Motion (hud_manager_motion.cpp): step every HUD and keep the ghost of any
    // that just stopped drawing, read from last frame's output - so BEFORE that
    // is cleared below. Null with Motion off, and then nothing here changes.
    MotionSurface* motion = motionBeginFrame(companion, surfaceIsActive, outQuads, outStrings);

    // Calculate total capacity needed to minimize allocations
    size_t totalQuads = 0;
    size_t totalStrings = 0;

    for (const auto& hud : m_huds) {
        if (hud) {
            totalQuads += hud->getQuads().size();
            totalStrings += hud->getStrings().size();
        }
    }
    // A HUD fading out draws its ghost.
    if (motion) {
        for (const MotionSlot& slot : motion->slots) {
            if (!slot.ghostLive) continue;
            totalQuads += slot.ghostQuads.size();
            totalStrings += slot.ghostStrings.size();
        }
    }

    // Benchmark: record each HUD's per-frame primitive footprint (game pass only —
    // the companion pass would double-count). Cheap, gated on the profiler being
    // active. A hidden HUD emits nothing this frame. Base counts (pre-shadow);
    // drop shadow ~doubles the string total globally.
    {
        auto& bm = PluginData::getInstance().getBenchmarkMetrics();
        if (!companion && bm.active) {
            for (const auto& hud : m_huds) {
                if (!hud) continue;
                int idx = hud->getBenchmarkIndex();
                if (idx < 0) continue;
                bool vis = hud->isVisible();
                bm.recordHudPrimitives(idx, vis ? static_cast<int>(hud->getQuads().size()) : 0,
                                            vis ? static_cast<int>(hud->getStrings().size()) : 0);
            }
        }
    }

    // Small fixed headroom (always allocated); the pointer and the Toggles strip
    // are HUDs, so their primitives are already counted above
    totalQuads += 2;
    totalStrings += 1;

    // Grid overlay (debug, INI-only) appends the snap lattice on top — reserve for it too.
    if (UiConfig::getInstance().getGridOverlay()) {
        totalQuads += gridOverlayQuadCount();
    }

    // If drop shadow is enabled, we may need up to 2x the strings
    if (dropShadowEnabled) {
        totalStrings *= 2;
    }
    // Note: this intentionally ignores two minor over-counts of shadow copies - the
    // one shadow quad per shadowed icon (title icon, icons beside text), and strings when a per-HUD dropShadow=1
    // override is on while the global is off. Both are bounded and only cost at most one
    // extra realloc (capacity never shrinks), not worth a pre-pass over every HUD.

    // Ensure vectors have sufficient capacity - grow if needed but never shrink
    if (outQuads.capacity() < totalQuads) {
        size_t newCapacity = totalQuads * CAPACITY_GROWTH_FACTOR;
        outQuads.reserve(newCapacity);
        DEBUG_INFO_F("HudManager quads capacity increased to %zu", newCapacity);
    }

    if (outStrings.capacity() < totalStrings) {
        size_t newCapacity = totalStrings * CAPACITY_GROWTH_FACTOR;
        outStrings.reserve(newCapacity);
        DEBUG_INFO_F("HudManager strings capacity increased to %zu", newCapacity);
    }

    // Clear existing data but keep allocated memory
    outQuads.resize(0);
    outStrings.resize(0);
    if (outLayers) outLayers->clear();

    // Collect from all visible HUDs using efficient vector operations
    // Settings and the Toggles strip are always rendered (even when toggle key pressed)
    // In stacking order (hud_manager_stack.cpp); slots and the rest stay keyed
    // on the registration index.
    const bool ordered = m_drawOrder.size() == m_huds.size();
    for (size_t pos = 0; pos < m_huds.size(); ++pos) {
        const size_t hudIndex = ordered ? m_drawOrder[pos] : pos;
        const auto& hud = m_huds[hudIndex];
        // A HUD fading out draws its ghost in its own place in the order.
        MotionSlot* motionSlot = motion ? &motion->slots[hudIndex] : nullptr;
        if (motionSlot && motionSlot->ghostLive) {
            const size_t ghostQuad = outQuads.size(), ghostString = outStrings.size();
            motionEmitGhost(*motionSlot, outQuads, outStrings);
            // A fading ghost is not at its HUD's bounds: it stacks against everything.
            if (outLayers) recordLayerSpan(*outLayers, outQuads, ghostQuad, outStrings, ghostString,
                                           nullptr, companion, true);
        }
        // Guard the deref: m_huds provably holds no nulls (registerHud filters
        // them), but this file is written defensively, so don't dereference before
        // the null check. drawsOnSurface is false for a null hud.
        if (drawsOnSurface(hud.get(), companion, surfaceIsActive)) {
            // Where this HUD's primitives start, so we can translate them to the
            // companion position afterward (delta is 0 for the game / a mirrored HUD).
            size_t quadStart = outQuads.size();
            size_t stringStart = outStrings.size();
            float deltaX = companion ? (hud->getCompanionOffsetX() - hud->getOffsetX()) : 0.0f;
            float deltaY = companion ? (hud->getCompanionOffsetY() - hud->getOffsetY()) : 0.0f;

            // A HUD that rebuilt DIRECTLY (a widget setter, the settings panel's
            // click paths) rather than through processDirtyFlags arrives here with
            // its themed fill still ARMED: the centre slice covering the whole
            // interior, every card stacked on it at double opacity. Cut it now,
            // before the quads are read. Consume-once, so it is a no-op for the
            // dirty-pipeline rebuilds that already finalized -- one int compare.
            hud->finalizeThemedFill();

            const auto& hudQuads = hud->getQuads();
            const auto& hudStrings = hud->getStrings();
            const auto& skipShadowFlags = hud->getStringSkipShadow();

            // Per-HUD drop shadow: the global setting unless this HUD has an ini-only override.
            bool hudShadow = hud->getEffectiveDropShadow(dropShadowEnabled);

            // Quads: bulk copy, with a shadow copy in front of the title icon and each
            // icon the HUD marked beside its text (appendHudQuads). Mirror the title
            // string's own shadow decision so the icon and the title beside it agree.
            int titleIconIdx = hud->m_titleIconQuadIndex;
            int titleStrIdx = hud->m_titleStringIndex;
            bool titleStrSkips = titleStrIdx >= 0 &&
                                 titleStrIdx < static_cast<int>(skipShadowFlags.size()) &&
                                 skipShadowFlags[titleStrIdx];
            bool shadowTitleIcon = hudShadow && !titleStrSkips && titleIconIdx >= 0 &&
                                   titleIconIdx < static_cast<int>(hudQuads.size());
            appendHudQuads(*hud, outQuads, shadowTitleIcon, hudShadow);

            // For strings: if drop shadow enabled, add shadow before each non-skipped string
            if (hudShadow) {
                for (size_t i = 0; i < hudStrings.size(); ++i) {
                    const auto& str = hudStrings[i];
                    bool skipShadow = (i < skipShadowFlags.size()) ? skipShadowFlags[i] : false;

                    if (!skipShadow) {
                        // Add shadow string first (so it renders behind)
                        SPluginString_t shadowStr = str;
                        // Offset proportional to font size, capped at EXTRA_LARGE to avoid exaggerated shadows on oversized fonts
                        float shadowSize = std::min(str.m_fSize, layoutDefaults().fontSizeExtraLarge);
                        shadowStr.m_afPos[0] += shadowSize * shadowOffsetXPct;
                        shadowStr.m_afPos[1] += shadowSize * shadowOffsetYPct;
                        // MODULATED BY THE STRING'S OWN ALPHA, not written verbatim: a
                        // HUD that fades a string (the radar fades a rider label out as
                        // the rider leaves proximity range) would otherwise get a solid
                        // shadow behind half-visible text. A no-op for opaque strings,
                        // which is nearly all of them.
                        shadowStr.m_ulColor =
                            PluginUtils::modulateAlpha(shadowColor, str.m_ulColor);
                        outStrings.push_back(shadowStr);
                    }

                    // Add original string
                    outStrings.push_back(str);
                }
            } else {
                // No drop shadow - use efficient bulk copy
                outStrings.insert(outStrings.end(), hudStrings.begin(), hudStrings.end());
            }
            if (hud->isSampled()) markSample(outQuads, quadStart, outStrings, stringStart);

            // Companion surface: shift this HUD's just-appended primitives to its
            // companion position (delta is 0 for the game, or a mirrored HUD).
            if (deltaX != 0.0f || deltaY != 0.0f) {
                for (size_t k = quadStart; k < outQuads.size(); ++k)
                    for (int c = 0; c < 4; ++c) { outQuads[k].m_aafPos[c][0] += deltaX; outQuads[k].m_aafPos[c][1] += deltaY; }
                for (size_t k = stringStart; k < outStrings.size(); ++k) {
                    outStrings[k].m_afPos[0] += deltaX; outStrings[k].m_afPos[1] += deltaY;
                }
            }

            if (motionSlot)
                motionFinishHud(*motionSlot, *hud, outQuads, quadStart, outStrings, stringStart,
                                &m_iconShifts, hudShadow);
            // Mid-slide or mid-fade, the HUD is not where its bounds say.
            if (outLayers)
                recordLayerSpan(*outLayers, outQuads, quadStart, outStrings, stringStart, hud.get(),
                                companion, motionSlot && motionSlot->frame.moving(),
                                &m_iconShifts, hudShadow);
        }
    }

    // Debug: draw the snap grid ON TOP of everything (INI-only, off by default), so HUD
    // edges can be checked against the lattice they snap to. Same on both surfaces.
    if (UiConfig::getInstance().getGridOverlay()) {
        const size_t gridQuad = outQuads.size();
        appendGridOverlay(outQuads);
        if (outLayers) recordLayerSpan(*outLayers, outQuads, gridQuad, outStrings, outStrings.size(),
                                       nullptr, companion, true);
    }
    if (outLayers) finishLayers(*outLayers);
}

bool HudManager::requiredIconsPresent() {
    // The icons buttons, carets and the Dash/Toggles strips draw: a stale or
    // partial icons folder lacking one gets the warning, or a chip draws empty.
    static const char* const kRequiredIcons[] = { "hud-menu", "hud-close", "hud-video", "caret-up", "hud-dash",
                                                  "hud-clock", "hud-fuel", "hud-flatmap", "hud-gauge" };
    for (const char* name : kRequiredIcons)
        if (AssetManager::getInstance().getIconSpriteIndex(name) <= 0) return false;
    return true;
}

void HudManager::buildInstallWarning(size_t fontCount, size_t iconCount, bool iconsComplete,
                                     bool realText) {
    SystemMessages& messages = SystemMessages::getInstance();
    if (fontCount > 0 && iconCount > 0 && iconsComplete) {
        messages.setInstallProblem(nullptr);
        return;
    }
    // Short enough for the Dash's card, at the small font, like its other lines.
    const char* what = fontCount == 0 && iconCount == 0 ? "No fonts or icons in mxbmrp3_data"
                     : fontCount == 0 ? "No fonts in mxbmrp3_data\\fonts"
                     : iconCount == 0 ? "No icons in mxbmrp3_data\\icons"
                                      : "Icons missing in mxbmrp3_data";
    messages.setInstallProblem(what);
    // Block letters unless the fonts are there (or a test compares the two).
    if (m_pDash) m_pDash->setInstallPixelText(fontCount == 0 && !realText);
    DEBUG_WARN_F("HudManager: install looks broken (%zu fonts, %zu icons) - the Dash says so",
                 fontCount, iconCount);
}

// Grid line spacing = the snap lattice itself: vertical lines every cellW across X,
// horizontal lines every cellH across Y, in normalized [0,1] screen space — exactly
// what snapX/snapY quantize to. Kept in one place so the count helper and the
// drawer can't drift, and read from the live metrics so the overlay still shows the
// truth after a theme file moves the type.
namespace {
    // Not constexpr any more -- the grid derives from uiFontSize/uiLineHeight ([Advanced]),
    // so these read it at call time. Functions rather than file-scope constants
    // because a namespace-scope initialiser would run before LayoutConfig::load().
    inline float gridCellW() { return layoutDefaults().cellW; }   // 0.0055 by default
    inline float gridCellH() { return layoutDefaults().cellH; }   // ~0.011734 by default
    // Line thicknesses in normalized units (~1px minor / ~2px major at 1080p).
    constexpr float GRID_MINOR_W = 0.00055f, GRID_MAJOR_W = 0.00110f;  // vertical-line width
    constexpr float GRID_MINOR_H = 0.00095f, GRID_MAJOR_H = 0.00190f;  // horizontal-line height
    inline int gridLineCount(float cell) { return static_cast<int>(1.0f / cell) + 1; }
}

size_t HudManager::gridOverlayQuadCount() {
    return static_cast<size_t>(gridLineCount(gridCellW()) + gridLineCount(gridCellH()));
}

void HudManager::appendGridOverlay(std::vector<SPluginQuad_t>& outQuads) const {
    const UiConfig& ui = UiConfig::getInstance();
    const int majorEvery = ui.getGridOverlayMajorEvery();  // clamped >= 1 in the setter
    const unsigned long minorColor = ui.getGridOverlayColor();
    const unsigned long majorColor = ui.getGridOverlayMajorColor();

    // Append one solid-color rectangle (x,y = top-left, positive w/h).
    auto pushLine = [&outQuads](float x, float y, float w, float h, unsigned long color) {
        SPluginQuad_t q;
        q.m_aafPos[0][0] = x;      q.m_aafPos[0][1] = y;
        q.m_aafPos[1][0] = x;      q.m_aafPos[1][1] = y + h;
        q.m_aafPos[2][0] = x + w;  q.m_aafPos[2][1] = y + h;
        q.m_aafPos[3][0] = x + w;  q.m_aafPos[3][1] = y;
        q.m_iSprite = PluginConstants::SpriteIndex::SOLID_COLOR;
        q.m_ulColor = color;
        outQuads.push_back(q);
    };

    // Vertical lines (index 0 at x=0). Centered on the grid line so major lines don't shift it.
    const int vCount = gridLineCount(gridCellW());
    for (int i = 0; i < vCount; ++i) {
        const bool major = (i % majorEvery) == 0;
        const float w = major ? GRID_MAJOR_W : GRID_MINOR_W;
        pushLine(i * gridCellW() - w * 0.5f, 0.0f, w, 1.0f, major ? majorColor : minorColor);
    }
    // Horizontal lines (index 0 at y=0).
    const int hCount = gridLineCount(gridCellH());
    for (int j = 0; j < hCount; ++j) {
        const bool major = (j % majorEvery) == 0;
        const float h = major ? GRID_MAJOR_H : GRID_MINOR_H;
        pushLine(0.0f, j * gridCellH() - h * 0.5f, 1.0f, h, major ? majorColor : minorColor);
    }
}
