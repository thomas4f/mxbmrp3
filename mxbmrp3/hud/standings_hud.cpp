// ============================================================================
// hud/standings_hud.cpp
// Displays race standings and lap times with position, gaps, and rider information
// ============================================================================
#include "standings_hud.h"
#include "../diagnostics/logger.h"
#include "../diagnostics/timer.h"
#include "../core/plugin_utils.h"
#include "../core/plugin_constants.h"
#include "../core/color_config.h"
#include "../core/input_manager.h"
#include "../core/plugin_manager.h"
#include "../core/tracked_riders_manager.h"
#include "../core/asset_manager.h"
#include "../core/director_manager.h"
#include <algorithm>
#include <cstring>
#include <cstdio>

using namespace PluginConstants;

#if defined(MXBMRP3_TEST_BUILD)
#include <chrono>
// Per-phase profiling for the headless standings perf probe (test builds only).
// Attributes rebuildRenderData() cost to setup (build display entries) / format
// (gap+laptime+penalty strings) / name+anim / layout / render (per-row quads +
// strings). Compiled out of every shipping DLL.
namespace {
    using StClock = std::chrono::steady_clock;
    double g_stSetupUs = 0, g_stFormatUs = 0, g_stNameAnimUs = 0, g_stLayoutUs = 0, g_stRenderUs = 0;
    long long g_stCount = 0;
    inline double stUsSince(StClock::time_point a) {
        return std::chrono::duration<double, std::micro>(StClock::now() - a).count();
    }
}
void standingsReadProfile(double& setupUs, double& formatUs, double& nameAnimUs,
                          double& layoutUs, double& renderUs, long long& count) {
    setupUs = g_stSetupUs; formatUs = g_stFormatUs; nameAnimUs = g_stNameAnimUs;
    layoutUs = g_stLayoutUs; renderUs = g_stRenderUs; count = g_stCount;
    g_stSetupUs = g_stFormatUs = g_stNameAnimUs = g_stLayoutUs = g_stRenderUs = 0;
    g_stCount = 0;
}

// Shared with standings_hud_render.cpp (renderRiderRow), so external linkage.
double g_standingsTrackedUs = 0;
double standingsReadTrackedUs() { double v = g_standingsTrackedUs; g_standingsTrackedUs = 0; return v; }
#endif

void StandingsHud::update() {
    // OPTIMIZATION: Skip all processing when not visible
    // Mouse handling and hover tracking only matter when HUD is rendered
    if (!isVisibleAnySurface()) {
        clearDataDirty();
        clearLayoutDirty();
        return;
    }

    // Handle mouse input for rider selection (LMB for clicking, RMB for dragging)
    const InputManager& input = InputManager::getInstance();

    if (input.getLeftButton().isClicked()) {
        // Shift into build space so rider rows line up when dragged on the companion.
        CursorPosition cursor = input.getCursorPosition();
        mapCursorToHudSpace(cursor.x, cursor.y);
        if (cursor.isValid) {
            handleClick(cursor.x, cursor.y);
        }
    }

    // Track hover state in spectator mode only
    const PluginData& pluginData = PluginData::getInstance();
    int drawState = pluginData.getDrawState();
    bool isSpectatorMode = (drawState == PluginConstants::ViewState::SPECTATE);

    if (isSpectatorMode) {
        CursorPosition cursor = input.getCursorPosition();
        mapCursorToHudSpace(cursor.x, cursor.y);
        int newHoveredRow = -1;

        if (cursor.isValid) {
            // Check which row (if any) the cursor is over
            for (size_t i = 0; i < m_riderClickRegions.size(); ++i) {
                const auto& region = m_riderClickRegions[i];
                if (isPointInRect(cursor.x, cursor.y, region.x, region.y, region.width, region.height)) {
                    // Find this rider's index in m_displayEntries
                    for (size_t j = 0; j < m_displayEntries.size(); ++j) {
                        if (m_displayEntries[j].raceNum == region.raceNum) {
                            newHoveredRow = static_cast<int>(j);
                            break;
                        }
                    }
                    break;
                }
            }
        }

        // If hover state changed, trigger rebuild
        if (newHoveredRow != m_hoveredRowIndex) {
            m_hoveredRowIndex = newHoveredRow;
            setDataDirty();
        }
    } else if (m_hoveredRowIndex != -1) {
        // Clear hover state when not in spectator mode
        m_hoveredRowIndex = -1;
        setDataDirty();
    }

    // Check if hazard/blue flag icon state changed for any displayed rider
    if (m_enabledColumns & COL_TRACKED) {
        for (const auto& entry : m_displayEntries) {
            if (entry.isPlaceholder || entry.raceNum <= 0) continue;
            uint8_t state = static_cast<uint8_t>(pluginData.getRiderHazardType(entry.raceNum));
            if (pluginData.isRiderBlueFlagged(entry.raceNum)) state |= 0x10;
            const StandingsData* standing = pluginData.getStanding(entry.raceNum);
            if (standing && standing->pit == 1) state |= 0x20;
            if (standing && pluginData.isRaceSession() && pluginData.getSessionData().isRiderOnLastLap(standing->numLaps, standing->numLapsAtLeaderFinish)) state |= 0x40;
            if (DirectorManager::getInstance().isLocked() && DirectorManager::getInstance().getCurrentSubject() == entry.raceNum) state |= 0x80;
            auto it = m_cachedIconStates.find(entry.raceNum);
            if (it == m_cachedIconStates.end() || it->second != state) {
                m_cachedIconStates[entry.raceNum] = state;
                setDataDirty();
            }
        }
    }

    // Capture frame time once for consistent timing across all animation logic
    m_frameTime = std::chrono::steady_clock::now();

    // Alternating gap reference: toggle between Leader and Player on interval
    if (m_gapReferenceMode == GapReferenceMode::ALTERNATING) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            m_frameTime - m_lastGapRefToggle).count();
        if (elapsed >= m_alternatingIntervalMs) {
            m_alternatingCurrent = (m_alternatingCurrent == GapReferenceMode::LEADER)
                ? GapReferenceMode::PLAYER : GapReferenceMode::LEADER;
            m_lastGapRefToggle = m_frameTime;
            setDataDirty();
        }
    }

    // Clean up finished animations before rebuilds so we don't do wasteful lookups
    if (hasActiveAnimations()) {
        for (auto it = m_activeAnimations.begin(); it != m_activeAnimations.end(); ) {
            float elapsedMs = std::chrono::duration<float, std::milli>(m_frameTime - it->second.startTime).count();
            if (elapsedMs >= m_animationDurationMs) {
                it = m_activeAnimations.erase(it);
            } else {
                ++it;
            }
        }
    }

    // Keep updating layout during active animations (smooth per-frame interpolation).
    // Slide-highlight quads (COLORED mode) are emitted on the data-change rebuild that
    // started the animation and then updated per-frame in rebuildLayout, so a cheap
    // layout update is sufficient regardless of mode.
    if (hasActiveAnimations()) {
        setLayoutDirty();
    }

    // Handle dirty flags using base class helper
    processDirtyFlags();
}

