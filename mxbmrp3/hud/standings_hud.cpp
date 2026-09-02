// ============================================================================
// hud/standings_hud.cpp
// Displays race standings and lap times with position, gaps, and rider
// information. This TU: update()/rebuildLayout()/input/reset. The rest of the
// family: standings_hud_build.cpp (rebuildRenderData), standings_hud_render.cpp
// (per-row quad/string emission), standings_hud_animation.cpp (row animation).
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
        // setContentLayoutDirty, not setLayoutDirty: a slide moves ROWS inside a
        // panel that has not moved, and this runs every frame while it does.
        setContentLayoutDirty();
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
        // Through the helper: a themed band is nine quads, so the whole span has to
        // move, and it applies the offset itself.
        repositionRowHighlight(m_cachedHighlightQuadIndex, hudDim.contentStartX,
                               highlightY, hudDim.plan.contentW(),
                               dim.lineHeightNormal);
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
        float slideX = hudDim.contentStartX;
        applyOffset(slideX, slideY);
        setQuadPositions(m_quads[slide.quadIndex], slideX, slideY, hudDim.plan.contentW(), dim.lineHeightNormal);

        unsigned long tintColor = slide.promoted
            ? this->getColor(ColorSlot::POSITIVE)
            : this->getColor(ColorSlot::NEGATIVE);
        m_quads[slide.quadIndex].m_ulColor = PluginUtils::applyOpacity(tintColor, ROW_HIGHLIGHT_OPACITY * fade);
    }

    // Update tracked icon quad positions
    if (!m_trackedIconQuads.empty()) {
        // Get tracked column position from column table
        float trackedColPosition = m_columns.tracked;

        // Same size calculation as renderRiderRow -- through the SHARED ratio, not a
        // second copy of the number: a copy that drifts from the full rebuild's makes
        // every flag resize itself for the duration of a drag or a position-slide and
        // snap back on the next data rebuild.
        float spriteHalfSize = dim.fontSize * STATUS_ICON_HALF_RATIO;
        float spriteHalfWidth = spriteHalfSize / UI_ASPECT_RATIO;
        // Same anchor the full rebuild uses, so a drag cannot move the icon relative
        // to its column.
        float spriteCenterX = getColumnTextX(COL_IDX_TRACKED, trackedColPosition, dim.fontSize, false, false);

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
        // The shared ratio, for the same reason as the status flag above: "keep in sync
        // with renderRiderRow" is what a comment says right up until it isn't.
        float spriteHalfSize = dim.fontSize * POSGAIN_ICON_HALF_RATIO;
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

            // Update the brand mark (same helper as the build path, so the shape and
            // the sprite cannot diverge from it on a drag)
            SPluginQuad_t& brandStrip = m_quads[plate.brandQuadIndex];
            float bsLeftX = npX + pg.plateWidth + pg.stripGap;
            setBrandMarkQuad(brandStrip, bsLeftX, npY + pg.arrowInsetY, pg);
        }
    }

    // Update all string positions
    float currentY = hudDim.contentStartY;
    size_t stringIndex = 0;

    // Title string (index 0, always exists, but may be empty if hidden). Its row
    // is the PLAN's caption row — inside the band, above the content the plan
    // starts at — so it does not advance currentY.
    if (stringIndex < m_strings.size()) {
        positionString(stringIndex, hudDim.plan.X(hudDim.plan.g.captionX),
                       planTitleY(hudDim.plan));
        stringIndex++;
    }

    // Session-info string (index 1, always exists, but may be empty if disabled)
    if (stringIndex < m_strings.size()) {
        float x = hudDim.contentStartX;
        float y = currentY;
        positionString(stringIndex, x, y);
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
            positionString(stringIndex, x, y);
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
            positionString(stringIndex, x, y);
            stringIndex++;
        }

        currentY += dim.lineHeightNormal;
    }
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
    setPosition(cellsX(1), cellsY(26));
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


