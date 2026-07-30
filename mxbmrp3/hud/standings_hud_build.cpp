// ============================================================================
// hud/standings_hud_build.cpp
// StandingsHud::rebuildRenderData() — builds the display entries and formatted
// strings for the standings table (positions, gaps, lap times, penalties,
// chips), plus the test-build per-phase profiling counters the headless
// standings perf probe reads (standingsReadProfile / standingsReadTrackedUs).
// (Split from standings_hud.cpp; row layout stays there, per-row quad/string
//  emission is standings_hud_render.cpp, row animation standings_hud_animation.cpp.)
// ============================================================================
#include "standings_hud.h"
#include "standings_context_window.h"
#include "standings_gap_plan.h"
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
    // -> full rebuild every frame (defeating dirty-gating; the 480fps trap).
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

    m_cachedPlayerIndex = -1;

    // Show context when player is running OR when spectating/in replay
    int drawState = pluginData.getDrawState();
    bool shouldShowContext = pluginData.isPlayerRunning() ||
                             drawState == PluginConstants::ViewState::SPECTATE ||
                             drawState == PluginConstants::ViewState::REPLAY;

    // Pagination arithmetic (the two-sided clamp compensation) lives in
    // standings_context_window.h so it can be unit-tested without the game.
    const auto window = StandingsWindow::computeWindow(
        static_cast<int>(classificationOrder.size()), playerPositionInClassification,
        m_topPositionsCount, m_displayRowCount, shouldShowContext);

    m_displayEntries.reserve(window.totalRows());
    // Displayed position is the classification index + 1 (positions are 1-based).
    if (!window.top.empty()) {
        addDisplayEntries(window.top.startIndex, window.top.endIndex,
                          window.top.startIndex + 1, classificationOrder, pluginData);
    }
    if (!window.context.empty()) {
        addDisplayEntries(window.context.startIndex, window.context.endIndex,
                          window.context.startIndex + 1, classificationOrder, pluginData);
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

    // Gather the table-wide gap inputs once. Everything the gap decision needs
    // from PluginData is read HERE; StandingsGap::planGap() below sees only
    // these numbers (see standings_gap_plan.h).
    const StandingsData* playerStanding = pluginData.getStanding(displayRaceNum);
    // The leader (P1) has gap == 0 legitimately — their zero is valid data, not missing.
    const bool playerIsLeader = (!classificationOrder.empty() && classificationOrder[0] == displayRaceNum);

    // standings_gap_plan.h mirrors these three enums so it can stay free of
    // standings_hud.h (and therefore of BaseHud, PluginData and the game API)
    // while the casts here and below stay one instruction instead of a switch.
    // This is the enforcement for that mirror: renumber either side and this TU
    // stops compiling. They live INSIDE the member function because
    // DisplayEntry is private — at file scope the GapStyle half does not
    // compile, which the unit gate cannot catch (it never builds this TU).
    static_assert(static_cast<uint8_t>(GapReferenceMode::LEADER) ==
                  static_cast<uint8_t>(StandingsGap::Reference::LEADER), "gap reference enum drift");
    static_assert(static_cast<uint8_t>(GapReferenceMode::PLAYER) ==
                  static_cast<uint8_t>(StandingsGap::Reference::PLAYER), "gap reference enum drift");
    static_assert(static_cast<uint8_t>(GapReferenceMode::ALTERNATING) ==
                  static_cast<uint8_t>(StandingsGap::Reference::ALTERNATING), "gap reference enum drift");
    static_assert(static_cast<uint8_t>(GapMode::OFF) ==
                  static_cast<uint8_t>(StandingsGap::Scope::OFF), "gap scope enum drift");
    static_assert(static_cast<uint8_t>(GapMode::PLAYER) ==
                  static_cast<uint8_t>(StandingsGap::Scope::PLAYER), "gap scope enum drift");
    static_assert(static_cast<uint8_t>(GapMode::ADJACENT) ==
                  static_cast<uint8_t>(StandingsGap::Scope::ADJACENT), "gap scope enum drift");
    static_assert(static_cast<uint8_t>(GapMode::ALL) ==
                  static_cast<uint8_t>(StandingsGap::Scope::ALL), "gap scope enum drift");
    static_assert(static_cast<uint8_t>(DisplayEntry::GapStyle::OFFICIAL) ==
                  static_cast<uint8_t>(StandingsGap::Style::OFFICIAL), "gap style enum drift");
    static_assert(static_cast<uint8_t>(DisplayEntry::GapStyle::LIVE) ==
                  static_cast<uint8_t>(StandingsGap::Style::LIVE), "gap style enum drift");
    static_assert(static_cast<uint8_t>(DisplayEntry::GapStyle::LABEL) ==
                  static_cast<uint8_t>(StandingsGap::Style::LABEL), "gap style enum drift");
    // CARDINALITY, not just values. Without these, adding GapMode::TEAM = 4
    // compiles clean and static_cast<Scope>(4) then falls through planGap as ALL
    // — silently, which is the exact drift the mirror exists to prevent. The
    // value asserts above cannot see a new enumerator; a COUNT on both sides can.
    static_assert(static_cast<uint8_t>(GapReferenceMode::COUNT) ==
                  static_cast<uint8_t>(StandingsGap::Reference::COUNT), "gap reference enum gained a value on one side only");
    static_assert(static_cast<uint8_t>(GapMode::COUNT) ==
                  static_cast<uint8_t>(StandingsGap::Scope::COUNT), "gap scope enum gained a value on one side only");
    static_assert(static_cast<uint8_t>(DisplayEntry::GapStyle::COUNT) ==
                  static_cast<uint8_t>(StandingsGap::Style::COUNT), "gap style enum gained a value on one side only");

    StandingsGap::Table gapTable;
    gapTable.reference = static_cast<StandingsGap::Reference>(effectiveGapRef);
    gapTable.scope = static_cast<StandingsGap::Scope>(m_gapMode);
    gapTable.isRace = isRace;
    gapTable.liveGapsEnabled = useLiveGap;
    gapTable.playerRowIndex = m_cachedPlayerIndex;
    gapTable.playerIsLeader = playerIsLeader;
    gapTable.playerOfficialGap = (playerStanding && playerStanding->gap > 0) ? playerStanding->gap : 0;
    gapTable.playerGapLaps = playerStanding ? playerStanding->gapLaps : 0;
    gapTable.playerFinishTime = playerStanding ? playerStanding->finishTime : 0;
    gapTable.leaderFinishTime = sessionData.leaderFinishTime;
    // Player needs valid gap data for relative comparisons to be meaningful.
    gapTable.playerHasGapData = playerStanding &&
        (playerStanding->bestLap > 0 || playerStanding->gap > 0 ||
         playerStanding->gapLaps > 0 || playerIsLeader);
    // The player-relative live gap subtracts the player's realTimeGap as the
    // reference for every other rider, so it must be a FRESH value. If the player
    // (spectated/target rider) has dropped out of the current track-position batch
    // its realTimeGap is stale (the game only sends the ~10 closest vehicles) —
    // gate on hasActiveTrackPos exactly as the per-rider live path does, so a
    // stale reference doesn't skew every rider's gap. The leader is always valid.
    const bool playerLiveGapUsable = playerStanding &&
        (pluginData.hasActiveTrackPos(displayRaceNum) || playerIsLeader);
    gapTable.playerLiveGap = (playerLiveGapUsable && playerStanding->realTimeGap > 0)
        ? playerStanding->realTimeGap : 0;

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

        // Format gap column. The DECISION (which value, which style, which
        // tint) is StandingsGap::planGap in standings_gap_plan.h — pure and
        // unit-tested; this side only turns the plan into characters and
        // palette slots.
        bool isPlayerRow = (entry.raceNum == displayRaceNum);

        StandingsGap::Row gapRow;
        gapRow.index = static_cast<int>(entryIdx);
        gapRow.stateNormal = (entry.state == RiderState::NORMAL);
        gapRow.hasStateAbbr = (PluginUtils::getRiderStateAbbreviation(entry.state)[0] != '\0');
        gapRow.isLeaderRow = (entry.position == Position::FIRST);
        gapRow.isPlayerRow = isPlayerRow;
        gapRow.officialGap = entry.officialGap;
        gapRow.gapLaps = entry.gapLaps;
        gapRow.realTimeGap = entry.realTimeGap;
        gapRow.bestLap = entry.bestLap;
        gapRow.isFinished = sessionData.isRiderFinished(entry.numLaps, entry.numLapsAtLeaderFinish);
        gapRow.hasActiveTrackPos = pluginData.hasActiveTrackPos(entry.raceNum);

        const StandingsGap::Plan gapPlan = StandingsGap::planGap(gapTable, gapRow);

        entry.gapStyle = static_cast<DisplayEntry::GapStyle>(gapPlan.style);

        switch (gapPlan.kind) {
        case StandingsGap::Kind::Empty:
            entry.formattedGap[0] = '\0';
            break;
        case StandingsGap::Kind::StateAbbr:
            strcpy_s(entry.formattedGap, sizeof(entry.formattedGap),
                PluginUtils::getRiderStateAbbreviation(entry.state));
            break;
        case StandingsGap::Kind::Label:
            strcpy_s(entry.formattedGap, sizeof(entry.formattedGap),
                effectiveGapRef == GapReferenceMode::LEADER ? "Leader" : "Player");
            break;
        case StandingsGap::Kind::LapTime: {
            // Right-aligned like the numeric gaps.
            char tmp[16];
            PluginUtils::formatLapTime(gapPlan.value, tmp, sizeof(tmp));
            snprintf(entry.formattedGap, sizeof(entry.formattedGap), "%s", tmp);
            break;
        }
        case StandingsGap::Kind::TimeDiff:
            PluginUtils::formatTimeDiff(entry.formattedGap, sizeof(entry.formattedGap), gapPlan.value);
            break;
        case StandingsGap::Kind::LapDiff:
            snprintf(entry.formattedGap, sizeof(entry.formattedGap), "%+dL", gapPlan.value);
            break;
        case StandingsGap::Kind::Placeholder:
        default:
            strcpy_s(entry.formattedGap, sizeof(entry.formattedGap), Placeholders::GENERIC);
            break;
        }

        // Adjacent-mode tint. The plan says which side of the player the row is
        // on; the palette slot is chosen here so a theme change never reaches
        // the pure header.
        if (gapPlan.tint == StandingsGap::Tint::Ahead) {
            entry.gapColorOverride = this->getColor(ColorSlot::NEGATIVE);
        } else if (gapPlan.tint == StandingsGap::Tint::Behind) {
            entry.gapColorOverride = this->getColor(ColorSlot::POSITIVE);
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
            setQuadPositionsArrowRight(brandStrip, bsLeftX, npY + pg.arrowInsetY, pg.brandStripWidth, pg.arrowHeight);
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
        // The gate is PluginData's now, so Standings, Map, Event Log and Session Charts
        // cannot drift apart on what "spectatable" means (see isRiderSpectatable).
        if (!entry.isPlaceholder && pluginData.isRiderSpectatable(entry.raceNum)) {
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