void StandingsHud::rebuildLayout() {

    // Fast path - only update positions
    // Apply scale to all dimensions
    auto dim = getScaledDimensions();

    // Render all display entries (rider rows + gap rows)
    int rowsToRender = static_cast<int>(m_displayEntries.size());
    auto hudDim = calculateHudDimensions(dim, rowsToRender);

    setBounds(START_X, START_Y, START_X + hudDim.backgroundWidth, START_Y + hudDim.backgroundHeight);

    // Update background quad position
    updateBackgroundQuadPosition(START_X, START_Y, hudDim.backgroundWidth, hudDim.backgroundHeight);

    // Update player row highlight quad position if the optional legacy row
    // background is enabled. Default behavior (accent-colored name) emits no
    // quad and skips this entirely.
    if (m_cachedHighlightQuadIndex >= 0 && m_cachedHighlightQuadIndex < static_cast<int>(m_quads.size()) &&
        m_cachedPlayerIndex >= 0 && m_cachedPlayerIndex < rowsToRender) {
        float highlightY = hudDim.contentStartY + hudDim.titleHeight + hudDim.headerHeight + (m_cachedPlayerIndex * dim.lineHeightNormal);
        const auto& playerEntry = m_displayEntries[m_cachedPlayerIndex];
        if (!playerEntry.isPlaceholder && playerEntry.raceNum >= 0) {
            highlightY += getAnimatedRowOffset(playerEntry.raceNum, dim.lineHeightNormal);
        }
        float highlightX = START_X;
        applyOffset(highlightX, highlightY);
        setQuadPositions(m_quads[m_cachedHighlightQuadIndex], highlightX, highlightY, hudDim.backgroundWidth, dim.lineHeightNormal);
    }

    // Update slide-highlight quads: position tracks the row's animation offset; alpha
    // fades linearly with animation progress. Finished slides have their alpha set to
    // zero here and remain in m_quads (invisible) until the next data-change rebuild
    // drops them — keeping cached indices for other quad lists stable.
    for (const auto& slide : m_slideHighlightQuads) {
        if (slide.quadIndex >= m_quads.size() || slide.rowIndex >= rowsToRender) continue;

        float fade = getSlideFade(slide.raceNum);

        float slideY = hudDim.contentStartY + hudDim.titleHeight + hudDim.headerHeight + (slide.rowIndex * dim.lineHeightNormal);
        slideY += getAnimatedRowOffset(slide.raceNum, dim.lineHeightNormal);
        float slideX = START_X;
        applyOffset(slideX, slideY);
        setQuadPositions(m_quads[slide.quadIndex], slideX, slideY, hudDim.backgroundWidth, dim.lineHeightNormal);

        unsigned long tintColor = slide.promoted
            ? this->getColor(ColorSlot::POSITIVE)
            : this->getColor(ColorSlot::NEGATIVE);
        m_quads[slide.quadIndex].m_ulColor = PluginUtils::applyOpacity(tintColor, ROW_HIGHLIGHT_OPACITY * fade);
    }

    // Update tracked icon quad positions
    if (!m_trackedIconQuads.empty()) {
        // Get tracked column position from column table
        float trackedColPosition = m_columns.tracked;

        // Same size calculations as in renderRiderRow
        constexpr float baseConeSize = 0.006f;
        float baseHalfSize = baseConeSize * m_fScale;
        float spriteHalfSize = baseHalfSize;
        float spriteHalfWidth = spriteHalfSize / UI_ASPECT_RATIO;
        float colWidth = PluginUtils::calculateMonospaceTextWidth(COL_TRACKED_WIDTH, dim.fontSize);

        for (const auto& iconInfo : m_trackedIconQuads) {
            if (iconInfo.quadIndex >= m_quads.size()) continue;

            // Calculate position for this row with animation offset
            float rowY = hudDim.contentStartY + hudDim.titleHeight + hudDim.headerHeight + (iconInfo.rowIndex * dim.lineHeightNormal);
            if (iconInfo.rowIndex < static_cast<int>(m_displayEntries.size())) {
                const auto& entry = m_displayEntries[iconInfo.rowIndex];
                if (!entry.isPlaceholder && entry.raceNum >= 0) {
                    rowY += getAnimatedRowOffset(entry.raceNum, dim.lineHeightNormal);
                }
            }
            float spriteCenterX = trackedColPosition + colWidth * 0.25f;
            float spriteCenterY = rowY + dim.lineHeightNormal * 0.5f;

            float x = spriteCenterX, y = spriteCenterY;
            applyOffset(x, y);

            // Update quad positions
            SPluginQuad_t& sprite = m_quads[iconInfo.quadIndex];
            sprite.m_aafPos[0][0] = x - spriteHalfWidth;  // Top-left
            sprite.m_aafPos[0][1] = y - spriteHalfSize;
            sprite.m_aafPos[1][0] = x - spriteHalfWidth;  // Bottom-left
            sprite.m_aafPos[1][1] = y + spriteHalfSize;
            sprite.m_aafPos[2][0] = x + spriteHalfWidth;  // Bottom-right
            sprite.m_aafPos[2][1] = y + spriteHalfSize;
            sprite.m_aafPos[3][0] = x + spriteHalfWidth;  // Top-right
            sprite.m_aafPos[3][1] = y - spriteHalfSize;
        }
    }

    // Update positions-gained/lost caret quad positions (mirror renderRiderRow geometry)
    if (!m_posGainIconQuads.empty()) {
        constexpr float baseConeSize = 0.0045f;  // keep in sync with renderRiderRow
        float spriteHalfSize = baseConeSize * m_fScale;
        float spriteHalfWidth = spriteHalfSize / UI_ASPECT_RATIO;
        float charW = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize);

        for (const auto& iconInfo : m_posGainIconQuads) {
            if (iconInfo.quadIndex >= m_quads.size()) continue;

            float rowY = hudDim.contentStartY + hudDim.titleHeight + hudDim.headerHeight + (iconInfo.rowIndex * dim.lineHeightNormal);
            if (iconInfo.rowIndex < static_cast<int>(m_displayEntries.size())) {
                const auto& entry = m_displayEntries[iconInfo.rowIndex];
                if (!entry.isPlaceholder && entry.raceNum >= 0) {
                    rowY += getAnimatedRowOffset(entry.raceNum, dim.lineHeightNormal);
                }
            }
            float spriteCenterX = m_columns.posGain + charW * 0.9f;
            float spriteCenterY = rowY + dim.lineHeightNormal * 0.5f;

            float x = spriteCenterX, y = spriteCenterY;
            applyOffset(x, y);
            // Down = 180° rotation (negate both axes) to keep CCW winding; see renderRiderRow.
            float sx = iconInfo.down ? -spriteHalfWidth : spriteHalfWidth;
            float sy = iconInfo.down ? -spriteHalfSize  : spriteHalfSize;

            SPluginQuad_t& sprite = m_quads[iconInfo.quadIndex];
            sprite.m_aafPos[0][0] = x - sx; sprite.m_aafPos[0][1] = y - sy;  // Top-left
            sprite.m_aafPos[1][0] = x - sx; sprite.m_aafPos[1][1] = y + sy;  // Bottom-left
            sprite.m_aafPos[2][0] = x + sx; sprite.m_aafPos[2][1] = y + sy;  // Bottom-right
            sprite.m_aafPos[3][0] = x + sx; sprite.m_aafPos[3][1] = y - sy;  // Top-right
        }
    }

    // Update race number plate quad positions
    if (!m_raceNumPlateQuads.empty()) {
        PlateGeometry pg(dim.fontSize, dim.lineHeightNormal);

        for (const auto& plate : m_raceNumPlateQuads) {
            if (plate.numberQuadIndex >= m_quads.size() || plate.brandQuadIndex >= m_quads.size()) continue;

            float rowY = hudDim.contentStartY + hudDim.titleHeight + hudDim.headerHeight + (plate.rowIndex * dim.lineHeightNormal);
            if (plate.rowIndex < static_cast<int>(m_displayEntries.size())) {
                const auto& entry = m_displayEntries[plate.rowIndex];
                if (!entry.isPlaceholder && entry.raceNum >= 0) {
                    rowY += getAnimatedRowOffset(entry.raceNum, dim.lineHeightNormal);
                }
            }

            float npX = m_columns.raceNum, npY = rowY + pg.platePadY;
            applyOffset(npX, npY);

            // Update number plate quad
            SPluginQuad_t& numPlate = m_quads[plate.numberQuadIndex];
            setQuadPositions(numPlate, npX, npY, pg.plateWidth, pg.plateHeight);

            // Update brand strip quad
            SPluginQuad_t& brandStrip = m_quads[plate.brandQuadIndex];
            float bsLeftX = npX + pg.plateWidth + pg.stripGap;
            setQuadPositions(brandStrip, bsLeftX, npY, pg.brandStripWidth, pg.plateHeight);
        }
    }

    // Update all string positions
    float currentY = hudDim.contentStartY;
    size_t stringIndex = 0;

    // Title string (index 0, always exists, but may be empty if hidden)
    if (stringIndex < m_strings.size()) {
        float x = hudDim.contentStartX;
        float y = currentY;
        applyOffset(x, y);
        m_strings[stringIndex].m_afPos[0] = x;
        m_strings[stringIndex].m_afPos[1] = y;
        stringIndex++;
    }
    if (m_bShowTitle) currentY += dim.lineHeightLarge;

    // Session-info string (index 1, always exists, but may be empty if disabled)
    if (stringIndex < m_strings.size()) {
        float x = hudDim.contentStartX;
        float y = currentY;
        applyOffset(x, y);
        m_strings[stringIndex].m_afPos[0] = x;
        m_strings[stringIndex].m_afPos[1] = y;
        stringIndex++;
    }
    if (m_bShowSessionInfo) currentY += dim.lineHeightNormal;

    // Column-header row strings (match the emit order/count in rebuildRenderData)
    if (m_bShowHeaders) {
        // Same vertical centering offset as rebuildRenderData (smaller header font)
        float headerY = currentY + labelRowYOffset(dim);
        for (const auto& col : m_columnTable) {
            if (col.columnIndex == COL_IDX_TRACKED) continue;
            if (stringIndex >= m_strings.size()) break;
            float x = getColumnHeaderTextX(col.columnIndex, col.position, dim.fontSize, nullptr);
            float y = headerY;
            applyOffset(x, y);
            m_strings[stringIndex].m_afPos[0] = x;
            m_strings[stringIndex].m_afPos[1] = y;
            stringIndex++;
        }
        currentY += hudDim.headerHeight;
    }

    // Update positions for actual rows being rendered (no spacing, consistent with other HUDs)
    // Use table-driven approach (only loops over enabled columns)
    for (int i = 0; i < rowsToRender; ++i) {
        // Calculate animation offset for this row
        float animOffset = 0.0f;
        if (i < static_cast<int>(m_displayEntries.size())) {
            const auto& entry = m_displayEntries[i];
            if (!entry.isPlaceholder && entry.raceNum >= 0) {
                animOffset = getAnimatedRowOffset(entry.raceNum, dim.lineHeightNormal);
            }
        }

        // Each row has strings for enabled columns only
        bool isPlaceholder = (i < static_cast<int>(m_displayEntries.size())) && m_displayEntries[i].isPlaceholder;
        for (const auto& col : m_columnTable) {
            // Skip tracked column - it uses quads, not strings
            if (col.columnIndex == COL_IDX_TRACKED) continue;

            if (stringIndex >= m_strings.size()) break;
            // Same alignment anchor as renderRiderRow (position/race-number columns
            // shift within their cell, gap column right-aligns); shared helper keeps
            // the two paths in sync.
            bool gapRightAlign = (col.columnIndex == COL_IDX_GAP && !isPlaceholder);
            float x = getColumnTextX(col.columnIndex, col.position, dim.fontSize, isPlaceholder, gapRightAlign);
            float y = currentY + animOffset;
            applyOffset(x, y);
            m_strings[stringIndex].m_afPos[0] = x;
            m_strings[stringIndex].m_afPos[1] = y;
            stringIndex++;
        }

        currentY += dim.lineHeightNormal;
    }
}

