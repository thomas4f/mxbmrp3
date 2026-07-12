// ============================================================================
// hud/stats_hud.cpp
// On-screen stats widget — shows session stats for current bike/track combo
// ============================================================================
#include "stats_hud.h"
#include "speed_widget.h"
#include "../core/plugin_data.h"
#include "../core/hud_manager.h"
#include "../core/stats_manager.h"
#include "../core/plugin_utils.h"
#include "../core/plugin_constants.h"
#include "../diagnostics/logger.h"

#include <cstdio>
#include <cmath>

using namespace PluginConstants;

StatsHud::StatsHud() {
    DEBUG_INFO("StatsHud created");
    m_strings.reserve(45);  // title + header row + up to 10 rows * 4 strings

    setDraggable(true);
    setTextureBaseName("stats_hud");
    resetToDefaults();
    rebuildRenderData();
}

bool StatsHud::handlesDataType(DataChangeType dataType) const {
    // No InputTelemetry — StatsHud displays accumulated stats, not raw telemetry.
    // Live distance/time refreshes are handled via checkFrequentUpdates() at ~1Hz.
    // SpectateTarget triggers a rebuild when the display rider changes so the
    // spectate-mode N/A overrides are applied immediately.
    return dataType == DataChangeType::LapLog ||
           dataType == DataChangeType::SessionData ||
           dataType == DataChangeType::SpectateTarget;
}

void StatsHud::update() {
    // "On Finish" auto-show: appear when the player finishes the race,
    // stay visible through pits, hide when re-entering the track
    if (m_visibilityMode == VisibilityMode::SESSION_END) {
        const auto& pd = PluginData::getInstance();
        // Use player's race number, not display rider (avoids triggering in spectate mode)
        int playerRaceNum = pd.getPlayerRaceNum();
        const StandingsData* playerStanding = (playerRaceNum >= 0) ? pd.getStanding(playerRaceNum) : nullptr;
        bool finished = playerStanding && pd.getSessionData().isRiderFinished(playerStanding->numLaps, playerStanding->numLapsAtLeaderFinish);
        bool running = pd.isPlayerRunning();
        if (finished && !m_finishAutoShown) {
            m_finishAutoShown = true;
            m_wasInPits = false;
            setVisible(true);
        } else if (m_finishAutoShown) {
            if (!running) {
                m_wasInPits = true;  // Entered pits after finishing
            } else if (m_wasInPits || !finished) {
                // Back on track after pits, or new session — hide
                m_finishAutoShown = false;
                m_wasInPits = false;
                setVisible(false);
            }
        }
    }

    if (isVisibleAnySurface()) {
        checkFrequentUpdates();
        processDirtyFlags();
    } else {
        clearDataDirty();
        clearLayoutDirty();
    }
}

bool StatsHud::needsFrequentUpdates() const {
    // Only need periodic ticks in ALWAYS mode for live distance/time updates
    return m_visibilityMode == VisibilityMode::ALWAYS;
}

int StatsHud::getTickIntervalMs() const {
    return STATS_TICK_INTERVAL_MS;  // ~1Hz — stats data changes slowly
}

const char* StatsHud::getVisibilityModeName(VisibilityMode mode) {
    switch (mode) {
        case VisibilityMode::ALWAYS:      return "Always";
        case VisibilityMode::SESSION_END: return "On Finish";
        default:                          return "Unknown";
    }
}

int StatsHud::getColumnCount() const {
    return (m_showLap ? 1 : 0) + (m_showSession ? 1 : 0) + (m_showAllTime ? 1 : 0);
}

int StatsHud::getRowCount() const {
    return DATA_ROWS + 1;  // a dedicated column-header row (below the title) + the data rows
}

