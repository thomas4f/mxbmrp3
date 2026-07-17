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
#include "../hud/version_widget.h"
#include "../hud/notices_hud.h"
#include "../hud/settings_hud.h"
#include "../hud/settings_button_widget.h"
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
#include "../hud/director_widget.h"
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
    // Draw (or the very first Draw) means drawing just resumed, so flash the corner
    // status buttons into view - they otherwise auto-hide with the idle cursor, and this
    // helps users find them without moving the mouse. Time-based so it's FPS-independent.
    {
        auto now = std::chrono::steady_clock::now();
        bool firstDraw = (m_lastDrawTime.time_since_epoch().count() == 0);
        bool resumed = !firstDraw &&
            std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastDrawTime).count()
                > PluginConstants::ENTER_TRACK_GAP_MS;
        if (firstDraw || resumed) {
            if (m_pSettingsButton) m_pSettingsButton->reveal(PluginConstants::WIDGET_REVEAL_MS);
            if (m_pDirector) m_pDirector->reveal(PluginConstants::WIDGET_REVEAL_MS);
        }
        m_lastDrawTime = now;
    }

    // Update input data once per frame at the beginning
    InputManager::getInstance().updateFrame();

    // Update hotkey manager (checks for triggered actions)
    HotkeyManager::getInstance().update();

    auto& bm = PluginData::getInstance().getBenchmarkMetrics();

    // Update all HUDs (they will only rebuild if marked dirty)
    // Note: No per-frame callback reset here. Callbacks accumulate across
    // the entire snapshot interval. BenchmarkWidget::takeSnapshot() resets them.
    updateHuds();

    // Collect render data from all HUDs
    // Note: PointerWidget is registered last, so pointer renders on top
    if (bm.active) {
        long long collectStart = DrawHandler::getCurrentTimeUs();
        collectRenderData();
        bm.collectRenderTimeUs = DrawHandler::getCurrentTimeUs() - collectStart;
    } else {
        collectRenderData();
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
                         AssetManager::getInstance().getFirstIconSpriteIndex());
    }

    // COMPANION mode suppresses the in-game HUD — EXCEPT while the settings menu is
    // showing IN-GAME, so the user can always reopen settings and switch back. The
    // menu renders only on the active surface (see collectSurface), so gate on the
    // menu being on the GAME surface; otherwise opening settings on the companion
    // would flash the game HUDs onto the in-game screen with no menu on them.
    bool activeCompanion =
        InputManager::getInstance().getActiveSurface() == InputManager::Surface::Companion;
    bool settingsOnGame = m_pSettingsHud && m_pSettingsHud->isVisible() && !activeCompanion;
    m_bSuppressInGame = (target == DisplayTarget::COMPANION && !settingsOnGame);
}