void StandingsHud::rebuildRenderData() {
#if defined(MXBMRP3_TEST_BUILD)
    auto stSetupStart = StClock::now();
#endif

    clearStrings();
    m_quads.clear();
    m_displayEntries.clear();
    m_cachedHighlightQuadIndex = -1;  // Reset highlight quad tracking
    m_trackedIconQuads.clear();  // Reset icon quad tracking
    m_posGainIconQuads.clear();  // Reset positions-gained/lost caret quad tracking
    m_raceNumPlateQuads.clear(); // Reset race number plate quad tracking
    m_slideHighlightQuads.clear(); // Reset slide-highlight quad tracking

    const PluginData& pluginData = PluginData::getInstance();
    int displayRaceNum = pluginData.getDisplayRaceNum();
    const SessionData& sessionData = pluginData.getSessionData();
    const auto& classificationOrder = pluginData.getDisplayClassificationOrder();

    // Prune stale icon cache entries for riders no longer in the classification
    // (e.g. after a new session/race, or a departed rider whose number is reused).
    // Do NOT compare sizes: m_cachedIconStates only ever holds *displayed* riders,
    // so on any grid larger than the display row count a size mismatch is permanent,
    // which would wipe the cache every rebuild -> update() re-inserts + setDataDirty()
    // -> full rebuild every frame (defeating dirty-gating; the 240fps trap).
    if (!m_cachedIconStates.empty()) {
        for (auto it = m_cachedIconStates.begin(); it != m_cachedIconStates.end();) {
            if (std::find(classificationOrder.begin(), classificationOrder.end(), it->first)
                    == classificationOrder.end()) {
                it = m_cachedIconStates.erase(it);
            } else {
                ++it;
            }
        }
    }

    // Column configuration is now managed by the profile system
    bool isRace = pluginData.isRaceSession();

    // Determine gap data source: use live gap in race sessions when enabled
    bool useLiveGap = isRace && m_bLiveGaps;

    // Apply gap mode toggle
    uint32_t effectiveColumns = m_enabledColumns;
    if (m_gapMode != GapMode::OFF) {
        effectiveColumns |= COL_GAP;
    } else {
        effectiveColumns &= ~COL_GAP;
    }

    // Apply name mode: OFF removes the column, SHORT/LONG enables it
    if (m_nameMode == NameMode::OFF) {
        effectiveColumns &= ~COL_NAME;
    } else {
        effectiveColumns |= COL_NAME;
    }

    // Apply positions-gained mode: OFF removes the column, any reference mode enables it
    if (m_posGainMode == PosGainMode::OFF) {
        effectiveColumns &= ~COL_POSGAIN;
    } else {
        effectiveColumns |= COL_POSGAIN;
    }

    // Only log when configuration actually changes
    static uint32_t prevEffectiveColumns = 0;
    if (effectiveColumns != prevEffectiveColumns) {
        DEBUG_INFO_F("StandingsHud column config: enabledColumns=0x%X, effective=0x%X",
            m_enabledColumns, effectiveColumns);
        prevEffectiveColumns = effectiveColumns;
    }

    // Use effective columns (with gap mode adjustments) for rendering
    uint32_t savedEnabledColumns = m_enabledColumns;
    m_enabledColumns = effectiveColumns;

    // Build display entries with smart pagination
    // Strategy:
    // - If display rider is in top 3 and (running or spectating): show first N riders (simple case)
    // - If display rider is beyond top 3 and (running or spectating): show top 3 + rider context
    // - Otherwise (rider not found): show first N riders (fallback)

    // Find display rider's position in classification
    int playerPositionInClassification = -1;
    for (size_t i = 0; i < classificationOrder.size(); ++i) {
        if (classificationOrder[i] == displayRaceNum) {
            playerPositionInClassification = static_cast<int>(i);
            break;
        }
    }

    const int SEPARATOR_ROWS = 0; // No separator (removed for cleaner display)

    m_cachedPlayerIndex = -1;

    // Show context when player is running OR when spectating/in replay
    int drawState = pluginData.getDrawState();
    bool shouldShowContext = pluginData.isPlayerRunning() ||
                             drawState == PluginConstants::ViewState::SPECTATE ||
                             drawState == PluginConstants::ViewState::REPLAY;

    if (playerPositionInClassification >= 0 && playerPositionInClassification < m_topPositionsCount && shouldShowContext) {
        // Display rider is in top N - show first m_displayRowCount riders
        int entriesToBuild = std::min(static_cast<int>(classificationOrder.size()), m_displayRowCount);
        m_displayEntries.reserve(entriesToBuild);
        addDisplayEntries(0, entriesToBuild - 1, 1, classificationOrder, pluginData);
    } else if (playerPositionInClassification >= m_topPositionsCount && shouldShowContext) {
        // Display rider is beyond top N and (running or spectating) - show top N + rider context
        m_displayEntries.reserve(m_displayRowCount);

        // 1. Add top N riders
        addDisplayEntries(0, m_topPositionsCount - 1, 1, classificationOrder, pluginData);

        // 2. Calculate rider context window
        int availableRows = m_displayRowCount - m_topPositionsCount - SEPARATOR_ROWS;
        int contextBefore = availableRows / 2;
        int contextAfter = availableRows - contextBefore - 1;  // -1 for rider row

        int startIndex = std::max(m_topPositionsCount, playerPositionInClassification - contextBefore);

        // Compensate endIndex when startIndex is clamped (overlap with top N)
        int lostBefore = startIndex - (playerPositionInClassification - contextBefore);
        int desiredEndIndex = playerPositionInClassification + contextAfter + lostBefore;
        int endIndex = std::min(static_cast<int>(classificationOrder.size()) - 1, desiredEndIndex);

        // Compensate startIndex when endIndex is clamped (player near end of field)
        int lostAfter = desiredEndIndex - endIndex;
        if (lostAfter > 0) {
            startIndex = std::max(m_topPositionsCount, startIndex - lostAfter);
        }

        // 3. Add rider context
        addDisplayEntries(startIndex, endIndex, startIndex + 1, classificationOrder, pluginData);
    } else {
        // Display rider not found or no classification - show first m_displayRowCount riders
        int entriesToBuild = std::min(static_cast<int>(classificationOrder.size()), m_displayRowCount);
        m_displayEntries.reserve(entriesToBuild);
        addDisplayEntries(0, entriesToBuild - 1, 1, classificationOrder, pluginData);
    }

    // Add placeholder rows to fill up to configured size
    // This shows the user how big the HUD will be when fully populated
    {
        int placeholderCount = m_displayRowCount - static_cast<int>(m_displayEntries.size());
        if (placeholderCount > 0) {
            // Reserve space for placeholders
            m_displayEntries.reserve(m_displayEntries.size() + placeholderCount);

            // Add placeholder rows at the end
            for (int i = 0; i < placeholderCount; ++i) {
                DisplayEntry placeholder;
                placeholder.isPlaceholder = true;
                strcpy_s(placeholder.formattedPosition, sizeof(placeholder.formattedPosition), Placeholders::GENERIC);
                m_displayEntries.push_back(placeholder);
            }
        }
    }

    // Format strings for all built entries (they're all displayed)
    // Resolve effective gap reference mode (ALTERNATING → current LEADER or PLAYER)
    const GapReferenceMode effectiveGapRef = getEffectiveGapReferenceMode();

    // Get player's gaps for player-relative mode
    const StandingsData* playerStanding = pluginData.getStanding(displayRaceNum);
    int playerOfficialGap = (playerStanding && playerStanding->gap > 0) ? playerStanding->gap : 0;
    int playerGapLaps = playerStanding ? playerStanding->gapLaps : 0;
    // Player needs valid gap data for relative comparisons to be meaningful.
    // The leader (P1) has gap == 0 legitimately — their zero is valid data, not missing.
    bool playerIsLeader = (!classificationOrder.empty() && classificationOrder[0] == displayRaceNum);
    // The player-relative live gap subtracts the player's realTimeGap as the
    // reference for every other rider, so it must be a FRESH value. If the player
    // (spectated/target rider) has dropped out of the current track-position batch
    // its realTimeGap is stale (the game only sends the ~10 closest vehicles) —
    // gate on hasActiveTrackPos exactly as the per-rider live path does below, so
    // a stale reference doesn't skew every rider's gap. The leader is always valid.
    bool playerLiveGapUsable = playerStanding &&
        (pluginData.hasActiveTrackPos(displayRaceNum) || playerIsLeader);
    int playerLiveGap = (playerLiveGapUsable && playerStanding->realTimeGap > 0)
        ? playerStanding->realTimeGap : 0;
    bool playerHasGapData = playerStanding && (playerStanding->bestLap > 0 || playerStanding->gap > 0 || playerStanding->gapLaps > 0 || playerIsLeader);

#if defined(MXBMRP3_TEST_BUILD)
    g_stSetupUs += stUsSince(stSetupStart);
    auto stFormatStart = StClock::now();
#endif

    for (size_t entryIdx = 0; entryIdx < m_displayEntries.size(); ++entryIdx) {
        auto& entry = m_displayEntries[entryIdx];
        // Skip formatting for placeholders
        if (entry.isPlaceholder) {
            continue;
        }

        entry.updateFormattedStrings();

        // Determine if rider has finished (used for icon display and gap logic)
        entry.isFinishedRace = (entry.state == RiderState::NORMAL) &&
            (sessionData.isRiderFinished(entry.numLaps, entry.numLapsAtLeaderFinish) || entry.sessionFinished);

        // Format gap column
        // Two-phase approach:
        //   Phase 1: Compute the gap value (live for same-lap riders, official for lapped/finished)
        //   Phase 2: Apply reference mode offset, scope filtering, and formatting
        bool isPlayerRow = (entry.raceNum == displayRaceNum);
        bool shouldShowGap = shouldShowGapForScope(isPlayerRow);

        bool isGapReference = (effectiveGapRef == GapReferenceMode::LEADER && entry.position == Position::FIRST) ||
                              (effectiveGapRef == GapReferenceMode::PLAYER && isPlayerRow);

        if (entry.state != RiderState::NORMAL) {
            // Non-participants (DNS, retired, DSQ) show status in gap column
            const char* stateAbbr = PluginUtils::getRiderStateAbbreviation(entry.state);
            if (stateAbbr[0] != '\0') {
                strcpy_s(entry.formattedGap, sizeof(entry.formattedGap), stateAbbr);
                entry.gapStyle = DisplayEntry::GapStyle::LABEL;
            } else {
                entry.formattedGap[0] = '\0';
            }
        }
        else if (isGapReference) {
            // Reference rider (leader or player): show contextual info instead of a gap value
            int leaderFinishTime = sessionData.leaderFinishTime;
            bool leaderFinished = (leaderFinishTime >= 0);

            if (!isRace) {
                // Non-race: show best lap time (right-aligned like numeric gaps)
                if (entry.bestLap > 0) {
                    char tmp[16];
                    PluginUtils::formatLapTime(entry.bestLap, tmp, sizeof(tmp));
                    snprintf(entry.formattedGap, sizeof(entry.formattedGap), "%s", tmp);
                } else {
                    strcpy_s(entry.formattedGap, sizeof(entry.formattedGap), Placeholders::GENERIC);
                }
            } else if (leaderFinished) {
                // Race finished: show finish time (right-aligned like numeric gaps)
                if (effectiveGapRef == GapReferenceMode::LEADER && leaderFinishTime > 0) {
                    char tmp[16];
                    PluginUtils::formatLapTime(leaderFinishTime, tmp, sizeof(tmp));
                    snprintf(entry.formattedGap, sizeof(entry.formattedGap), "%s", tmp);
                } else if (effectiveGapRef == GapReferenceMode::PLAYER && playerStanding && playerStanding->finishTime > 0) {
                    char tmp[16];
                    PluginUtils::formatLapTime(playerStanding->finishTime, tmp, sizeof(tmp));
                    snprintf(entry.formattedGap, sizeof(entry.formattedGap), "%s", tmp);
                } else {
                    entry.gapStyle = DisplayEntry::GapStyle::LABEL;
                    strcpy_s(entry.formattedGap, sizeof(entry.formattedGap),
                        effectiveGapRef == GapReferenceMode::LEADER ? "Leader" : "Player");
                }
            } else {
                // Race in progress: show label
                entry.gapStyle = DisplayEntry::GapStyle::LABEL;
                strcpy_s(entry.formattedGap, sizeof(entry.formattedGap),
                    effectiveGapRef == GapReferenceMode::LEADER ? "Leader" : "Player");
            }
        }
        else if (!shouldShowGap) {
            strcpy_s(entry.formattedGap, sizeof(entry.formattedGap), Placeholders::GENERIC);
        }
        else {
            // --- Phase 1: Determine visibility (ADJACENT scope filtering) ---
            bool showThisRow = true;
            int playerIdx = m_cachedPlayerIndex;
            int idx = static_cast<int>(entryIdx);

            if (m_gapMode == GapMode::ADJACENT) {
                showThisRow = (playerIdx >= 0) &&
                              (idx == playerIdx - 1 || idx == playerIdx || idx == playerIdx + 1);
            }

            if (!showThisRow) {
                strcpy_s(entry.formattedGap, sizeof(entry.formattedGap), Placeholders::GENERIC);
            } else {
                // --- Phase 2: Compute gap value ---
                // Lapped riders (gapLaps > 0) always use official lap gap — live timing
                // is only meaningful for riders on the same lap as the leader.
                // Finished riders also use official gap (their time is fixed).
                bool isLapped = (entry.gapLaps > 0);
                bool isFinished = pluginData.getSessionData().isRiderFinished(entry.numLaps, entry.numLapsAtLeaderFinish);
                bool canUseLive = useLiveGap && !isLapped && !isFinished && entry.realTimeGap > 0;

                bool isActiveTrackPos = pluginData.hasActiveTrackPos(entry.raceNum) || entry.position == Position::FIRST;
                bool canUseLiveForRider = canUseLive && isActiveTrackPos;

                if (effectiveGapRef == GapReferenceMode::PLAYER && !playerHasGapData) {
                    // Player has no lap time yet — show absolute best lap times as fallback
                    if (entry.bestLap > 0) {
                        char tmp[16];
                        PluginUtils::formatLapTime(entry.bestLap, tmp, sizeof(tmp));
                        snprintf(entry.formattedGap, sizeof(entry.formattedGap), "%s", tmp);
                    } else {
                        strcpy_s(entry.formattedGap, sizeof(entry.formattedGap), Placeholders::GENERIC);
                    }
                } else if (effectiveGapRef == GapReferenceMode::PLAYER) {
                    // Player-relative gaps
                    if (canUseLiveForRider && (playerLiveGap > 0 || playerIsLeader) &&
                               (entry.realTimeGap > 0 || entry.position == Position::FIRST)) {
                        int relativeGap = entry.realTimeGap - playerLiveGap;
                        PluginUtils::formatTimeDiff(entry.formattedGap, sizeof(entry.formattedGap), relativeGap);
                        entry.gapStyle = DisplayEntry::GapStyle::LIVE;
                    } else if (entry.officialGap > 0 || entry.gapLaps > 0 || entry.position == Position::FIRST) {
                        int relativeLapGap = entry.gapLaps - playerGapLaps;
                        int relativeTimeGap = entry.officialGap - playerOfficialGap;
                        if (relativeLapGap != 0) {
                            snprintf(entry.formattedGap, sizeof(entry.formattedGap), "%+dL", relativeLapGap);
                        } else if (relativeTimeGap != 0) {
                            PluginUtils::formatTimeDiff(entry.formattedGap, sizeof(entry.formattedGap), relativeTimeGap);
                        } else {
                            strcpy_s(entry.formattedGap, sizeof(entry.formattedGap), Placeholders::GENERIC);
                        }
                    } else {
                        strcpy_s(entry.formattedGap, sizeof(entry.formattedGap), Placeholders::GENERIC);
                    }
                } else {
                    // Leader-relative gaps
                    if (canUseLiveForRider) {
                        PluginUtils::formatTimeDiff(entry.formattedGap, sizeof(entry.formattedGap), entry.realTimeGap);
                        entry.gapStyle = DisplayEntry::GapStyle::LIVE;
                    } else if (isLapped) {
                        snprintf(entry.formattedGap, sizeof(entry.formattedGap), "+%dL", entry.gapLaps);
                    } else if (entry.officialGap > 0) {
                        PluginUtils::formatTimeDiff(entry.formattedGap, sizeof(entry.formattedGap), entry.officialGap);
                    } else {
                        strcpy_s(entry.formattedGap, sizeof(entry.formattedGap), Placeholders::GENERIC);
                    }
                }

                // --- Phase 3: Adjacent mode coloring (Player ref only) ---
                // Skip coloring when showing absolute lap times (no player gap data yet)
                if (m_gapMode == GapMode::ADJACENT && effectiveGapRef == GapReferenceMode::PLAYER &&
                    playerHasGapData && playerIdx >= 0 && idx != playerIdx &&
                    strcmp(entry.formattedGap, Placeholders::GENERIC) != 0 && entry.formattedGap[0] != '\0') {
                    entry.gapColorOverride = (idx < playerIdx)
                        ? this->getColor(ColorSlot::NEGATIVE)
                        : this->getColor(ColorSlot::POSITIVE);
                }
            }
        }

        // Format best lap time
        if (entry.hasBestLap) {
            PluginUtils::formatLapTime(entry.bestLap, entry.formattedLapTime, sizeof(entry.formattedLapTime));
        }
        else {
            strcpy_s(entry.formattedLapTime, sizeof(entry.formattedLapTime), Placeholders::LAP_TIME);
        }

        // Format last lap time (cuts included; 0/none -> placeholder)
        if (entry.hasLastLap) {
            PluginUtils::formatLapTime(entry.lastLap, entry.formattedLastLap, sizeof(entry.formattedLastLap));
        }
        else {
            strcpy_s(entry.formattedLastLap, sizeof(entry.formattedLastLap), Placeholders::LAP_TIME);
        }

        // Hidden INI faster/slower coding vs the LOCAL rider's last lap (the display
        // target - your own bike, or the rider you're spectating). Uses the semantic
        // POSITIVE/NEGATIVE palette slots (default green/red, but follows the user's
        // theme), not literal colors. Default off (m_bLastLapColorCode). Skip the local
        // rider's own row and any row without a comparable time; leave the override at 0
        // so the default color applies.
        entry.lastLapColorOverride = 0;
        if (m_bLastLapColorCode && entry.hasLastLap &&
            m_cachedPlayerIndex >= 0 && m_cachedPlayerIndex < static_cast<int>(m_displayEntries.size()) &&
            static_cast<int>(entryIdx) != m_cachedPlayerIndex) {
            int playerLastLap = m_displayEntries[m_cachedPlayerIndex].lastLap;
            if (playerLastLap > 0 && entry.lastLap != playerLastLap) {
                entry.lastLapColorOverride = (entry.lastLap > playerLastLap)
                    ? this->getColor(ColorSlot::POSITIVE)   // slower than you → POSITIVE slot
                    : this->getColor(ColorSlot::NEGATIVE);  // faster than you → NEGATIVE slot
            }
        }

        // Format penalty as whole seconds (e.g., "+5s" for 5 second penalty)
        if (entry.penalty > 0) {
            int penaltySeconds = (entry.penalty + MS_TO_SEC_ROUNDING_OFFSET) / MS_TO_SEC_DIVISOR;
            snprintf(entry.formattedPenalty, sizeof(entry.formattedPenalty), "+%ds", penaltySeconds);
        } else {
            // No penalty - show generic placeholder
            strcpy_s(entry.formattedPenalty, sizeof(entry.formattedPenalty), Placeholders::GENERIC);
        }
    }

#if defined(MXBMRP3_TEST_BUILD)
    g_stFormatUs += stUsSince(stFormatStart);
    auto stNameAnimStart = StClock::now();
#endif

    // Apply name mode: truncate names for SHORT, calculate long width for LONG
    if (m_nameMode == NameMode::SHORT) {
        for (auto& entry : m_displayEntries) {
            if (!entry.isPlaceholder && static_cast<int>(strlen(entry.name)) > m_shortNameChars) {
                entry.name[m_shortNameChars] = '\0';  // Truncate to configured char count
            }
        }
    } else if (m_nameMode == NameMode::LONG) {
        // Static column width (m_longNameChars); names beyond it get the shared
        // ellipsis truncation. No longest-name scan, so the table doesn't reflow
        // as riders join/leave. SHORT mode above stays a deliberate hard cut.
        for (auto& entry : m_displayEntries) {
            if (!entry.isPlaceholder && static_cast<int>(strlen(entry.name)) > m_longNameChars) {
                std::string fitted = PluginUtils::fitText(entry.name, m_longNameChars);
                strncpy_s(entry.name, sizeof(entry.name), fitted.c_str(), _TRUNCATE);
            }
        }
    }

    // Update animation state (detect position changes, start/clean animations)
    updateAnimationState();

#if defined(MXBMRP3_TEST_BUILD)
    g_stNameAnimUs += stUsSince(stNameAnimStart);
    auto stLayoutStart = StClock::now();
#endif

    // Generate render data
    // Apply scale to all dimensions
    auto dim = getScaledDimensions();

    // IMPORTANT: Recalculate column positions and rebuild column table BEFORE calculating dimensions
    // This ensures m_cachedBackgroundWidth is updated before we create the background quad
    float contentStartX = START_X + dim.paddingH;
    int nameColWidth = getNameColumnWidth();
    m_columns = ColumnPositions(contentStartX, m_fScale, m_enabledColumns, nameColWidth, getRaceNumColumnWidth());
    buildColumnTable();  // Rebuild column table and cache width

    // Render all display entries (rider rows + gap rows)
    int rowsToRender = static_cast<int>(m_displayEntries.size());

    // Calculate dimensions based on actual rows that will be rendered
    auto hudDim = calculateHudDimensions(dim, rowsToRender);

    setBounds(START_X, START_Y, START_X + hudDim.backgroundWidth, START_Y + hudDim.backgroundHeight);

    addBackgroundQuad(START_X, START_Y, hudDim.backgroundWidth, hudDim.backgroundHeight);

    float currentY = hudDim.contentStartY;

    // Title: static "Standings" caption in the standard title style, toggled by
    // the shared title control. addTitleString keeps string index 0 stable
    // (emits an empty string when the title is hidden).
    addTitleString("Standings", hudDim.contentStartX, currentY, Justify::LEFT,
        this->getFont(FontCategory::TITLE), this->getColor(ColorSlot::PRIMARY), dim.fontSizeLarge);
    if (m_bShowTitle) currentY += dim.lineHeightLarge;

    // Session-info row: context-aware "<session>: <clock / leader lap / overtime>"
    // on a single line below the title (e.g. "Race 2: FINAL LAP"). The overtime
    // label ("N TO GO" / "FINAL LAP" / "CHECKERED") replaces the frozen 00:00 once
    // a time+lap clock expires. Always emitted (empty when disabled) so string
    // index 1 stays stable for the rebuildLayout fast path.
    char sessionInfoBuf[48] = "";
    if (m_bShowSessionInfo) {
        const char* sessionLabel = PluginUtils::getSessionString(sessionData.eventType, sessionData.session);
        if (!sessionLabel) sessionLabel = Placeholders::GENERIC;

        char value[24] = "";
        if (sessionData.sessionLength > 0) {
            // Timed (or timed+lap) session: live countdown, or the overtime label.
            PluginUtils::formatSessionClock(pluginData.getLeaderLapsToGo(),
                pluginData.getSessionTime(), value, sizeof(value));
        } else if (sessionData.sessionNumLaps > 0) {
            // Pure lap race: "CHECKERED" once the leader crosses the line on the final
            // lap; the session clock (a count-up elapsed timer for lap races) before the
            // race goes green; otherwise the leader's current lap / total laps. Reuse
            // isRiderFinished so the threshold matches the FinalLap/finished logic, and
            // so laps-only races read consistently with the time+lap overtime label.
            const StandingsData* leaderStanding = classificationOrder.empty()
                ? nullptr : pluginData.getStanding(classificationOrder[0]);
            bool leaderFinished = leaderStanding && sessionData.isRiderFinished(
                leaderStanding->numLaps, leaderStanding->numLapsAtLeaderFinish);
            bool raceInProgress = (sessionData.sessionState & SessionState::IN_PROGRESS) != 0;
            if (leaderFinished) {
                // Checked first so the post-race state (also "not in progress") keeps the
                // checkered label instead of falling back to the pre-race timer below.
                strcpy_s(value, sizeof(value), "CHECKERED");
            } else if (!raceInProgress) {
                // Pre-race (not yet green): show the session clock like timed races do,
                // instead of a static "Lap 1/N" before anyone has turned a lap.
                PluginUtils::formatSessionClock(pluginData.getLeaderLapsToGo(),
                    pluginData.getSessionTime(), value, sizeof(value));
            } else {
                int leaderLap = leaderStanding ? leaderStanding->numLaps + 1 : 1;  // numLaps = completed → 1-based current
                if (leaderLap < 1) leaderLap = 1;
                if (leaderLap > sessionData.sessionNumLaps) leaderLap = sessionData.sessionNumLaps;
                snprintf(value, sizeof(value), "Lap %d/%d", leaderLap, sessionData.sessionNumLaps);
            }
        }

        if (value[0] != '\0') {
            snprintf(sessionInfoBuf, sizeof(sessionInfoBuf), "%s: %s", sessionLabel, value);
        } else {
            snprintf(sessionInfoBuf, sizeof(sessionInfoBuf), "%s", sessionLabel);
        }
    }
    addString(sessionInfoBuf, hudDim.contentStartX, currentY, Justify::LEFT,
        this->getFont(FontCategory::TITLE), this->getColor(ColorSlot::PRIMARY), dim.fontSize);
    if (m_bShowSessionInfo) currentY += dim.lineHeightNormal;

    // Optional column-header row. Emits one string per enabled column (skipping the
    // status-icon column, which has no label), in the same column-table order the
    // per-row strings use so the rebuildLayout fast path can reposition them by index.
    if (m_bShowHeaders) {
        unsigned long headerColor = this->getColor(ColorSlot::TERTIARY);
        int headerFont = this->getFont(FontCategory::STRONG);
        for (const auto& col : m_columnTable) {
            if (col.columnIndex == COL_IDX_TRACKED) continue;
            int justify = Justify::LEFT;
            float textX = getColumnHeaderTextX(col.columnIndex, col.position, dim.fontSize, &justify);
            addLabel(getColumnHeaderLabel(col.columnIndex), textX, currentY, justify,
                headerFont, headerColor, dim);
        }
        currentY += hudDim.headerHeight;
    }

    // Clear and rebuild click regions for rider selection
    m_riderClickRegions.clear();

#if defined(MXBMRP3_TEST_BUILD)
    g_stLayoutUs += stUsSince(stLayoutStart);
    auto stRenderStart = StClock::now();
#endif

    // Render rows (no spacing between rows, consistent with other HUDs)
    for (int i = 0; i < rowsToRender; ++i) {
        const auto& entry = m_displayEntries[i];

        // Apply animation offset (slides row from old position to new position)
        float animOffset = (!entry.isPlaceholder && entry.raceNum >= 0)
            ? getAnimatedRowOffset(entry.raceNum, dim.lineHeightNormal) : 0.0f;
        float rowY = currentY + animOffset;

        // Colored animation mode: tint row positive/negative while animating.
        // Skipped on the player row while the row highlight is on (the default), so
        // the accent/brand background stays unobstructed (no crossfade, no flicker —
        // slide direction on the player row is conveyed by the row-position change itself).
        // The quad index is cached so rebuildLayout can update its position + alpha
        // each frame without forcing a full data rebuild.
        bool suppressSlideForPlayerRow = (m_bPlayerRowHighlight && i == m_cachedPlayerIndex);
        if (m_animationMode == AnimationMode::COLORED && !entry.isPlaceholder && entry.raceNum >= 0
                && !suppressSlideForPlayerRow) {
            float slideFade = getSlideFade(entry.raceNum);
            if (slideFade > 0.0f) {
                auto animIt = m_activeAnimations.find(entry.raceNum);
                bool promoted = (animIt != m_activeAnimations.end())
                    && (animIt->second.fromSlot > animIt->second.toSlot);
                unsigned long tintColor = promoted
                    ? this->getColor(ColorSlot::POSITIVE)
                    : this->getColor(ColorSlot::NEGATIVE);

                SPluginQuad_t slide;
                float slideX = START_X;
                float slideY = rowY;
                applyOffset(slideX, slideY);
                setQuadPositions(slide, slideX, slideY, hudDim.backgroundWidth, dim.lineHeightNormal);
                slide.m_iSprite = PluginConstants::SpriteIndex::SOLID_COLOR;
                slide.m_ulColor = PluginUtils::applyOpacity(tintColor, ROW_HIGHLIGHT_OPACITY * slideFade);
                m_slideHighlightQuads.push_back({m_quads.size(), i, entry.raceNum, promoted});
                m_quads.push_back(slide);
            }
        }

        // Skip highlights for placeholder rows
        if (!entry.isPlaceholder) {
            // Player/spectated row highlight (full-row background, accent color by
            // default, bike brand color via INI). On by default; when disabled via
            // INI the accent-colored name marker in renderRiderRow takes over.
            if (m_bPlayerRowHighlight && i == m_cachedPlayerIndex) {
                SPluginQuad_t highlight;
                float highlightX = START_X;
                float highlightY = rowY;
                applyOffset(highlightX, highlightY);
                setQuadPositions(highlight, highlightX, highlightY, hudDim.backgroundWidth, dim.lineHeightNormal);
                highlight.m_iSprite = PluginConstants::SpriteIndex::SOLID_COLOR;

                unsigned long highlightColor;
                if (m_bPlayerRowHighlightBrand) {
                    // Brand mode: use the bike's brand color, but fall back to the
                    // muted slot when the bike has no real brand mapping (all GPB/KRP
                    // bikes and brand-less MXB bikes resolve to the neutral gray
                    // sentinel) so the bar stays theme-aware instead of off-palette gray.
                    highlightColor = (entry.bikeBrandColor == PluginConstants::BrandColors::DEFAULT)
                        ? this->getColor(ColorSlot::MUTED)
                        : entry.bikeBrandColor;
                } else {
                    highlightColor = this->getColor(ColorSlot::ACCENT);
                }
                highlight.m_ulColor = PluginUtils::applyOpacity(highlightColor, ROW_HIGHLIGHT_OPACITY);

                m_cachedHighlightQuadIndex = static_cast<int>(m_quads.size());
                m_quads.push_back(highlight);
            }
            // Hover highlight for other riders (spectator mode only). Uses the muted
            // slot to stay visually distinct from the player's own accent highlight.
            else if (i == m_hoveredRowIndex && i != m_cachedPlayerIndex) {
                SPluginQuad_t hoverHighlight;
                float hoverX = START_X;
                float hoverY = rowY;
                applyOffset(hoverX, hoverY);
                setQuadPositions(hoverHighlight, hoverX, hoverY, hudDim.backgroundWidth, dim.lineHeightNormal);
                hoverHighlight.m_iSprite = PluginConstants::SpriteIndex::SOLID_COLOR;
                hoverHighlight.m_ulColor = PluginUtils::applyOpacity(
                    this->getColor(ColorSlot::MUTED), HOVER_HIGHLIGHT_OPACITY);
                m_quads.push_back(hoverHighlight);
            }
        }

        // Race number plate: quad behind number (primary color) + brand color strip
        // Layout within COL_RACENUM_WIDTH: [plate 4 chars][strip ~0.5 chars][padding ~0.5 chars]
        // Skipped in classic layout (no plates, no brand strip)
        if (isColumnEnabled(COL_RACENUM) && !entry.isPlaceholder && entry.raceNum >= 0 && !m_bClassicLayout) {
            PlateGeometry pg(dim.fontSize, dim.lineHeightNormal);

            // Number plate quad
            SPluginQuad_t numPlate;
            float npX = m_columns.raceNum, npY = rowY + pg.platePadY;
            applyOffset(npX, npY);
            setQuadPositions(numPlate, npX, npY, pg.plateWidth, pg.plateHeight);
            numPlate.m_iSprite = PluginConstants::SpriteIndex::SOLID_COLOR;

            // Determine plate color: podium colors for finished P1-P3, muted for DNS/DSQ/RET, primary otherwise
            bool isMutedRider = (entry.state == PluginConstants::RiderState::DNS ||
                                 entry.state == PluginConstants::RiderState::DSQ ||
                                 entry.state == PluginConstants::RiderState::RETIRED);
            unsigned long basePlateColor;
            if (isMutedRider) {
                basePlateColor = this->getColor(ColorSlot::MUTED);
            } else if (entry.trackedColor != 0) {
                // Tracked riders keep their custom plate colour (e.g. a red
                // points-leader plate) even when finishing on the podium; only the
                // muted (DNS/RET/DSQ) state takes precedence over it.
                basePlateColor = entry.trackedColor;
            } else if (entry.isFinishedRace && entry.position == 1) {
                basePlateColor = PluginConstants::PodiumColors::GOLD;
            } else if (entry.isFinishedRace && entry.position == 2) {
                basePlateColor = PluginConstants::PodiumColors::SILVER;
            } else if (entry.isFinishedRace && entry.position == 3) {
                basePlateColor = PluginConstants::PodiumColors::BRONZE;
            } else {
                // Default plate: secondary colour (dark number stays legible on it).
                basePlateColor = this->getColor(ColorSlot::SECONDARY);
            }
            unsigned long plateColor = PluginUtils::applyOpacity(basePlateColor, 230.0f / 255.0f);
            numPlate.m_ulColor = plateColor;

            size_t numPlateIdx = m_quads.size();
            m_quads.push_back(numPlate);

            // Brand color strip quad (right of plate with gap)
            SPluginQuad_t brandStrip;
            float bsLeftX = npX + pg.plateWidth + pg.stripGap;
            setQuadPositions(brandStrip, bsLeftX, npY, pg.brandStripWidth, pg.plateHeight);
            brandStrip.m_iSprite = PluginConstants::SpriteIndex::SOLID_COLOR;
            // Brand color always visible; dimmed for non-participants (DNS/RET/DSQ)
            float stripOpacity = isMutedRider ? 100.0f / 255.0f : 230.0f / 255.0f;
            unsigned long stripColor = PluginUtils::applyOpacity(entry.bikeBrandColor, stripOpacity);
            brandStrip.m_ulColor = stripColor;

            size_t brandStripIdx = m_quads.size();
            m_quads.push_back(brandStrip);

            m_raceNumPlateQuads.push_back({numPlateIdx, brandStripIdx, i});
        }

        renderRiderRow(entry, entry.isPlaceholder, rowY, dim, i);

        // Add click region for this rider so they can be hover-highlighted and
        // clicked to spectate — but only for riders actually on track. Anyone who
        // can't be spectated (DNS/DSQ/retired/unknown, e.g. left the server) or is
        // sitting in the pits gets no region, so they're neither highlighted on
        // hover nor clickable. This is the single chokepoint for both behaviors.
        bool isSpectatable = (entry.state == PluginConstants::RiderState::NORMAL) && (entry.pit != 1);
        if (!entry.isPlaceholder && entry.raceNum >= 0 && isSpectatable) {
            RiderClickRegion region;
            region.x = START_X;
            region.y = rowY;
            region.width = hudDim.backgroundWidth;
            region.height = dim.lineHeightNormal;
            region.raceNum = entry.raceNum;
            applyOffset(region.x, region.y);  // Apply drag offset to region
            m_riderClickRegions.push_back(region);
        }

        currentY += dim.lineHeightNormal;
    }

    // Restore m_enabledColumns to the profile-set value (we temporarily modified it for gap mode filtering)
    m_enabledColumns = savedEnabledColumns;

#if defined(MXBMRP3_TEST_BUILD)
    g_stRenderUs += stUsSince(stRenderStart);
    ++g_stCount;
#endif
}

