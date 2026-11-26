// ============================================================================
// hud/deck_hud_laplog.cpp
// The Lap Log deck (deck_hud.h): the last laps, oldest on top, and the lap
// running at the foot, each a row of the layout's row template (deck_layout.h,
// Deck::rows): its number, three sectors and its time. The third sector is the
// rest of the lap, so a game timing four (GP Bikes) folds its last two into it.
// A sector or lap that is your best is lit, and a lap that sets nothing
// (struck, or through the pits) muted; the running lap's sectors read good or
// bad against your best ones, as they are crossed, and its time against the
// reference lap at this point (the Lap Log HUD's gap row). Logged times are
// in hundredths: in thousandths a monospaced face overflows the columns.
// ============================================================================
#include "deck_hud.h"
#include "deck_format.h"
#include "lap_log_plan.h"

#include <algorithm>
#include <cstdio>
#include <deque>

#include "../core/i18n.h"
#include "../core/plugin_data.h"
#include "../core/plugin_utils.h"
#include "../core/plugin_constants.h"

using namespace PluginConstants;
using namespace DeckLayout;
using namespace DeckFormat;

namespace {

// A row's cells, in the order of its fields
constexpr int F_LAP = ROW_LAP - ROW_FIELD_FIRST;
constexpr int F_S1 = ROW_S1 - ROW_FIELD_FIRST;
constexpr int F_S2 = ROW_S2 - ROW_FIELD_FIRST;
constexpr int F_S3 = ROW_S3 - ROW_FIELD_FIRST;
constexpr int F_TIME = ROW_TIME - ROW_FIELD_FIRST;
constexpr int F_ME = ROW_ME - ROW_FIELD_FIRST;

// The rest of the lap after the second sector: one sector, or two on a game
// timing four; -1 when any part of it is missing
int lastSector(int s3, int s4) {
#if GAME_SECTOR_COUNT >= 4
    return s3 > 0 && s4 > 0 ? s3 + s4 : -1;
#else
    (void)s4;
    return s3;
#endif
}

void sectorCell(char* text, size_t n, uint8_t& tone, int ms, int best) {
    if (ms <= 0) {
        snprintf(text, n, "%s", Placeholders::GENERIC);
        tone = TONE_EMPTY;
        return;
    }
    formatSector(text, n, ms);
    if (best > 0 && ms <= best) tone = TONE_BEST;
}

}  // namespace