void HudManager::updateHuds() {
    // When focus moves between the game and companion windows, refresh the settings
    // menu so its per-HUD checkboxes reflect the now-active surface's instance.
    bool activeCompanion = InputManager::getInstance().getActiveSurface() == InputManager::Surface::Companion;
    if (activeCompanion != m_lastActiveCompanion) {
        m_lastActiveCompanion = activeCompanion;
        if (m_pSettingsHud && m_pSettingsHud->isVisible()) m_pSettingsHud->setDataDirty();
    }

    // Handle settings button click for SettingsHud toggle
    handleSettingsButton();

    // Handle director status-button click (pause/resume auto-direction)
    handleDirectorButton();

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

    // Only allow one HUD to be dragged at a time
    // Process HUDs in reverse order (last registered = top layer, gets priority)

    // Drag targets the surface the user is on: on the companion, a HUD sits at its
    // companion offset and uses its companion visibility, so the pick test and the
    // draggable gate must both use those — otherwise a HUD moved on the companion can
    // only be re-grabbed at its GAME position (and one hidden in-game but shown on the
    // companion can't be grabbed at all).
    bool dragCompanion =
        InputManager::getInstance().getActiveSurface() == InputManager::Surface::Companion;

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
                for (auto it = m_huds.rbegin(); it != m_huds.rend(); ++it) {
                    auto& hud = *it;
                    if (!hud || !hud->isDraggable()) continue;
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

    // Now update all HUDs
    for (auto& hud : m_huds) {
        if (hud) {
            // Allow mouse input only for the target HUD (and only if visible)
            bool allowInput = (hud.get() == inputTarget);

            // Keep a HUD that's already mid-drag flowing even if the active surface
            // momentarily flips (cursor slips off the window), so the drag isn't cut.
            bool visibleHere = dragCompanion ? hud->getCompanionVisible() : hud->isVisible();
            if (hud->isDraggable() && (visibleHere || hud->isDragging())) {
                hud->handleMouseInput(allowInput);

                // Cache the HUD if it just started dragging
                if (hud->isDragging() && !m_pDraggingHud) {
                    m_pDraggingHud = hud.get();
                }
            }

            // Always call update() to handle data/layout dirty flags
            hud->update();
        }
    }
}

void HudManager::collectRenderData() {
    // Game surface: byte-identical to before. Then, only when the companion window
    // is open, build its frame from each HUD's companion instance (own on/off +
    // position; mirrors the game until diverged).
    collectSurface(m_quads, m_strings, /*companion=*/false);
    if (CompanionWindow::getInstance().isEnabled()) {
        // Decouple from the start: the first frame the companion is on, snapshot each
        // HUD's game state into its companion instance so the two are independent
        // immediately, rather than the companion mirroring the game until the user
        // edits each HUD. No-op once a HUD is already configured.
        for (auto& hud : m_huds)
            if (hud) hud->snapshotCompanionFromGame();
        collectSurface(m_companionQuads, m_companionStrings, /*companion=*/true);
    }
}

// Build one surface's frame. companion=false reproduces the original game frame
// exactly; companion=true filters by each HUD's companion visibility and, after
// copying a HUD's primitives (reusing all the shadow logic), translates that HUD's
// appended range by its (companion - game) offset delta.
void HudManager::collectSurface(std::vector<SPluginQuad_t>& outQuads,
                                std::vector<SPluginString_t>& outStrings,
                                bool companion) {

    // Get drop shadow settings once (avoid repeated singleton calls)
    const UiConfig& uiConfig = UiConfig::getInstance();
    bool dropShadowEnabled = uiConfig.getDropShadow();
    float shadowOffsetXPct = uiConfig.getDropShadowOffsetX();
    float shadowOffsetYPct = uiConfig.getDropShadowOffsetY();
    unsigned long shadowColor = uiConfig.getDropShadowColor();

    // Calculate total capacity needed to minimize allocations
    size_t totalQuads = 0;
    size_t totalStrings = 0;

    for (const auto& hud : m_huds) {
        if (hud) {
            totalQuads += hud->getQuads().size();
            totalStrings += hud->getStrings().size();
        }
    }

    // Reserve space for pointer and settings button (always allocated, conditionally added)
    totalQuads += 2;     // Pointer quad + settings button background quad
    totalStrings += 1;   // Settings button text

    // Grid overlay (debug, INI-only) appends the snap lattice on top — reserve for it too.
    if (UiConfig::getInstance().getGridOverlay()) {
        totalQuads += gridOverlayQuadCount();
    }

    // If drop shadow is enabled, we may need up to 2x the strings
    if (dropShadowEnabled) {
        totalStrings *= 2;
    }
    // Note: this intentionally ignores two minor over-counts of shadow copies - the
    // one shadow quad per shadowed title icon, and strings when a per-HUD dropShadow=1
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

    // The interactive chrome — the mouse pointer and the OPEN settings menu — belongs
    // to the surface the user is actually on, not both. Otherwise the companion
    // mirrors the game's pointer/menu and the user sees a cursor and a settings menu
    // in both windows. The settings BUTTON stays on every surface so settings can be
    // opened from either window. In single-window mode the active surface is Game, so
    // this leaves the game frame unchanged.
    bool activeCompanion =
        InputManager::getInstance().getActiveSurface() == InputManager::Surface::Companion;
    bool surfaceIsActive = (companion == activeCompanion);

    // Collect from all visible HUDs using efficient vector operations
    // Settings and settings button are always rendered (even when toggle key pressed)
    for (const auto& hud : m_huds) {
        // Guard the deref: m_huds provably holds no nulls (registerHud filters
        // them), but this file is written defensively, so don't dereference before
        // the null check. visible is false for a null hud, so the body is safe.
        bool visible = hud && (companion ? hud->getCompanionVisible() : hud->isVisible());
        if (visible) {
            // Where this HUD's primitives start, so we can translate them to the
            // companion position afterward (delta is 0 for the game / a mirrored HUD).
            size_t quadStart = outQuads.size();
            size_t stringStart = outStrings.size();
            float deltaX = companion ? (hud->getCompanionOffsetX() - hud->getOffsetX()) : 0.0f;
            float deltaY = companion ? (hud->getCompanionOffsetY() - hud->getOffsetY()) : 0.0f;
            // Check if version widget's easter egg game is active (bypasses all toggles)
            bool isVersionGameActive = (hud.get() == m_pVersion && m_pVersion && m_pVersion->isGameActive());

            // Skip rendering if temporary toggle is active (except settings HUDs, pointer, and active game)
            bool isSettingsHud = (hud.get() == m_pSettingsHud || hud.get() == m_pSettingsButton);
            bool isPointer = (hud.get() == m_pPointer);
            if (m_bAllHudsToggledOff && !isSettingsHud && !isPointer && !isVersionGameActive) {
                continue;
            }

            // Pointer and the open settings MENU render only on the active surface
            // (the settings BUTTON stays on both — it's how you open settings there).
            bool isMenu = (hud.get() == m_pSettingsHud);
            if ((isPointer || isMenu) && !surfaceIsActive) {
                continue;
            }

            // Skip rendering widgets if widget toggle is active.
            // SessionHud is intentionally NOT in this list: it started as a widget but was
            // upgraded to a full HUD (its own settings tab + row config), so it's decoupled
            // from the widgets master toggle and only hides via its own visibility/hotkey.
            bool isWidget = (hud.get() == m_pLap || hud.get() == m_pPosition ||
                           hud.get() == m_pTime ||
                           hud.get() == m_pSpeed || hud.get() == m_pGear ||
                           hud.get() == m_pSpeedo || hud.get() == m_pTacho ||
                           hud.get() == m_pBars || hud.get() == m_pVersion ||
                           hud.get() == m_pFuel ||
                           hud.get() == m_pGamepad || hud.get() == m_pLean ||
                           hud.get() == m_pGforce || hud.get() == m_pCompass ||
                           hud.get() == m_pClock);
            if (m_bAllWidgetsToggledOff && isWidget && !isVersionGameActive) {
                continue;
            }

            const auto& hudQuads = hud->getQuads();
            const auto& hudStrings = hud->getStrings();
            const auto& skipShadowFlags = hud->getStringSkipShadow();

            // Per-HUD drop shadow: the global setting unless this HUD has an ini-only override.
            bool hudShadow = hud->getEffectiveDropShadow(dropShadowEnabled);

            // Quads: bulk copy normally, but drop-shadow the title icon (the per-HUD
            // identity icon to the left of the title) so it matches the title text.
            // Only the title icon is shadowed - in-body/widget icons (settings tabs,
            // the gear, gamepad glyphs, map/radar markers) keep their own outlines and
            // would look wrong with an added shadow.
            int titleIconIdx = hud->m_titleIconQuadIndex;
            // Mirror the title string's own shadow decision so the icon and the title
            // text beside it always agree (today the title string never opts out, but
            // this keeps them in lockstep if addTitleString ever gains a skip flag).
            int titleStrIdx = hud->m_titleStringIndex;
            bool titleStrSkips = titleStrIdx >= 0 &&
                                 titleStrIdx < static_cast<int>(skipShadowFlags.size()) &&
                                 skipShadowFlags[titleStrIdx];
            bool shadowTitleIcon = hudShadow && !titleStrSkips && titleIconIdx >= 0 &&
                                   titleIconIdx < static_cast<int>(hudQuads.size());
            if (shadowTitleIcon) {
                // Bulk-copy the quads before the icon, then a tinted/offset shadow copy
                // (renders behind), then the icon and everything after it - two bulk
                // inserts + one push_back instead of N per-quad copies in this hot path.
                outQuads.insert(outQuads.end(), hudQuads.begin(), hudQuads.begin() + titleIconIdx);

                // Shadow copy. Offset is proportional to the icon's height and capped at
                // EXTRA_LARGE, matching the string formula.
                const auto& iconQuad = hudQuads[titleIconIdx];
                SPluginQuad_t shadowQuad = iconQuad;
                float iconHeight = iconQuad.m_aafPos[1][1] - iconQuad.m_aafPos[0][1];
                float shadowSize = std::min(iconHeight, PluginConstants::FontSizes::EXTRA_LARGE);
                float dx = shadowSize * shadowOffsetXPct;
                float dy = shadowSize * shadowOffsetYPct;
                for (int c = 0; c < 4; ++c) {
                    shadowQuad.m_aafPos[c][0] += dx;
                    shadowQuad.m_aafPos[c][1] += dy;
                }
                shadowQuad.m_ulColor = shadowColor;
                outQuads.push_back(shadowQuad);

                outQuads.insert(outQuads.end(), hudQuads.begin() + titleIconIdx, hudQuads.end());
            } else {
                // No drop shadow - use efficient bulk copy
                outQuads.insert(outQuads.end(), hudQuads.begin(), hudQuads.end());
            }

            // For strings: if drop shadow enabled, add shadow before each non-skipped string
            if (hudShadow) {
                for (size_t i = 0; i < hudStrings.size(); ++i) {
                    const auto& str = hudStrings[i];
                    bool skipShadow = (i < skipShadowFlags.size()) ? skipShadowFlags[i] : false;

                    if (!skipShadow) {
                        // Add shadow string first (so it renders behind)
                        SPluginString_t shadowStr = str;
                        // Offset proportional to font size, capped at EXTRA_LARGE to avoid exaggerated shadows on oversized fonts
                        float shadowSize = std::min(str.m_fSize, PluginConstants::FontSizes::EXTRA_LARGE);
                        shadowStr.m_afPos[0] += shadowSize * shadowOffsetXPct;
                        shadowStr.m_afPos[1] += shadowSize * shadowOffsetYPct;
                        shadowStr.m_ulColor = shadowColor;
                        outStrings.push_back(shadowStr);
                    }

                    // Add original string
                    outStrings.push_back(str);
                }
            } else {
                // No drop shadow - use efficient bulk copy
                outStrings.insert(outStrings.end(), hudStrings.begin(), hudStrings.end());
            }

            // Companion surface: shift this HUD's just-appended primitives to its
            // companion position (delta is 0 for the game, or a mirrored HUD).
            if (deltaX != 0.0f || deltaY != 0.0f) {
                for (size_t k = quadStart; k < outQuads.size(); ++k)
                    for (int c = 0; c < 4; ++c) { outQuads[k].m_aafPos[c][0] += deltaX; outQuads[k].m_aafPos[c][1] += deltaY; }
                for (size_t k = stringStart; k < outStrings.size(); ++k) {
                    outStrings[k].m_afPos[0] += deltaX; outStrings[k].m_afPos[1] += deltaY;
                }
            }
        }
    }

    // Debug: draw the snap grid ON TOP of everything (INI-only, off by default), so HUD
    // edges can be checked against the lattice they snap to. Same on both surfaces.
    if (UiConfig::getInstance().getGridOverlay()) {
        appendGridOverlay(outQuads);
    }
}

// Grid line spacing = the snap lattice (HudGrid). Vertical lines every GRID_SIZE_HORIZONTAL
// across X, horizontal lines every GRID_SIZE_VERTICAL across Y, in normalized [0,1] screen
// space — exactly the grid SNAP_TO_GRID_X/Y quantize to. Kept in one place so the count
// helper and the drawer can't drift.
namespace {
    constexpr float GRID_CELL_W = PluginConstants::HudGrid::GRID_SIZE_HORIZONTAL;  // 0.0055
    constexpr float GRID_CELL_H = PluginConstants::HudGrid::GRID_SIZE_VERTICAL;    // ~0.011734
    // Line thicknesses in normalized units (~1px minor / ~2px major at 1080p).
    constexpr float GRID_MINOR_W = 0.00055f, GRID_MAJOR_W = 0.00110f;  // vertical-line width
    constexpr float GRID_MINOR_H = 0.00095f, GRID_MAJOR_H = 0.00190f;  // horizontal-line height
    inline int gridLineCount(float cell) { return static_cast<int>(1.0f / cell) + 1; }
}

size_t HudManager::gridOverlayQuadCount() {
    return static_cast<size_t>(gridLineCount(GRID_CELL_W) + gridLineCount(GRID_CELL_H));
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
    const int vCount = gridLineCount(GRID_CELL_W);
    for (int i = 0; i < vCount; ++i) {
        const bool major = (i % majorEvery) == 0;
        const float w = major ? GRID_MAJOR_W : GRID_MINOR_W;
        pushLine(i * GRID_CELL_W - w * 0.5f, 0.0f, w, 1.0f, major ? majorColor : minorColor);
    }
    // Horizontal lines (index 0 at y=0).
    const int hCount = gridLineCount(GRID_CELL_H);
    for (int j = 0; j < hCount; ++j) {
        const bool major = (j % majorEvery) == 0;
        const float h = major ? GRID_MAJOR_H : GRID_MINOR_H;
        pushLine(0.0f, j * GRID_CELL_H - h * 0.5f, 1.0f, h, major ? majorColor : minorColor);
    }
}