bool StatsHud::computeLayout(Layout& out) const {
    out.cols = getColumnCount();
    if (out.cols == 0) return false;

    out.dim = getScaledDimensions();

    int rowCount = getRowCount();
    int colGaps = (out.cols > 1) ? (out.cols - 1) * COLUMN_GAP_CHARS : 0;
    int widthChars = LABEL_WIDTH_CHARS + out.cols * COLUMN_WIDTH_CHARS + colGaps;
    out.backgroundWidth = calculateBackgroundWidth(widthChars);
    out.titleHeight = m_bShowTitle ? out.dim.lineHeightLarge : 0.0f;
    float contentHeight = out.titleHeight + out.dim.lineHeightNormal * rowCount;
    out.backgroundHeight = out.dim.paddingV + contentHeight + out.dim.paddingV;

    out.contentStartX = out.dim.paddingH;
    out.contentStartY = out.dim.paddingV;

    float rightX = out.backgroundWidth - out.dim.paddingH;
    float colStride = PluginUtils::calculateMonospaceTextWidth(COLUMN_WIDTH_CHARS + COLUMN_GAP_CHARS, out.dim.fontSize);
    for (int i = 0; i < out.cols; i++) {
        out.col[out.cols - 1 - i] = rightX - i * colStride;
    }
    return true;
}

void StatsHud::rebuildLayout() {
    Layout lay;
    if (!computeLayout(lay)) {
        setBounds(0, 0, 0, 0);
        return;
    }

    setBounds(0, 0, lay.backgroundWidth, lay.backgroundHeight);
    updateBackgroundQuadPosition(0, 0, lay.backgroundWidth, lay.backgroundHeight);

    float currentY = lay.contentStartY;
    size_t stringIndex = 0;

    // Title
    positionString(stringIndex, lay.contentStartX, currentY);
    stringIndex++;
    currentY += lay.titleHeight;

    // Column headers — their own row below the title (like StandingsHud)
    float labelOffset = labelRowYOffset(lay.dim);  // Headers/row labels render at Small size, centered
    for (int i = 0; i < lay.cols; i++) {
        positionString(stringIndex++, lay.col[i], currentY + labelOffset);
    }
    currentY += lay.dim.lineHeightNormal;

    // Data rows: label + value per column
    for (int row = 0; row < DATA_ROWS; row++) {
        positionString(stringIndex++, lay.contentStartX, currentY + labelOffset);
        for (int i = 0; i < lay.cols; i++) {
            positionString(stringIndex++, lay.col[i], currentY);
        }
        currentY += lay.dim.lineHeightNormal;
    }
}