void DeckHud::gatherLapLog(int rows) {
    const PluginData& pd = PluginData::getInstance();
    setText(TITLE, title(Kind::LapLog));

    const std::deque<LapLogEntry>* log = pd.getLapLog();
    const LapLogEntry* bestEntry = pd.getBestLapEntry();
    const IdealLapData* ideal = pd.getIdealLapData();
    const int bestLap = bestEntry && bestEntry->isComplete ? bestEntry->lapTime : -1;
    const int bestS1 = ideal ? ideal->bestSector1 : -1;
    const int bestS2 = ideal ? ideal->bestSector2 : -1;
    const int bestS3 = ideal ? lastSector(ideal->bestSector3, ideal->bestSector4) : -1;
    const bool shortForm = pd.isShortTimeFormat();

    // The lap running, as the Lap Log HUD shows it (LapLogPlan::runningLapShown)
    const bool pitLap = pd.isLapViaPits(pd.getDisplayRaceNum());
    const bool running = LapLogPlan::runningLapShown(pd.isLapTimerValid(), pitLap, pd.isDisplayRiderFinished());

    rows = std::clamp(rows, 0, MAX_ROWS);
    const int logged = log ? static_cast<int>(log->size()) : 0;
    const int done = std::min(logged, std::max(0, rows - (running ? 1 : 0)));
    const int n = rows > 0 ? std::max(done + (running ? 1 : 0), 1) : 0;
    m_rows.rowCount = n;
    m_rows.rowBreakAt = -1;

    for (int r = 0; r < n; ++r) {
        char (&text)[ROW_FIELD_COUNT][32] = m_rows.rowText[r];
        uint8_t (&tone)[ROW_FIELD_COUNT] = m_rows.rowTone[r];
        for (int f = 0; f < ROW_FIELD_COUNT; ++f) {
            text[f][0] = '\0';
            tone[f] = TONE_NORMAL;
        }
        m_rows.rowNum[r] = r;
        const size_t len = sizeof(text[0]);

        if (r < done) {
            // A lap logged: the log is newest first, the rows oldest on top
            const LapLogEntry& e = (*log)[static_cast<size_t>(done - 1 - r)];
            snprintf(text[F_LAP], len, "L%d", e.lapNum + 1);
            // As the Lap Log HUD writes it: a lap with a time shows it, one
            // without says why (through the pits, or struck out)
            const bool timed = e.lapTime > 0 && e.isComplete;
            const bool counts = timed && e.isValid;
            sectorCell(text[F_S1], len, tone[F_S1], e.sector1, counts ? bestS1 : -1);
            sectorCell(text[F_S2], len, tone[F_S2], e.sector2, counts ? bestS2 : -1);
            sectorCell(text[F_S3], len, tone[F_S3], lastSector(e.sector3, e.sector4), counts ? bestS3 : -1);
            if (timed) {
                formatLapHundredths(text[F_TIME], len, e.lapTime, shortForm);
                if (counts && bestLap > 0 && e.lapTime <= bestLap) tone[F_TIME] = TONE_BEST;
            } else {
                snprintf(text[F_TIME], len, "%s", Placeholders::untimedLap(e.viaPits, e.isValid));
            }
            // A lap that sets nothing (struck, through the pits, untimed):
            // every cell muted
            if (!counts) for (int f = 0; f < ROW_FIELD_COUNT; ++f) tone[f] = TONE_EMPTY;
            continue;
        }

        if (!running) {
            // Nothing logged and nothing running: one empty row
            snprintf(text[F_LAP], len, "%s", Placeholders::GENERIC);
            snprintf(text[F_S1], len, "%s", Placeholders::GENERIC);
            snprintf(text[F_S2], len, "%s", Placeholders::GENERIC);
            snprintf(text[F_S3], len, "%s", Placeholders::GENERIC);
            snprintf(text[F_TIME], len, "%s", Placeholders::GENERIC);
            for (int f = 0; f < ROW_FIELD_COUNT; ++f) tone[f] = TONE_EMPTY;
            continue;
        }

        // The lap running: lit, its crossed sectors good or bad against your
        // best, the last still to come
        snprintf(text[F_ME], len, "%s", "1");
        snprintf(text[F_LAP], len, "L%d", pd.getLapTimerCurrentLap() + 1);
        tone[F_LAP] = TONE_BEST;
        const CurrentLapData* cur = pd.getCurrentLapData();
        const int s1 = cur && cur->split1 > 0 ? cur->split1 : -1;
        const int s2 = s1 > 0 && cur->split2 > s1 ? cur->split2 - s1 : -1;
        const int sector[2] = { s1, s2 };
        const int best[2] = { bestS1, bestS2 };
        const int field[2] = { F_S1, F_S2 };
        for (int i = 0; i < 2; ++i) {
            if (sector[i] <= 0) {
                snprintf(text[field[i]], len, "%s", Placeholders::GENERIC);
                tone[field[i]] = TONE_EMPTY;
                continue;
            }
            formatSector(text[field[i]], len, sector[i]);
            if (best[i] > 0) tone[field[i]] = sector[i] <= best[i] ? TONE_GOOD : TONE_BAD;
        }
        snprintf(text[F_S3], len, "%s", Placeholders::GENERIC);
        tone[F_S3] = TONE_EMPTY;
        const int elapsed = pitLap ? -1 : pd.getElapsedLapTime();
        if (pitLap) {
            snprintf(text[F_TIME], len, "%s", Placeholders::PIT_LAP);
            tone[F_TIME] = TONE_EMPTY;
        } else if (elapsed > 0) {
            PluginUtils::formatLapTimeTenths(elapsed, text[F_TIME], len);
            tone[F_TIME] = runningTone();
        } else {
            snprintf(text[F_TIME], len, "%s", Placeholders::GENERIC);
            tone[F_TIME] = TONE_EMPTY;
        }
    }
}
