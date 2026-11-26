// ============================================================================
// hud/deck_hud_standings.cpp
// The Standings deck (deck_hud.h): a list, the leader always on top, then
// the riders around the player, each with the gap to the player (outside a
// race, their best lap) and any time penalty (a chip the layout may hang off
// the row's end). The places
// shown are the classic Standings table's window (standings_context_window.h,
// one place pinned); the layout draws each as its row template (deck_layout.h,
// Deck::rows).
// ============================================================================
#include "deck_hud.h"
#include "deck_format.h"
#include "standings_context_window.h"

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "../core/hud_manager.h"
#include "../core/i18n.h"
#include "../core/motion.h"
#include "../core/plugin_data.h"
#include "../core/plugin_utils.h"
#include "../core/ui_config.h"
#include "standings_hud.h"
#include "../core/plugin_constants.h"

using namespace PluginConstants;
using namespace DeckLayout;
using namespace DeckFormat;

int DeckHud::standingsPlaces(int rows, int* places, int* breakAt) const {
    *breakAt = -1;
    const int field = static_cast<int>(runningOrder().size());
    rows = std::clamp(rows, 0, MAX_ROWS);
    if (field <= 0 || rows <= 0) return 0;
    // The leader pinned, the rest a window around the player (half the rows
    // left ahead of them, the others behind); the leaders only while there is
    // no place to centre on
    const int pos = rankedPosition();
    const StandingsWindow::Window w = StandingsWindow::computeWindow(field, pos > 0 ? pos - 1 : -1, 1, rows, pos > 0);
    int n = 0;
    for (int i = w.top.startIndex; i <= w.top.endIndex && n < rows; ++i) places[n++] = i + 1;
    if (!w.context.empty() && w.context.startIndex > w.top.endIndex + 1) *breakAt = n;
    for (int i = w.context.startIndex; i <= w.context.endIndex && n < rows; ++i) places[n++] = i + 1;
    return n;
}

void DeckHud::gatherStandings(int rows) {
    const PluginData& pd = PluginData::getInstance();
    const std::vector<int>& order = runningOrder();
    const int raceNum = pd.getDisplayRaceNum();
    setText(TITLE, title(Kind::Standings));

    m_listRows = rows;
    int places[MAX_ROWS];
    int breakAt = -1;
    const int n = standingsPlaces(rows, places, &breakAt);
    m_rows.rowCount = n;
    m_rows.rowBreakAt = breakAt;
    const bool race = pd.isRaceSession();
    // A place change slides the rows to their new place, with Motion on
    const bool slide = UiConfig::getInstance().getMotion() != Motion::Level::OFF;
    if (slide) m_placeSlide.begin();
    else m_placeSlide.forget();
    for (int r = 0; r < n; ++r) {
        char (&text)[ROW_FIELD_COUNT][32] = m_rows.rowText[r];
        uint8_t (&tone)[ROW_FIELD_COUNT] = m_rows.rowTone[r];
        for (int f = 0; f < ROW_FIELD_COUNT; ++f) {
            text[f][0] = '\0';
            tone[f] = TONE_NORMAL;
        }
        const int p = places[r];
        const int num = order[static_cast<size_t>(p - 1)];
        const RaceEntryData* entry = pd.getRaceEntry(num);
        const bool me = num == raceNum;
        m_rows.rowNum[r] = num;
        if (slide) m_placeSlide.add(num, p, r);
        snprintf(text[ROW_POS - ROW_FIELD_FIRST], sizeof(text[0]), "%d", p);
        if (entry) {
            snprintf(text[ROW_NUM - ROW_FIELD_FIRST], sizeof(text[0]), "%s", entry->formattedRaceNum);
            snprintf(text[ROW_NAME - ROW_FIELD_FIRST], sizeof(text[0]), "%s", entry->name);
            upper(text[ROW_NAME - ROW_FIELD_FIRST]);
        }
        // A time penalty, as the Standings HUD writes it; yours too
        const StandingsData* s = pd.getStanding(num);
        if (s && s->penalty > 0) StandingsHud::formatPenalty(s->penalty, text[ROW_PEN - ROW_FIELD_FIRST], sizeof(text[0]));
        char* g = text[ROW_GAP - ROW_FIELD_FIRST];
        if (me) {
            // Your row: lit (the layout's best colour)
            snprintf(text[ROW_ME - ROW_FIELD_FIRST], sizeof(text[0]), "%s", "1");
            tone[ROW_POS - ROW_FIELD_FIRST] = tone[ROW_NAME - ROW_FIELD_FIRST] = TONE_BEST;
        }
        if (!race) {
            // Outside a race the places are by best lap: each row's own, yours
            // too, so the times compare at a glance
            if (s && s->bestLap > 0) {
                PluginUtils::formatLapTime(s->bestLap, g, sizeof(text[0]));
                if (me) tone[ROW_GAP - ROW_FIELD_FIRST] = TONE_BEST;
            } else {
                snprintf(g, sizeof(text[0]), "%s", Placeholders::GENERIC);
                tone[ROW_GAP - ROW_FIELD_FIRST] = TONE_EMPTY;
            }
            continue;
        }
        if (me) continue;   // no gap to yourself
        int laps = 0;
        const int gap = gapToPlayer(p, &laps);
        if (laps != 0) snprintf(g, sizeof(text[0]), "%+dL", laps);
        else if (gap != INT_MAX) formatDelta(g, sizeof(text[0]), gap);
        else {
            snprintf(g, sizeof(text[0]), "%s", Placeholders::GENERIC);
            tone[ROW_GAP - ROW_FIELD_FIRST] = TONE_EMPTY;
        }
    }
    // Where each row is in its slide: stepAnimations, which an animation
    // frame runs again without gathering
    if (slide) m_placeSlide.commit(n - 1, DigitRoll::nowUs());
}