void StatsHud::rebuildRenderData() {
    clearStrings();
    m_quads.clear();

    Layout lay;
    if (!computeLayout(lay)) {
        setBounds(0, 0, 0, 0);  // Clear hit-test bounds
        return;
    }

    addBackgroundQuad(0, 0, lay.backgroundWidth, lay.backgroundHeight);
    setBounds(0, 0, lay.backgroundWidth, lay.backgroundHeight);

    float currentY = lay.contentStartY;

    unsigned long labelColor = this->getColor(ColorSlot::TERTIARY);
    unsigned long valueColor = this->getColor(ColorSlot::SECONDARY);
    unsigned long mutedColor = this->getColor(ColorSlot::MUTED);
    unsigned long primaryColor = this->getColor(ColorSlot::PRIMARY);

    // Spectator detection: when the display rider isn't the local player,
    // StatsManager's player-only data doesn't apply. We still surface what we
    // can from per-rider sources (StandingsData + per-rider crash counter);
    // the rest falls back to "N/A" muted, matching the TelemetryHud convention.
    const PluginData& pd = PluginData::getInstance();
    int playerRaceNum = pd.getPlayerRaceNum();
    int displayRaceNum = pd.getDisplayRaceNum();
    const bool isSpectating = (displayRaceNum >= 0 && displayRaceNum != playerRaceNum);

    // Get stats from StatsManager. All-time stats only apply to the local
    // player, so suppress the source fetch entirely when spectating.
    const StatsManager& stats = StatsManager::getInstance();
    const TrackBikeStats* tbStats = (m_showAllTime && !isSpectating) ? stats.getTrackBikeStats() : nullptr;

    // Speed unit preference (guard for early construction before HudManager is fully initialized)
    bool useKmh = false;
    const auto& hm = HudManager::getInstance();
    if (hm.isInitialized()) {
        useKmh = hm.getSpeedWidget().getSpeedUnit() == SpeedWidget::SpeedUnit::KMH;
    }
    float speedFactor = useKmh ? UnitConversion::MS_TO_KMH : UnitConversion::MS_TO_MPH;
    const char* speedLabel = useKmh ? "km/h" : "mph";

    // Build column header names
    const char* headerNames[3];
    int headerIdx = 0;
    if (m_showLap)     headerNames[headerIdx++] = "Last";
    if (m_showSession) headerNames[headerIdx++] = "Session";
    if (m_showAllTime) headerNames[headerIdx++] = "All-time";

    // Title
    addTitleString("Stats", lay.contentStartX, currentY, Justify::LEFT,
        this->getFont(FontCategory::TITLE), primaryColor, lay.dim.fontSizeLarge);
    currentY += lay.titleHeight;

    // Column headers — their own row below the title (like StandingsHud)
    for (int i = 0; i < lay.cols; i++) {
        addLabel(headerNames[i], lay.col[i], currentY, Justify::RIGHT,
            this->getFont(FontCategory::STRONG), labelColor, lay.dim);
    }
    currentY += lay.dim.lineHeightNormal;

    // Helper: add a row with label + one value per enabled column
    struct ColValue { const char* text; unsigned long color; };

    auto addRow = [&](const char* label, ColValue lap, ColValue session, ColValue allTime) {
        addLabel(label, lay.contentStartX, currentY, Justify::LEFT,
            this->getFont(FontCategory::STRONG), labelColor, lay.dim);

        int ci = 0;
        if (m_showLap) {
            addString(lap.text, lay.col[ci], currentY, Justify::RIGHT,
                this->getFont(FontCategory::DIGITS), lap.color, lay.dim.fontSize);
            ci++;
        }
        if (m_showSession) {
            addString(session.text, lay.col[ci], currentY, Justify::RIGHT,
                this->getFont(FontCategory::DIGITS), session.color, lay.dim.fontSize);
            ci++;
        }
        if (m_showAllTime) {
            addString(allTime.text, lay.col[ci], currentY, Justify::RIGHT,
                this->getFont(FontCategory::DIGITS), allTime.color, lay.dim.fontSize);
            ci++;
        }
        currentY += lay.dim.lineHeightNormal;
    };

    // Helper: format avg speed from distance (meters) and time (ms)
    auto formatAvgSpeed = [speedFactor, speedLabel](double distM, int64_t timeMs, char* buf, size_t bufSize) -> bool {
        if (distM > 0.0 && timeMs > 0) {
            double avgMs = distM / (timeMs / 1000.0);  // m/s
            snprintf(buf, bufSize, "%.0f %s", avgMs * speedFactor, speedLabel);
            return true;
        }
        snprintf(buf, bufSize, "%s", Placeholders::GENERIC);
        return false;
    };

    // ---- Format last completed lap values ----
    char lTimeBuf[16], lDistBuf[32], lCrashBuf[8], lShiftBuf[8];
    char lPenCntBuf[8], lPenTimeBuf[16], lSpeedBuf[24], lAvgBuf[24];
    unsigned long lPenCntColor = valueColor, lPenTimeColor = valueColor;
    unsigned long lSpeedColor = valueColor, lAvgColor = valueColor;

    bool hasLast = stats.hasLastLapData();
    if (hasLast) {
        int lastLapMs = stats.getLastLapTimeMs();
        PluginUtils::formatDuration(static_cast<int64_t>(lastLapMs), lTimeBuf, sizeof(lTimeBuf));

        double lastDist = stats.getLastLapDistance();
        PluginUtils::formatDistance(lastDist, lDistBuf, sizeof(lDistBuf));

        snprintf(lCrashBuf, sizeof(lCrashBuf), "%d", stats.getLastLapCrashes());
        snprintf(lShiftBuf, sizeof(lShiftBuf), "%d", stats.getLastLapGearShifts());

        int lastPenCnt = stats.getLastLapPenaltyCount();
        snprintf(lPenCntBuf, sizeof(lPenCntBuf), "%d", lastPenCnt);
        if (lastPenCnt == 0) lPenCntColor = mutedColor;

        int64_t lastPenMs = stats.getLastLapPenaltyTimeMs();
        if (lastPenMs > 0) {
            snprintf(lPenTimeBuf, sizeof(lPenTimeBuf), "%ds", static_cast<int>((lastPenMs + 500) / 1000));
        } else {
            snprintf(lPenTimeBuf, sizeof(lPenTimeBuf), "%s", Placeholders::GENERIC);
            lPenTimeColor = mutedColor;
        }

        float lastTopMs = stats.getLastLapTopSpeedMs();
        if (lastTopMs > 0.0f) {
            snprintf(lSpeedBuf, sizeof(lSpeedBuf), "%.0f %s", lastTopMs * speedFactor, speedLabel);
        } else {
            snprintf(lSpeedBuf, sizeof(lSpeedBuf), "%s", Placeholders::GENERIC);
            lSpeedColor = mutedColor;
        }

        if (!formatAvgSpeed(lastDist, static_cast<int64_t>(lastLapMs), lAvgBuf, sizeof(lAvgBuf)))
            lAvgColor = mutedColor;
    } else {
        // No completed lap yet — all placeholders
        snprintf(lTimeBuf, sizeof(lTimeBuf), "%s", Placeholders::GENERIC);
        snprintf(lDistBuf, sizeof(lDistBuf), "%s", Placeholders::GENERIC);
        snprintf(lCrashBuf, sizeof(lCrashBuf), "%s", Placeholders::GENERIC);
        snprintf(lShiftBuf, sizeof(lShiftBuf), "%s", Placeholders::GENERIC);
        snprintf(lPenCntBuf, sizeof(lPenCntBuf), "%s", Placeholders::GENERIC);
        snprintf(lPenTimeBuf, sizeof(lPenTimeBuf), "%s", Placeholders::GENERIC);
        snprintf(lSpeedBuf, sizeof(lSpeedBuf), "%s", Placeholders::GENERIC);
        snprintf(lAvgBuf, sizeof(lAvgBuf), "%s", Placeholders::GENERIC);
        lPenCntColor = mutedColor;
        lPenTimeColor = mutedColor;
        lSpeedColor = mutedColor;
        lAvgColor = mutedColor;
    }

    // ---- Format session values ----
    char sBestBuf[16], sLapsBuf[8], sTimeBuf[16], sDistBuf[32], sCrashBuf[8], sShiftBuf[8];
    char sPenCntBuf[8], sPenTimeBuf[16], sSpeedBuf[24], sAvgBuf[24];
    unsigned long sBestColor = valueColor, sPenCntColor = valueColor, sPenTimeColor = valueColor;
    unsigned long sSpeedColor = valueColor, sAvgColor = valueColor;
    // Spectate-mode overrideable colors for the rows that used to hardcode valueColor.
    unsigned long sLapsColor = valueColor, sTimeColor = valueColor, sDistColor = valueColor;
    unsigned long sCrashColor = valueColor, sShiftColor = valueColor;

    int sessionBestLapMs = m_showSession ? stats.getSessionBestLapMs() : 0;
    if (sessionBestLapMs > 0) {
        PluginUtils::formatLapTime(sessionBestLapMs, sBestBuf, sizeof(sBestBuf));
    } else {
        snprintf(sBestBuf, sizeof(sBestBuf), "%s", Placeholders::GENERIC);
        sBestColor = mutedColor;
    }
    snprintf(sLapsBuf, sizeof(sLapsBuf), "%d", m_showSession ? stats.getSessionLaps() : 0);
    int64_t sessionDurMs = m_showSession ? stats.getSessionDurationMs() : 0;
    PluginUtils::formatDuration(sessionDurMs, sTimeBuf, sizeof(sTimeBuf));
    double sessionDistM = m_showSession ? stats.getSessionTripDistance() : 0.0;
    PluginUtils::formatDistance(sessionDistM, sDistBuf, sizeof(sDistBuf));
    snprintf(sCrashBuf, sizeof(sCrashBuf), "%d", m_showSession ? stats.getSessionCrashes() : 0);
    snprintf(sShiftBuf, sizeof(sShiftBuf), "%d", m_showSession ? stats.getSessionGearShifts() : 0);

    int sessionPenCnt = m_showSession ? stats.getSessionPenaltyCount() : 0;
    snprintf(sPenCntBuf, sizeof(sPenCntBuf), "%d", sessionPenCnt);
    if (sessionPenCnt == 0) sPenCntColor = mutedColor;

    int64_t sessionPenMs = m_showSession ? stats.getSessionPenaltyTimeMs() : 0;
    if (sessionPenMs > 0) {
        snprintf(sPenTimeBuf, sizeof(sPenTimeBuf), "%llds", (long long)(sessionPenMs + 500) / 1000);
    } else {
        snprintf(sPenTimeBuf, sizeof(sPenTimeBuf), "%s", Placeholders::GENERIC);
        sPenTimeColor = mutedColor;
    }

    float sessionTopMs = m_showSession ? stats.getSessionTopSpeedMs() : 0.0f;
    if (sessionTopMs > 0.0f) {
        snprintf(sSpeedBuf, sizeof(sSpeedBuf), "%.0f %s", sessionTopMs * speedFactor, speedLabel);
    } else {
        snprintf(sSpeedBuf, sizeof(sSpeedBuf), "%s", Placeholders::GENERIC);
        sSpeedColor = mutedColor;
    }

    if (!formatAvgSpeed(sessionDistM, sessionDurMs, sAvgBuf, sizeof(sAvgBuf)))
        sAvgColor = mutedColor;

    // ---- Format all-time values ----
    char aBestBuf[16], aLapsBuf[8], aTimeBuf[16], aDistBuf[32], aCrashBuf[8], aShiftBuf[8];
    char aPenCntBuf[8], aPenTimeBuf[16], aSpeedBuf[24], aAvgBuf[24];
    unsigned long aBestColor = valueColor, aPenCntColor = valueColor, aPenTimeColor = valueColor;
    unsigned long aSpeedColor = valueColor;
    // Spectate-mode overrideable colors for the rows that used to hardcode valueColor.
    unsigned long aLapsColor = valueColor, aTimeColor = valueColor, aDistColor = valueColor;
    unsigned long aCrashColor = valueColor, aShiftColor = valueColor;
    // Average speed is never tracked all-time (no per-session distance persistence)
    // even for the player, so it starts as GENERIC muted regardless of mode.
    snprintf(aAvgBuf, sizeof(aAvgBuf), "%s", Placeholders::GENERIC);
    unsigned long aAvgColor = mutedColor;

    if (tbStats && tbStats->bestLapTimeMs > 0) {
        PluginUtils::formatLapTime(tbStats->bestLapTimeMs, aBestBuf, sizeof(aBestBuf));
    } else {
        snprintf(aBestBuf, sizeof(aBestBuf), "%s", Placeholders::GENERIC);
        aBestColor = mutedColor;
    }
    snprintf(aLapsBuf, sizeof(aLapsBuf), "%d", tbStats ? tbStats->validLaps : 0);
    int64_t allTimeMs = stats.getCurrentTotalTimeOnTrackMs();
    PluginUtils::formatDuration(allTimeMs, aTimeBuf, sizeof(aTimeBuf));
    double allTimeDistM = m_showAllTime ? stats.getCurrentTotalDistanceM() : 0.0;
    PluginUtils::formatDistance(allTimeDistM, aDistBuf, sizeof(aDistBuf));
    snprintf(aCrashBuf, sizeof(aCrashBuf), "%d", tbStats ? tbStats->crashCount : 0);
    snprintf(aShiftBuf, sizeof(aShiftBuf), "%d", tbStats ? tbStats->gearShiftCount : 0);

    int allTimePenCnt = tbStats ? tbStats->penaltyCount : 0;
    snprintf(aPenCntBuf, sizeof(aPenCntBuf), "%d", allTimePenCnt);
    if (allTimePenCnt == 0) aPenCntColor = mutedColor;

    int64_t allTimePenMs = tbStats ? tbStats->penaltyTimeMs : 0;
    if (allTimePenMs > 0) {
        snprintf(aPenTimeBuf, sizeof(aPenTimeBuf), "%llds", (long long)(allTimePenMs + 500) / 1000);
    } else {
        snprintf(aPenTimeBuf, sizeof(aPenTimeBuf), "%s", Placeholders::GENERIC);
        aPenTimeColor = mutedColor;
    }

    if (tbStats && tbStats->topSpeedMs > 0.0f) {
        snprintf(aSpeedBuf, sizeof(aSpeedBuf), "%.0f %s", tbStats->topSpeedMs * speedFactor, speedLabel);
    } else {
        snprintf(aSpeedBuf, sizeof(aSpeedBuf), "%s", Placeholders::GENERIC);
        aSpeedColor = mutedColor;
    }

    // ---- Spectator mode overrides ----
    // When we're displaying a non-player rider, most of the StatsManager-sourced
    // values above are meaningless (they belong to the local player). Replace
    // them with N/A muted, and populate the cells we *can* derive from per-rider
    // data: best lap, laps, crash count, and penalty time (all from
    // StandingsData + per-rider crash counter).
    if (isSpectating) {
        // Last-lap column: no lap-level data is tracked for other riders.
        snprintf(lTimeBuf,    sizeof(lTimeBuf),    "%s", Placeholders::NOT_AVAILABLE);
        snprintf(lDistBuf,    sizeof(lDistBuf),    "%s", Placeholders::NOT_AVAILABLE);
        snprintf(lCrashBuf,   sizeof(lCrashBuf),   "%s", Placeholders::NOT_AVAILABLE);
        snprintf(lShiftBuf,   sizeof(lShiftBuf),   "%s", Placeholders::NOT_AVAILABLE);
        snprintf(lPenCntBuf,  sizeof(lPenCntBuf),  "%s", Placeholders::NOT_AVAILABLE);
        snprintf(lPenTimeBuf, sizeof(lPenTimeBuf), "%s", Placeholders::NOT_AVAILABLE);
        snprintf(lSpeedBuf,   sizeof(lSpeedBuf),   "%s", Placeholders::NOT_AVAILABLE);
        snprintf(lAvgBuf,     sizeof(lAvgBuf),     "%s", Placeholders::NOT_AVAILABLE);
        hasLast = false;  // propagates mutedColor to the hasLast-gated cells
        lPenCntColor  = mutedColor;
        lPenTimeColor = mutedColor;
        lSpeedColor   = mutedColor;
        lAvgColor     = mutedColor;

        // Session column: populate from per-rider sources where possible.
        if (m_showSession) {
            const StandingsData* spec = pd.getStanding(displayRaceNum);

            // Best lap — from standings
            if (spec && spec->bestLap > 0) {
                PluginUtils::formatLapTime(spec->bestLap, sBestBuf, sizeof(sBestBuf));
                sBestColor = valueColor;
            } else {
                snprintf(sBestBuf, sizeof(sBestBuf), "%s", Placeholders::NOT_AVAILABLE);
                sBestColor = mutedColor;
            }

            // Laps — from standings
            if (spec) {
                snprintf(sLapsBuf, sizeof(sLapsBuf), "%d", spec->numLaps);
                sLapsColor = valueColor;
            } else {
                snprintf(sLapsBuf, sizeof(sLapsBuf), "%s", Placeholders::NOT_AVAILABLE);
                sLapsColor = mutedColor;
            }

            // Crashes — from per-rider edge-detection counter
            snprintf(sCrashBuf, sizeof(sCrashBuf), "%d", pd.getRiderSessionCrashCount(displayRaceNum));
            sCrashColor = valueColor;

            // Penalty time — from standings
            if (spec && spec->penalty > 0) {
                snprintf(sPenTimeBuf, sizeof(sPenTimeBuf), "%ds", static_cast<int>((spec->penalty + 500) / 1000));
                sPenTimeColor = valueColor;
            } else {
                snprintf(sPenTimeBuf, sizeof(sPenTimeBuf), "%s", Placeholders::NOT_AVAILABLE);
                sPenTimeColor = mutedColor;
            }

            // Not available per-rider: ride time, distance, shifts,
            // penalty count (standings only carries cumulative time), top
            // speed, avg speed.
            snprintf(sTimeBuf,   sizeof(sTimeBuf),   "%s", Placeholders::NOT_AVAILABLE);
            sTimeColor = mutedColor;
            snprintf(sDistBuf,   sizeof(sDistBuf),   "%s", Placeholders::NOT_AVAILABLE);
            sDistColor = mutedColor;
            snprintf(sShiftBuf,  sizeof(sShiftBuf),  "%s", Placeholders::NOT_AVAILABLE);
            sShiftColor = mutedColor;
            snprintf(sPenCntBuf, sizeof(sPenCntBuf), "%s", Placeholders::NOT_AVAILABLE);
            sPenCntColor = mutedColor;
            snprintf(sSpeedBuf,  sizeof(sSpeedBuf),  "%s", Placeholders::NOT_AVAILABLE);
            sSpeedColor = mutedColor;
            snprintf(sAvgBuf,    sizeof(sAvgBuf),    "%s", Placeholders::NOT_AVAILABLE);
            sAvgColor = mutedColor;
        }

        // All-time column: cross-session stats only exist for the local player.
        if (m_showAllTime) {
            snprintf(aBestBuf,    sizeof(aBestBuf),    "%s", Placeholders::NOT_AVAILABLE);
            aBestColor = mutedColor;
            snprintf(aLapsBuf,    sizeof(aLapsBuf),    "%s", Placeholders::NOT_AVAILABLE);
            aLapsColor = mutedColor;
            snprintf(aTimeBuf,    sizeof(aTimeBuf),    "%s", Placeholders::NOT_AVAILABLE);
            aTimeColor = mutedColor;
            snprintf(aDistBuf,    sizeof(aDistBuf),    "%s", Placeholders::NOT_AVAILABLE);
            aDistColor = mutedColor;
            snprintf(aCrashBuf,   sizeof(aCrashBuf),   "%s", Placeholders::NOT_AVAILABLE);
            aCrashColor = mutedColor;
            snprintf(aShiftBuf,   sizeof(aShiftBuf),   "%s", Placeholders::NOT_AVAILABLE);
            aShiftColor = mutedColor;
            snprintf(aPenCntBuf,  sizeof(aPenCntBuf),  "%s", Placeholders::NOT_AVAILABLE);
            aPenCntColor = mutedColor;
            snprintf(aPenTimeBuf, sizeof(aPenTimeBuf), "%s", Placeholders::NOT_AVAILABLE);
            aPenTimeColor = mutedColor;
            snprintf(aSpeedBuf,   sizeof(aSpeedBuf),   "%s", Placeholders::NOT_AVAILABLE);
            aSpeedColor = mutedColor;
            snprintf(aAvgBuf,     sizeof(aAvgBuf),     "%s", Placeholders::NOT_AVAILABLE);
            aAvgColor = mutedColor;
        }
    }

    // ---- Emit rows with descriptive labels ----
    addRow("Best lap",   {Placeholders::GENERIC, mutedColor},            {sBestBuf,    sBestColor},    {aBestBuf,    aBestColor});
    addRow("Laps",       {Placeholders::GENERIC, mutedColor},            {sLapsBuf,    sLapsColor},    {aLapsBuf,    aLapsColor});
    addRow("Ride time",  {lTimeBuf,  hasLast ? valueColor : mutedColor}, {sTimeBuf,    sTimeColor},    {aTimeBuf,    aTimeColor});
    addRow("Distance",   {lDistBuf,  hasLast ? valueColor : mutedColor}, {sDistBuf,    sDistColor},    {aDistBuf,    aDistColor});
    addRow("Crashes",    {lCrashBuf, hasLast ? valueColor : mutedColor}, {sCrashBuf,   sCrashColor},   {aCrashBuf,   aCrashColor});
    addRow("Shifts",     {lShiftBuf, hasLast ? valueColor : mutedColor}, {sShiftBuf,   sShiftColor},   {aShiftBuf,   aShiftColor});
    addRow("Penalties",  {lPenCntBuf,  lPenCntColor},                    {sPenCntBuf,  sPenCntColor},  {aPenCntBuf,  aPenCntColor});
    addRow("Pen. time",  {lPenTimeBuf, lPenTimeColor},                   {sPenTimeBuf, sPenTimeColor}, {aPenTimeBuf, aPenTimeColor});
    addRow("Top speed",  {lSpeedBuf,   lSpeedColor},                     {sSpeedBuf,   sSpeedColor},   {aSpeedBuf,   aSpeedColor});
    addRow("Avg speed",  {lAvgBuf,     lAvgColor},                       {sAvgBuf,     sAvgColor},     {aAvgBuf,     aAvgColor});
}

void StatsHud::resetToDefaults() {
    m_bVisible = false;
    m_bShowTitle = true;
    setTextureVariant(0);
    m_fBackgroundOpacity = 0.80f;
    m_fScale = 1.0f;
    m_visibilityMode = VisibilityMode::ALWAYS;
    m_showLap = true;
    m_showSession = true;
    m_showAllTime = false;
    setPosition(0.7315f, 0.62188f);
    setDataDirty();
}