void StandingsHud::handleClick(float mouseX, float mouseY) {
    // Check if click is within any rider row
    for (const auto& region : m_riderClickRegions) {
        if (isPointInRect(mouseX, mouseY, region.x, region.y, region.width, region.height)) {
            // Request to spectate this rider
            DEBUG_INFO_F("StandingsHud: Switching to rider #%d", region.raceNum);
            PluginManager::getInstance().requestSpectateRider(region.raceNum);
            return;  // Only process one click
        }
    }
}

void StandingsHud::resetToDefaults() {
    m_bVisible = true;
    m_bShowTitle = true;
    setTextureVariant(0);  // No texture by default
    m_fBackgroundOpacity = SettingsLimits::DEFAULT_OPACITY;
    m_fScale = 1.0f;
    setPosition(0.0055f, 0.30507f);
    m_gapMode = GapMode::ALL;
    m_gapReferenceMode = GapReferenceMode::PLAYER;
    m_posGainMode = PosGainMode::OFF;
    m_enabledColumns = COL_DEFAULT;
    m_nameMode = NameMode::SHORT;
    m_shortNameChars = DEFAULT_SHORT_NAME_CHARS;
    m_longNameChars = DEFAULT_LONG_NAME_CHARS;
    m_displayRowCount = DEFAULT_ROW_COUNT;
    m_topPositionsCount = DEFAULT_TOP_POSITIONS;
    m_bPlayerRowHighlight = true;
    m_bPlayerRowHighlightBrand = false;
    m_bClassicLayout = false;
    m_animationMode = AnimationMode::BASIC;
    m_animationDurationMs = 500.0f;
    m_bShowHeaders = false;
    m_bShowSessionInfo = true;
    m_bLiveGaps = false;
    m_activeAnimations.clear();
    m_previousPositions.clear();
    m_previousSlots.clear();
    m_cachedIconStates.clear();
    setDataDirty();
}


