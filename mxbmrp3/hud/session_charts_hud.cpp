// ============================================================================
// hud/session_charts_hud.cpp
// Session Charts HUD - see session_charts_hud.h. Reads each rider's per-lap lap time
// from PluginData, derives positions/gaps/trace via session_charts_math.h, and
// draws one line per rider for the selected chart type.
// ============================================================================
#include "session_charts_hud.h"
#include "../core/plugin_data.h"
#include "../core/plugin_utils.h"
#include "../core/color_config.h"
#include "../core/input_manager.h"
#include "../core/plugin_manager.h"
#include "../diagnostics/logger.h"
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <deque>
#include <unordered_map>

using namespace PluginConstants;

namespace {

// Distinct-hue palette for the per-position colour mode. Chosen for maximum
// separation (a variant of the Sasha Trubetskoy 20-colour set), avoiding the
// grays/blacks in the game's basic ColorPalette which would be unreadable as
// lines. Cycled by classification position.
constexpr unsigned long PALETTE[] = {
    PluginUtils::makeColor(230,  25,  75),  // red
    PluginUtils::makeColor( 60, 180,  75),  // green
    PluginUtils::makeColor(  0, 130, 200),  // blue
    PluginUtils::makeColor(245, 130,  48),  // orange
    PluginUtils::makeColor(145,  30, 180),  // purple
    PluginUtils::makeColor( 70, 240, 240),  // cyan
    PluginUtils::makeColor(240,  50, 230),  // magenta
    PluginUtils::makeColor(210, 245,  60),  // lime
    PluginUtils::makeColor(  0, 160, 160),  // teal
    PluginUtils::makeColor(250, 190, 190),  // pink
    PluginUtils::makeColor(170, 110,  40),  // brown
    PluginUtils::makeColor(170, 255, 195),  // mint
    PluginUtils::makeColor(190, 190,   0),  // olive
    PluginUtils::makeColor(220, 190, 255),  // lavender
    PluginUtils::makeColor(255, 225,  25),  // yellow
    PluginUtils::makeColor( 80, 130, 255),  // periwinkle
};
constexpr int PALETTE_SIZE = static_cast<int>(sizeof(PALETTE) / sizeof(PALETTE[0]));

// A drawable point in screen space; ok=false marks a gap (break the polyline).
struct Pt { float x = 0.0f; float y = 0.0f; bool ok = false; };

// Per-chart subheading label. The gap chart's reference differs by session: the
// race leader vs the session-best lap in practice/qualifying.
const char* chartNameOf(SessionChartsHud::ChartType t, bool isRace) {
    switch (t) {
        case SessionChartsHud::ChartType::LAP:   return "Lap Chart";
        case SessionChartsHud::ChartType::TRACE: return "Race Trace";
        case SessionChartsHud::ChartType::GAP:   return isRace ? "Gap to Leader" : "Gap to Best Lap";
        case SessionChartsHud::ChartType::PACE:  return "Pace";
        default: return "";
    }
}

// Only the race trace needs a mass-start cumulative and has no non-race meaning.
// Lap (position) and gap fall back to best-lap-so-far ranking off-race; pace is
// raw lap times. So only the trace is race-only.
bool chartIsRaceOnly(SessionChartsHud::ChartType t) {
    return t == SessionChartsHud::ChartType::TRACE;
}

} // namespace

// The compact seconds label (formatSecs) lives in session_charts_math.h with the
// derivations so it's unit-tested headlessly; used unqualified below via `using`.
using SessionChartsMath::formatSecs;

SessionChartsHud::SessionChartsHud() {
    DEBUG_INFO("SessionChartsHud created");
    setDraggable(true);

    // Reserve hint only (grown on rebuild, not per frame). Covers the default
    // 2-chart view over a long race (2 × 10 rows × ~100 laps × 2 for line+dot); the
    // rare 4-chart + max-rows + full MAX_LAP_LOG_STORAGE config reallocs a little.
    m_quads.reserve(4096);
    m_strings.reserve(64);

    setTextureBaseName("session_charts_hud");

    resetToDefaults();
    rebuildRenderData();
}

void SessionChartsHud::update() {
    // Skip the (relatively expensive) full rebuild when not visible on any surface.
    if (!isVisibleAnySurface()) {
        clearDataDirty();
        clearLayoutDirty();
        return;
    }
    // Click-to-spectate on a rider tag, in spectate/replay only (same gate as MapHud and
    // the Event Log). No hover highlight: the tags are small-font labels sitting on the
    // plot, and a box behind one would fight the chart lines it labels.
    const PluginData& pluginData = PluginData::getInstance();
    int drawState = pluginData.getDrawState();
    if (drawState == ViewState::SPECTATE || drawState == ViewState::REPLAY) {
        InputManager& input = InputManager::getInstance();
        if (input.getLeftButton().isClicked()) {
            // Shift into build space so tags line up when dragged on the companion.
            CursorPosition cursor = input.getCursorPosition();
            mapCursorToHudSpace(cursor.x, cursor.y);
            if (cursor.isValid && isPointInBounds(cursor.x, cursor.y)) {
                handleClick(cursor.x, cursor.y);
            }
        }
    }

    // Detect session changes (new event/track/bike) and reset. sessionGeneration is
    // bumped on every RaceSession callback, so comparing it catches every transition
    // without subscribing to the 1 Hz session-clock notifications that share that
    // change type (see handlesDataType).
    const int currentGeneration = pluginData.getSessionData().sessionGeneration;
    if (currentGeneration != m_cachedSessionGeneration) {
        m_cachedSessionGeneration = currentGeneration;
        setDataDirty();
    }

    // Chart data only changes on lap completion / spectate / session change, all
    // of which set the data-dirty flag; positions are derived, not polled.
    processDirtyFlags();
}

bool SessionChartsHud::handlesDataType(DataChangeType dataType) const {
    switch (dataType) {
        case DataChangeType::LapLog:         // a rider completed a lap (the data source)
        case DataChangeType::SpectateTarget: // player-window recenters on the new target
        case DataChangeType::RaceEntries:    // grid/entries changed
            return true;
        default:
            // Deliberately NOT Standings: it fires many times/sec on full grids,
            // and the chart series only change when a lap completes (LapLog).
            //
            // Nor SessionData, for the same reason at a slower rate: setSessionTime()
            // notifies with that type on every whole-second boundary (the session-clock
            // heartbeat), which would drive a full session recompute once a second for a
            // subscription only ever needed on a session CHANGE. The change itself is
            // caught by the sessionGeneration compare in update(), the same way
            // TimingHud / GapBarHud / LeanWidget catch it.
            return false;
    }
}

// ---------------------------------------------------------------------------
// Data collection
// ---------------------------------------------------------------------------

void SessionChartsHud::collectSectorTimes(int raceNum, std::vector<int>& out) const {
    const PluginData& pluginData = PluginData::getInstance();
    out.clear();

    // Completed laps, oldest-first, flattened with a fixed GAME_SECTOR_COUNT stride so the
    // same index means the same point on track for every rider.
    const std::deque<LapLogEntry>* log = pluginData.getLapLog(raceNum);
    if (log) {
        out.reserve(log->size() * GAME_SECTOR_COUNT + GAME_SECTOR_COUNT);
        for (auto it = log->rbegin(); it != log->rend(); ++it) {
            if (!it->isComplete || it->lapTime <= 0) continue;
            out.push_back(it->sector1);
            out.push_back(it->sector2);
            out.push_back(it->sector3);
#if GAME_SECTOR_COUNT >= 4
            out.push_back(it->sector4);
#endif
        }
    }

    // The LIVE leading edge: sectors of the lap currently in progress, which the lap log
    // will not hold until the rider crosses start/finish. RaceSplit fires for EVERY rider,
    // not just the player, so this is real data for the whole field — it is the difference
    // between a chart that updates once a lap and one that updates three or four times.
    //
    // Appended positionally rather than by matching lap numbers: completing a lap calls
    // setCurrentLapNumber(), which CLEARS the splits (plugin_data.cpp), so CurrentLapData
    // always describes the lap after the last logged one and cannot double-count it. That
    // ordering is the invariant this relies on — not the lap-number convention, which
    // differs between the RaceLap and RaceSplit callbacks.
    const CurrentLapData* cur = pluginData.getCurrentLapData(raceNum);
    if (cur) {
        // Splits are ACCUMULATED within the lap; sectors are their successive differences.
        // Stops at the first split not yet crossed (-1), which is what makes the series end
        // exactly where the rider currently is on track.
        int prev = 0;
        const int splits[3] = { cur->split1, cur->split2, cur->split3 };
        for (int s = 0; s < GAME_SECTOR_COUNT - 1; ++s) {
            if (splits[s] <= prev) break;   // not crossed yet, or nonsense ordering
            out.push_back(splits[s] - prev);
            prev = splits[s];
        }
    }
}

void SessionChartsHud::collectField(FieldData& field) const {
    const PluginData& pluginData = PluginData::getInstance();
    const std::vector<int>& order = pluginData.getClassificationOrder();

    field.raceNums = order;
    field.isRace = pluginData.isRaceSession();
    field.lapMs.assign(order.size(), {});
    field.lapValid.assign(order.size(), {});

    for (size_t i = 0; i < order.size(); ++i) {
        const std::deque<LapLogEntry>* log = pluginData.getLapLog(order[i]);
        if (!log) continue;
        // Deque is newest-first; reverse to oldest-first, keep completed laps only.
        // Keep INVALID laps too: their time still elapsed, so cumulative/position/gap
        // must include them (an invalidated lap doesn't rewind the race). Validity is
        // recorded in parallel so pace/best-lap can exclude them.
        std::vector<int>& laps = field.lapMs[i];
        std::vector<char>& valid = field.lapValid[i];
        laps.reserve(log->size());
        valid.reserve(log->size());
        for (auto it = log->rbegin(); it != log->rend(); ++it) {
            if (it->isComplete && it->lapTime > 0) {
                laps.push_back(it->lapTime);
                valid.push_back(it->isValid ? 1 : 0);
            }
        }
        field.maxLap = std::max(field.maxLap, static_cast<int>(laps.size()));
    }

    // Cumulative race time (drives the race trace; race only), at the configured
    // resolution. See FieldData::pointsPerLap for why the choice is field-wide.
    field.cumulative.assign(order.size(), {});
    // Races only, for two reasons — the second is why this is a gate and not a preference.
    //
    // Nothing to plot: off-race the charts rank by BEST LAP so far (the provisional
    // qualifying order, and each rider's gap to the session-best lap), which a partial lap
    // cannot improve. Sector points would draw GAME_SECTOR_COUNT identical stacked values
    // per lap — stair-steps advertising resolution that isn't there.
    //
    // And it would be wrong, not merely useless: bestLapSoFar() consumes lapMs, so off-race
    // `rank` (and therefore positions/gaps) is a PER-LAP array. The renderers index those by
    // series index while xForPoint maps that index at the field's resolution, so a
    // sector-resolution field with a per-lap rank makes the two disagree about what an index
    // means and squeezes the whole race into the left third of the plot. Padding each value
    // out to the sector stride would fix the geometry and buy only the stair-steps above.
    //
    // A genuinely useful off-race sector view is a DIFFERENT quantity — the current lap's
    // cumulative sector time against your own best at the same sector ("up or down at S1",
    // which TimingHud answers today) — so it belongs as a new chart type, not as these at a
    // higher resolution. The snapshot ships laps[].s in every session type, so that would
    // need no plugin-side data change.
    const bool wantSectors = (m_enabledElements & ELEM_SECTOR_POINTS) != 0 && field.isRace;
    bool sectorsUsable = wantSectors;
    std::vector<std::vector<long long>> sectorCum;
    if (wantSectors) {
        sectorCum.assign(order.size(), {});
        for (size_t i = 0; i < order.size() && sectorsUsable; ++i) {
            std::vector<int> sectors;
            collectSectorTimes(order[i], sectors);
            sectorCum[i] = SessionChartsMath::cumulativeBySector(sectors, GAME_SECTOR_COUNT);
            // A rider whose sector series doesn't reach their completed-lap count has a
            // hole in it — a track with misplaced split markers reports a zero sector, and
            // cumulativeBySector stops there (it cannot sum past an unanchored gap). Rather
            // than silently dropping that rider's later laps, fall the WHOLE field back to
            // per-lap: the alternative is mixed resolutions, and the ranking functions
            // compare riders by array index, so mixing would rank one rider's sector
            // against another's lap.
            const size_t completed = field.lapMs[i].size();
            if (sectorCum[i].size() < completed * static_cast<size_t>(GAME_SECTOR_COUNT)) {
                sectorsUsable = false;
            }
        }
    }

    // Per-lap cumulative is always built: it is the trace baseline's basis (below) and the
    // fallback series, independent of the resolution the charts end up drawn at.
    std::vector<std::vector<long long>> perLapCum(order.size());
    for (size_t i = 0; i < order.size(); ++i) {
        perLapCum[i] = SessionChartsMath::cumulative(field.lapMs[i]);
    }

    field.pointsPerLap = sectorsUsable ? GAME_SECTOR_COUNT : 1;
    for (size_t i = 0; i < order.size(); ++i) {
        field.cumulative[i] = sectorsUsable ? std::move(sectorCum[i]) : perLapCum[i];
    }

    // X domain: how far the furthest-along rider has actually got. At sector resolution
    // that is usually part way through a lap, and clamping it back to maxLap would stack
    // the leader's last points on the right edge.
    field.maxLaps = static_cast<float>(field.maxLap);
    if (sectorsUsable) {
        for (size_t i = 0; i < order.size(); ++i) {
            if (field.cumulative[i].empty()) continue;
            field.maxLaps = std::max(field.maxLaps,
                field.lapsAt(static_cast<int>(field.cumulative[i].size()) - 1));
        }
    }

    // Ranking basis for the position and gap charts: cumulative race time in a
    // race, best-lap-so-far otherwise. The latter gives the provisional
    // qualifying/practice order and each rider's gap to the session-best lap.
    // In a RACE the ranking basis IS field.cumulative, so bind to it rather than copying
    // every rider's whole series into a second array — that copy ran on every rebuild and
    // grew with the race. Off-race the basis is a different quantity and has to be built.
    std::vector<std::vector<long long>> rankStorage;
    if (!field.isRace) {
        rankStorage.resize(order.size());
        for (size_t i = 0; i < order.size(); ++i) {
            rankStorage[i] = SessionChartsMath::bestLapSoFar(field.lapMs[i], field.lapValid[i]);
        }
    }
    const std::vector<std::vector<long long>>& rank = field.isRace ? field.cumulative : rankStorage;
    field.positions = SessionChartsMath::positionsPerLap(rank, field.raceNums);
    field.gaps = SessionChartsMath::gapToLeaderPerLap(rank);

    // Reference pace / trace need a mass start — race only. Deliberately computed from the
    // PER-LAP cumulative even when the charts are drawn at sector resolution: the reference
    // is the leader's average LAP time, so counting sectors as laps would divide it by the
    // sector count and put every rider wildly "ahead" of the reference.
    if (field.isRace) {
        int leader = SessionChartsMath::leaderIndex(perLapCum, field.raceNums);
        if (leader >= 0 && !perLapCum[leader].empty()) {
            field.refPaceMs = SessionChartsMath::referencePaceMs(
                perLapCum[leader].back(),
                static_cast<int>(perLapCum[leader].size()));
        }
    }
}

void SessionChartsHud::selectDrawn(const FieldData& field, std::vector<DrawnRider>& drawn) const {
    const int n = static_cast<int>(field.raceNums.size());
    if (n == 0) return;

    const PluginData& pluginData = PluginData::getInstance();
    const int displayRaceNum = pluginData.getDisplayRaceNum();

    // Find the display rider's index in the classification order.
    int playerIdx = -1;
    for (int i = 0; i < n; ++i) {
        if (field.raceNums[i] == displayRaceNum) { playerIdx = i; break; }
    }

    const int topN = std::min(m_topPositionsCount, n);
    const int rows = std::min(m_displayRowCount, n);

    // Build the set of classification indices to draw: top-N pinned plus a
    // window centered on the player (StandingsHud algorithm, standings_hud.cpp).
    std::vector<int> indices;
    indices.reserve(rows);
    auto addIdx = [&](int i) {
        if (i < 0 || i >= n) return;
        for (int existing : indices) if (existing == i) return;  // dedupe overlap
        if (static_cast<int>(indices.size()) < rows) indices.push_back(i);
    };

    if (playerIdx < 0 || playerIdx < topN) {
        // Player in the top group (or unknown): just take the first `rows`.
        for (int i = 0; i < rows; ++i) addIdx(i);
    } else {
        for (int i = 0; i < topN; ++i) addIdx(i);
        int available = rows - topN;
        int before = available / 2;
        int after = available - before - 1;  // -1 for the player row itself
        int start = std::max(topN, playerIdx - before);
        int lostBefore = start - (playerIdx - before);
        int desiredEnd = playerIdx + after + lostBefore;
        int end = std::min(n - 1, desiredEnd);
        int lostAfter = desiredEnd - end;
        if (lostAfter > 0) start = std::max(topN, start - lostAfter);
        for (int i = start; i <= end; ++i) addIdx(i);
    }
    // Stable per-rider COLOUR, independent of the draw/legend order below. Rank the
    // drawn set by RACE NUMBER and assign a fixed palette hue (or brand-variant
    // ordinal) per rider, so a rider's line keeps its colour through overtakes — the
    // whole point of a progression chart is to follow one line over time. The
    // race-number ranking is 0..size-1, so every on-screen line gets a distinct hue
    // (the drawn count is capped at the palette size).
    std::vector<int> byNum = indices;
    std::sort(byNum.begin(), byNum.end(), [&](int a, int b) {
        return field.raceNums[a] < field.raceNums[b];
    });
    std::unordered_map<int, unsigned long> colorFor;   // fieldIdx -> colour
    std::unordered_map<unsigned long, int> brandOrdinal;
    int slot = 0;
    for (int idx : byNum) {
        unsigned long color;
        if (m_riderColorMode == RiderColorMode::BRAND) {
            const RaceEntryData* entry = pluginData.getRaceEntry(field.raceNums[idx]);
            unsigned long base = (entry && entry->bikeBrandColor) ? entry->bikeBrandColor
                                                                  : PALETTE[slot % PALETTE_SIZE];
            int ord = brandOrdinal[base]++;
            switch (ord % 5) {
                case 0: color = base; break;
                case 1: color = PluginUtils::lightenColor(base, 0.35f); break;
                case 2: color = PluginUtils::darkenColor(base, 0.60f); break;
                case 3: color = PluginUtils::lightenColor(base, 0.60f); break;
                default: color = PluginUtils::darkenColor(base, 0.40f); break;
            }
        } else {
            color = PALETTE[slot % PALETTE_SIZE];
        }
        colorFor[idx] = color;
        ++slot;
    }

    // Draw / legend ORDER: running order (classification index ascending), so the
    // legend reads top-to-bottom as the live classification (leader first), like the
    // Standings HUD. Colour comes from the stable per-rider map above, so the
    // ordering reshuffles as places change but a rider's hue never does.
    std::sort(indices.begin(), indices.end());
    for (int idx : indices) {
        DrawnRider d;
        d.fieldIdx = idx;
        d.isPlayer = (field.raceNums[idx] == displayRaceNum);
        // The player/spectated rider's line (and its #num tag) uses the accent
        // colour so it stands out, matching the StandingsHud player highlight.
        d.color = d.isPlayer ? this->getColor(ColorSlot::ACCENT) : colorFor[idx];
        drawn.push_back(d);
    }
}

// ---------------------------------------------------------------------------
// Layout / dispatch
// ---------------------------------------------------------------------------

void SessionChartsHud::rebuildRenderData() {
    m_quads.clear();
    clearStrings();
    m_tagClickRegions.clear();

    const auto dims = getScaledDimensions();

    // Collect and derive data, then select which riders to draw. Note: we always
    // render the chart frame (grid + axes) even before any laps exist; the lines
    // and their inline "#num" tags simply fill in as laps arrive.
    FieldData field;
    collectField(field);
    std::vector<DrawnRider> drawn;
    selectDrawn(field, drawn);
    const bool isRace = field.isRace;

    // Which charts to render, stacked vertically top-to-bottom (whichever checkboxes
    // are enabled, in a fixed order). Each is a subheading + a graph.
    std::vector<ChartType> charts;
    if (m_enabledCharts & CHART_LAP)   charts.push_back(ChartType::LAP);
    if (m_enabledCharts & CHART_TRACE) charts.push_back(ChartType::TRACE);
    if (m_enabledCharts & CHART_GAP)   charts.push_back(ChartType::GAP);
    if (m_enabledCharts & CHART_PACE)  charts.push_back(ChartType::PACE);

    // Dimensions. Every chart gets the full height, so the HUD grows taller as charts
    // are added (a multi-chart stack can exceed the screen — position/scale is the
    // user's to set).
    int nCharts = static_cast<int>(charts.size());
    float titleHeight = m_bShowTitle ? dims.lineHeightLarge : 0.0f;
    float subHeadH = dims.lineHeightNormal;                        // per-chart subheading row
    float chartGapY = dims.lineHeightNormal;                       // full-row gap between stacked charts (keeps the HUD on-grid and matching Performance's section gap)
    float perChartH = GRAPH_HEIGHT_LINES * dims.lineHeightNormal;

    float graphWidth = PluginUtils::calculateMonospaceTextWidth(GRAPH_WIDTH_CHARS, dims.fontSize);

    float contentHeight = nCharts > 0
        ? nCharts * (subHeadH + perChartH) + (nCharts - 1) * chartGapY
        : dims.lineHeightNormal;  // "no charts enabled" note

    float backgroundWidth = dims.paddingH + graphWidth + dims.paddingH;
    float backgroundHeight = dims.paddingV + titleHeight + contentHeight + dims.paddingV;

    setBounds(START_X, START_Y, START_X + backgroundWidth, START_Y + backgroundHeight);
    addBackgroundQuad(START_X, START_Y, backgroundWidth, backgroundHeight);

    float contentStartX = START_X + dims.paddingH;
    float currentY = START_Y + dims.paddingV;

    if (m_bShowTitle) {
        addTitleString("Charts", contentStartX, currentY, Justify::LEFT,
            this->getFont(FontCategory::TITLE), this->getColor(ColorSlot::PRIMARY), dims.fontSizeLarge);
        currentY += titleHeight;
    }

    if (charts.empty()) {
        addString("No charts enabled", contentStartX + graphWidth * 0.5f, currentY,
            Justify::CENTER, this->getFont(FontCategory::NORMAL),
            this->getColor(ColorSlot::MUTED), dims.fontSize);
    }
    float y = currentY;
    for (ChartType ct : charts) {
        // Subheading (chart name), styled like StandingsHud's session line.
        addString(chartNameOf(ct, isRace), contentStartX, y, Justify::LEFT,
            this->getFont(FontCategory::TITLE), this->getColor(ColorSlot::PRIMARY), dims.fontSize);
        y += subHeadH;
        if (!isRace && chartIsRaceOnly(ct)) {
            drawRaceOnlyNote(contentStartX, y, graphWidth, perChartH);
        } else {
            drawChart(ct, contentStartX, y, graphWidth, perChartH, field, drawn);
        }
        y += perChartH + chartGapY;
    }
}

// ---------------------------------------------------------------------------
// Chart dispatch: reserve room for axis labels inside the cell, then render.
// ---------------------------------------------------------------------------

void SessionChartsHud::drawChart(ChartType type, float x, float y, float w, float h,
                              const FieldData& field, const std::vector<DrawnRider>& drawn) {
    const auto dims = getScaledDimensions();
    // Leave room at the left for Y labels, the bottom for lap labels (overlaid, same
    // idiom as TelemetryHud), and the right for the inline "#num" line tags so a
    // finisher's tag (at the right plot edge) isn't clipped off the HUD.
    float labelPadLeft = (m_enabledElements & ELEM_AXIS_LABELS)
        ? PluginUtils::calculateMonospaceTextWidth(5, dims.fontSizeSmall) : 0.0f;
    float labelPadBottom = (m_enabledElements & ELEM_AXIS_LABELS) ? dims.lineHeightSmall : 0.0f;
    float labelPadRight = (m_enabledElements & ELEM_LEGEND)
        ? PluginUtils::calculateMonospaceTextWidth(TAG_WIDTH_CHARS, dims.fontSizeSmall) : 0.0f;

    float px = x + labelPadLeft;
    float py = y;
    float pw = w - labelPadLeft - labelPadRight;
    float ph = h - labelPadBottom;

    switch (type) {
        case ChartType::LAP:   drawLapChart(px, py, pw, ph, field, drawn); break;
        case ChartType::TRACE: drawTraceChart(px, py, pw, ph, field, drawn); break;
        case ChartType::GAP:   drawGapChart(px, py, pw, ph, field, drawn); break;
        case ChartType::PACE:  drawPaceChart(px, py, pw, ph, field, drawn); break;
        default: break;
    }
}

// Placeholder for a race-only chart shown in a non-race session (practice/qual):
// position/trace/gap need a mass-start cumulative race to mean anything.
void SessionChartsHud::drawRaceOnlyNote(float x, float y, float w, float h) {
    const auto dims = getScaledDimensions();
    addString("Race sessions only", x + w * 0.5f, y + h * 0.5f - dims.lineHeightNormal * 0.5f,
              Justify::CENTER, this->getFont(FontCategory::NORMAL),
              this->getColor(ColorSlot::MUTED), dims.fontSize);
}

// ---------------------------------------------------------------------------
// Chart 1: Lap chart (track position per lap)
// ---------------------------------------------------------------------------

void SessionChartsHud::drawLapChart(float px, float py, float pw, float ph,
                                 const FieldData& field, const std::vector<DrawnRider>& drawn) {
    const auto dims = getScaledDimensions();
    const int K = static_cast<int>(drawn.size());
    const int yRows = std::max(2, K);
    const float gridThickness = stripChartGridThickness();
    const unsigned long gridColor = this->getColor(ColorSlot::MUTED);

    auto yForRow = [&](int row0) {  // 0 = top row (leader among the shown riders)
        return py + static_cast<float>(row0) / static_cast<float>(yRows - 1) * ph;
    };

    if (m_enabledElements & ELEM_GRID) {
        addHorizontalGridLine(px, yForRow(0), pw, gridColor, gridThickness);
        addHorizontalGridLine(px, yForRow(yRows - 1), pw, gridColor, gridThickness);
    }

    // Bump chart of the SHOWN subset: at each lap, rank the shown riders by their
    // absolute track position and give each its own row. Every line is then exactly
    // one row, so its end tag lines up with the line and never overlaps another.
    // rowOf[di][lap] = 0-based row (rank among the shown present), or -1 if absent.
    std::vector<std::vector<int>> rowOf(K);
    for (int di = 0; di < K; ++di)
        rowOf[di].assign(field.positions[drawn[di].fieldIdx].size(), -1);
    // Iterate the SERIES, not laps: at sector resolution there are pointsPerLap entries
    // per lap, and rowOf is sized from field.positions (which follows the series).
    int maxPoints = 0;
    for (int di = 0; di < K; ++di)
        maxPoints = std::max(maxPoints, static_cast<int>(rowOf[di].size()));
    for (int lap = 0; lap < maxPoints; ++lap) {
        std::vector<std::pair<int, int>> present;  // (absolute position, di)
        for (int di = 0; di < K; ++di) {
            const std::vector<int>& pos = field.positions[drawn[di].fieldIdx];
            if (lap < static_cast<int>(pos.size()) && pos[lap] > 0)
                present.push_back({ pos[lap], di });
        }
        std::sort(present.begin(), present.end());  // by position, then di (stable enough)
        for (int r = 0; r < static_cast<int>(present.size()); ++r)
            rowOf[present[r].second][lap] = r;
    }

    const float lineThickness = 0.0022f * dims.scale;
    const float dotSize = 0.004f * dims.scale;
    for (int di = 0; di < K; ++di) {
        const DrawnRider& d = drawn[di];
        float thick = d.isPlayer ? lineThickness * 1.6f : lineThickness;
        Pt prev, tag;
        for (int lap = 0; lap < static_cast<int>(rowOf[di].size()); ++lap) {
            if (rowOf[di][lap] < 0) { prev.ok = false; continue; }
            Pt cur{ xForPoint(px, pw, lap, field), yForRow(rowOf[di][lap]), true };
            if (prev.ok) addLineSegment(prev.x, prev.y, cur.x, cur.y, d.color, thick);
            if (m_enabledElements & ELEM_DOTS) addDot(cur.x, cur.y, d.color, dotSize);
            prev = cur; tag = cur;
        }
        if ((m_enabledElements & ELEM_LEGEND) && tag.ok)
            addRiderTag(tag.x, tag.y, field.raceNums[d.fieldIdx], d.color);
    }

    if (m_enabledElements & ELEM_AXIS_LABELS) {
        // Y is order among the SHOWN riders, so label the top/bottom rows with the actual
        // positions of whoever occupies them at the latest sample. The search bound lives in
        // latestPositionExtent() — it follows the series, not the lap count, which is what
        // this loop got wrong when it was open-coded here.
        std::vector<std::vector<int>> drawnPositions;
        drawnPositions.reserve(K);
        for (int di = 0; di < K; ++di) drawnPositions.push_back(field.positions[drawn[di].fieldIdx]);
        const auto extent = SessionChartsMath::latestPositionExtent(drawnPositions);
        const int topPos = extent.top, botPos = extent.bottom;
        char topBuf[8], botBuf[8];
        if (topPos > 0) {
            snprintf(topBuf, sizeof(topBuf), "P%d", topPos);
            snprintf(botBuf, sizeof(botBuf), "P%d", botPos);
        }
        addChartAxisLabels(px, py, pw, ph, field.maxLaps,
                           topPos > 0 ? topBuf : nullptr, topPos > 0 ? botBuf : nullptr, dims);
    }
}

// ---------------------------------------------------------------------------
// Chart 2: Race trace (cumulative time vs reference pace)
// ---------------------------------------------------------------------------

void SessionChartsHud::drawTraceChart(float px, float py, float pw, float ph,
                                   const FieldData& field, const std::vector<DrawnRider>& drawn) {
    const auto dims = getScaledDimensions();
    // refPace is 0 until the leader has completed a lap; we still draw the frame
    // and let the lines fill in once data arrives.
    const bool hasData = field.refPaceMs > 0;

    // Robust Y range: gather every trace value, then fit to a robust range (Tukey
    // fence) so one rider who lost minutes can't stretch the axis and crush the
    // pack into a sliver. The outlier's line is still drawn, clipped to the chart
    // edge below. Always include 0 so the reference (zero) line stays on screen.
    std::vector<long long> vals;
    for (const DrawnRider& d : drawn) {
        const std::vector<long long>& cum = field.cumulative[d.fieldIdx];
        for (size_t l = 0; l < cum.size(); ++l)
            vals.push_back(SessionChartsMath::traceValueAtSector(
                field.refPaceMs, static_cast<int>(l), field.pointsPerLap, cum[l]));
    }
    SessionChartsMath::AxisRange rr = SessionChartsMath::robustRange(vals);
    long long vMin = std::min<long long>(rr.valid ? rr.lo : 0, 0);
    long long vMax = std::max<long long>(rr.valid ? rr.hi : 0, 0);
    if (vMax - vMin < 1000) { vMax += 500; vMin -= 500; }  // at least a 1s span
    long long span = vMax - vMin;

    auto yForVal = [&](long long v) {  // clip outliers to the chart edge
        long long vc = std::max(vMin, std::min(vMax, v));
        return py + static_cast<float>(vMax - vc) / static_cast<float>(span) * ph;
    };

    const float gridThickness = stripChartGridThickness();
    const unsigned long gridColor = this->getColor(ColorSlot::MUTED);
    if (m_enabledElements & ELEM_GRID) {
        addHorizontalGridLine(px, py, pw, gridColor, gridThickness);
        addHorizontalGridLine(px, py + ph, pw, gridColor, gridThickness);
    }

    // Dashed zero (reference-pace) line.
    if ((m_enabledElements & ELEM_ZERO_LINE) && vMin <= 0 && vMax >= 0) {
        float zy = yForVal(0);
        const int dashes = 24;
        float dashW = pw / (dashes * 2 - 1);
        for (int i = 0; i < dashes; ++i) {
            float x1 = px + i * 2 * dashW;
            addLineSegment(x1, zy, x1 + dashW, zy, this->getColor(ColorSlot::SECONDARY), gridThickness * 1.5f);
        }
    }

    const float lineThickness = 0.0022f * dims.scale;
    const float dotSize = 0.004f * dims.scale;
    for (const DrawnRider& d : drawn) {
        const std::vector<long long>& cum = field.cumulative[d.fieldIdx];
        float thick = d.isPlayer ? lineThickness * 1.6f : lineThickness;
        Pt prev, tag;
        for (size_t l = 0; l < cum.size(); ++l) {
            long long v = SessionChartsMath::traceValueAtSector(
                field.refPaceMs, static_cast<int>(l), field.pointsPerLap, cum[l]);
            Pt cur{ xForPoint(px, pw, static_cast<int>(l), field), yForVal(v), true };
            if (prev.ok) addLineSegment(prev.x, prev.y, cur.x, cur.y, d.color, thick);
            if (m_enabledElements & ELEM_DOTS) addDot(cur.x, cur.y, d.color, dotSize);
            prev = cur; tag = cur;
        }
        if ((m_enabledElements & ELEM_LEGEND) && tag.ok)
            addRiderTag(tag.x, tag.y, field.raceNums[d.fieldIdx], d.color);
    }
    if (m_enabledElements & ELEM_AXIS_LABELS) {
        char topBuf[16], botBuf[16];
        if (hasData) {
            formatSecs(topBuf, sizeof(topBuf), vMax, true);
            formatSecs(botBuf, sizeof(botBuf), vMin, true);
        }
        addChartAxisLabels(px, py, pw, ph, field.maxLaps,
                           hasData ? topBuf : nullptr, hasData ? botBuf : nullptr, dims);
    }
}

// ---------------------------------------------------------------------------
// Chart 3: Gap to leader (seconds behind the current leader per lap)
// ---------------------------------------------------------------------------

void SessionChartsHud::drawGapChart(float px, float py, float pw, float ph,
                                 const FieldData& field, const std::vector<DrawnRider>& drawn) {
    const auto dims = getScaledDimensions();

    // Robust upper bound: one blown-out rider (minutes behind) must not compress
    // the whole pack against the top grid line. Fit to a robust range so the pack
    // fills the axis; the outlier's line is still drawn, clipped to the bottom edge.
    // Gaps are >= 0 with the leader pinned at 0, so only the high end needs taming.
    // In a non-race session a rider with no VALID lap yet carries the kNoValidLap
    // sentinel through bestLapSoFar -> gap, so exclude those from the range sample
    // (else the fence lands on the sentinel and crushes the real pack). Their lines
    // still draw, clipped to the bottom edge.
    std::vector<long long> vals;
    for (const DrawnRider& d : drawn)
        for (long long g : field.gaps[d.fieldIdx])
            if (g < SessionChartsMath::kNoValidLap / 2) vals.push_back(g);
    SessionChartsMath::AxisRange rr = SessionChartsMath::robustRange(vals);
    long long gapMax = std::max<long long>(1000, rr.valid ? rr.hi : 0);  // at least a 1s span

    auto yForGap = [&](long long g) {  // 0 at top, growing downward; clip to edge
        long long gc = std::max<long long>(0, std::min(gapMax, g));
        return py + static_cast<float>(gc) / static_cast<float>(gapMax) * ph;
    };

    const float gridThickness = stripChartGridThickness();
    const unsigned long gridColor = this->getColor(ColorSlot::MUTED);
    if (m_enabledElements & ELEM_GRID) {
        addHorizontalGridLine(px, py, pw, gridColor, gridThickness);          // leader (0)
        addHorizontalGridLine(px, py + ph, pw, gridColor, gridThickness);     // gapMax
    }

    const float lineThickness = 0.0022f * dims.scale;
    const float dotSize = 0.004f * dims.scale;
    for (const DrawnRider& d : drawn) {
        const std::vector<long long>& gaps = field.gaps[d.fieldIdx];
        const std::vector<long long>& cum = field.cumulative[d.fieldIdx];
        float thick = d.isPlayer ? lineThickness * 1.6f : lineThickness;
        Pt prev, tag;
        for (size_t l = 0; l < cum.size(); ++l) {
            Pt cur{ xForPoint(px, pw, static_cast<int>(l), field), yForGap(gaps[l]), true };
            if (prev.ok) addLineSegment(prev.x, prev.y, cur.x, cur.y, d.color, thick);
            if (m_enabledElements & ELEM_DOTS) addDot(cur.x, cur.y, d.color, dotSize);
            prev = cur; tag = cur;
        }
        if ((m_enabledElements & ELEM_LEGEND) && tag.ok)
            addRiderTag(tag.x, tag.y, field.raceNums[d.fieldIdx], d.color);
    }
    if (m_enabledElements & ELEM_AXIS_LABELS) {
        char botBuf[16];
        formatSecs(botBuf, sizeof(botBuf), gapMax, false);
        addChartAxisLabels(px, py, pw, ph, field.maxLaps, "0.0s", botBuf, dims);
    }
}

// ---------------------------------------------------------------------------
// Chart 4: Pace (raw lap time per lap)
// ---------------------------------------------------------------------------

void SessionChartsHud::drawPaceChart(float px, float py, float pw, float ph,
                                  const FieldData& field, const std::vector<DrawnRider>& drawn) {
    // The pace chart plots LAP TIMES, so its X domain is whole laps whatever resolution the
    // field is at — unlike the three sector-aware charts, which span field.maxLaps. ONE
    // local for both the point placement and the axis label so the two cannot disagree:
    // they did, when the label was switched to the field domain and the points were not,
    // putting "L4" under a chart whose last point is lap 3.
    const float paceDomainEnd = static_cast<float>(field.maxLap);
    const auto dims = getScaledDimensions();
    const bool filter = (m_enabledElements & ELEM_FILTER_OUTLIERS);

    // A lap counts as "clean racing pace" when it's not filtered out. Pace excludes
    // INVALID laps (cut track / jump-start / penalised — not representative pace),
    // the opening lap, and laps slower than median*factor. The invalid-lap exclusion
    // rides on the same ELEM_FILTER_OUTLIERS toggle: with the filter off you see
    // every completed lap raw. (Cumulative/gap/position keep invalid laps — the time
    // still elapsed — so this is a pace-only exclusion.)
    auto isValidLap = [&](int fieldIdx, int lapIndex0) {
        const std::vector<char>& v = field.lapValid[fieldIdx];
        return lapIndex0 >= static_cast<int>(v.size()) || v[lapIndex0] != 0;
    };

    // Median across clean laps (baseline for outlier filtering), so an invalid or
    // opening lap doesn't skew the racing-pace band.
    std::vector<int> allLaps;
    for (const DrawnRider& d : drawn) {
        const std::vector<int>& laps = field.lapMs[d.fieldIdx];
        for (size_t l = 0; l < laps.size(); ++l) {
            if (filter && (!isValidLap(d.fieldIdx, static_cast<int>(l)) || l == 0)) continue;
            allLaps.push_back(laps[l]);
        }
    }
    int median = SessionChartsMath::medianMs(allLaps);

    auto included = [&](int fieldIdx, int lapIndex0, int lapMs) {
        if (!filter) return true;
        if (!isValidLap(fieldIdx, lapIndex0)) return false;   // invalid lap: not clean pace
        return !SessionChartsMath::isOutlierLap(lapIndex0, lapMs, median, m_outlierFactor);
    };

    // Auto-fit Y range across included laps.
    long long vMin = -1, vMax = -1;
    for (const DrawnRider& d : drawn) {
        const std::vector<int>& laps = field.lapMs[d.fieldIdx];
        for (size_t l = 0; l < laps.size(); ++l) {
            if (!included(d.fieldIdx, static_cast<int>(l), laps[l])) continue;
            if (vMin < 0 || laps[l] < vMin) vMin = laps[l];
            if (vMax < 0 || laps[l] > vMax) vMax = laps[l];
        }
    }
    // Before any laps arrive we still draw the frame; a nominal range keeps the
    // grid sensible and the value labels are suppressed until there's data.
    const bool hasData = (vMin >= 0 && vMax >= 0);
    if (!hasData) { vMin = 0; vMax = 1; }
    if (vMax - vMin < 500) { vMax += 250; vMin = std::max(0LL, vMin - 250); }
    long long span = std::max(1LL, vMax - vMin);

    auto yForVal = [&](long long v) {  // slower (larger) higher up
        return py + static_cast<float>(vMax - v) / static_cast<float>(span) * ph;
    };

    const float gridThickness = stripChartGridThickness();
    const unsigned long gridColor = this->getColor(ColorSlot::MUTED);
    if (m_enabledElements & ELEM_GRID) {
        addHorizontalGridLine(px, py, pw, gridColor, gridThickness);
        addHorizontalGridLine(px, py + ph * 0.5f, pw, gridColor, gridThickness);
        addHorizontalGridLine(px, py + ph, pw, gridColor, gridThickness);
    }

    const float lineThickness = 0.0022f * dims.scale;
    const float dotSize = 0.004f * dims.scale;
    for (const DrawnRider& d : drawn) {
        const std::vector<int>& laps = field.lapMs[d.fieldIdx];
        float thick = d.isPlayer ? lineThickness * 1.6f : lineThickness;
        Pt prev, tag;
        for (size_t l = 0; l < laps.size(); ++l) {
            if (!included(d.fieldIdx, static_cast<int>(l), laps[l])) { prev.ok = false; continue; }
            Pt cur{ xForLap(px, pw, static_cast<int>(l), paceDomainEnd), yForVal(laps[l]), true };
            if (prev.ok) addLineSegment(prev.x, prev.y, cur.x, cur.y, d.color, thick);
            if (m_enabledElements & ELEM_DOTS) addDot(cur.x, cur.y, d.color, dotSize);
            prev = cur; tag = cur;
        }
        if ((m_enabledElements & ELEM_LEGEND) && tag.ok)
            addRiderTag(tag.x, tag.y, field.raceNums[d.fieldIdx], d.color);
    }
    if (m_enabledElements & ELEM_AXIS_LABELS) {
        char topBuf[16], botBuf[16];
        if (hasData) {
            formatSecs(topBuf, sizeof(topBuf), vMax, false);   // slower at top
            formatSecs(botBuf, sizeof(botBuf), vMin, false);   // faster at bottom
        }
        addChartAxisLabels(px, py, pw, ph, paceDomainEnd,
                           hasData ? topBuf : nullptr, hasData ? botBuf : nullptr, dims);
    }
}

// ---------------------------------------------------------------------------
// Inline line tag ("#num" at the end of a rider's line, in the rider's colour)
// ---------------------------------------------------------------------------

void SessionChartsHud::addRiderTag(float x, float y, int raceNum, unsigned long color) {
    const auto dims = getScaledDimensions();
    char buf[8];
    snprintf(buf, sizeof(buf), "#%d", raceNum);
    const float tagX = x + dims.paddingH * 0.25f;
    const float tagY = y - dims.lineHeightSmall * 0.5f;
    // Just right of the line's last point, vertically centred on it. The plotting
    // rect already reserves TAG_WIDTH_CHARS on the right so a finisher's tag fits.
    addString(buf, tagX, tagY,
              Justify::LEFT, this->getFont(FontCategory::SMALL), color, dims.fontSizeSmall);

    // Click-to-spectate on the tag (Standings/Map/Event Log all offer it). The tag is the
    // only part of a chart that identifies a rider, so it is the natural target — the lines
    // themselves overlap too much to hit-test meaningfully. Gated on isRiderSpectatable, so
    // a retired rider's line keeps its label but the label is inert.
    //
    // The SAME chart can be drawn more than once per rebuild (one per enabled chart type),
    // so a rider gets one region per chart — all with the same raceNum, which is harmless:
    // whichever is hit requests the same rider.
    if (PluginData::getInstance().isRiderSpectatable(raceNum)) {
        TagClickRegion region;
        region.x = tagX;
        region.y = tagY;
        // Tag text is "#" + up to 3 digits; size the box from the glyph metrics rather
        // than the drawn string so a 1-digit number is still comfortably clickable.
        region.width = PluginUtils::calculateMonospaceTextWidth(TAG_WIDTH_CHARS, dims.fontSizeSmall);
        region.height = dims.lineHeightSmall;
        region.raceNum = raceNum;
        applyOffset(region.x, region.y);
        m_tagClickRegions.push_back(region);
    }
}

SessionChartsHud::TestSeries SessionChartsHud::testSeries() const {
    FieldData field;
    collectField(field);
    TestSeries out;
    out.pointsPerLap = field.pointsPerLap;
    const int displayRaceNum = PluginData::getInstance().getDisplayRaceNum();
    for (size_t i = 0; i < field.raceNums.size(); ++i) {
        if (field.raceNums[i] == displayRaceNum) {
            out.points = static_cast<int>(field.cumulative[i].size());
            break;
        }
    }
    return out;
}

void SessionChartsHud::handleClick(float mouseX, float mouseY) {
    for (const auto& region : m_tagClickRegions) {
        if (isPointInRect(mouseX, mouseY, region.x, region.y, region.width, region.height)) {
            DEBUG_INFO_F("SessionChartsHud: Switching to rider #%d", region.raceNum);
            PluginManager::getInstance().requestSpectateRider(region.raceNum);
            return;   // Only process one click
        }
    }
}

// ---------------------------------------------------------------------------
// Shared axis labels (Y-range pair beside the plot + "L1"/"L<max>" below it)
// ---------------------------------------------------------------------------

void SessionChartsHud::addChartAxisLabels(float px, float py, float pw, float ph, float domainEndLaps,
                                          const char* topLabel, const char* botLabel,
                                          const ScaledDimensions& dims) {
    const unsigned long lc = this->getColor(ColorSlot::TERTIARY);
    const int f = this->getFont(FontCategory::SMALL);
    if (topLabel)
        addString(topLabel, px - dims.paddingH * STRIP_CHART_LABEL_INSET, py,
                  Justify::RIGHT, f, lc, dims.fontSizeSmall);
    if (botLabel)
        addString(botLabel, px - dims.paddingH * STRIP_CHART_LABEL_INSET, py + ph - dims.lineHeightSmall,
                  Justify::RIGHT, f, lc, dims.fontSizeSmall);
    addString("L1", px, py + ph + dims.lineHeightSmall * 0.2f, Justify::LEFT, f, lc, dims.fontSizeSmall);
    // The lap the RIGHT EDGE falls in, which is not always maxLap. At sector resolution the
    // domain ends at field.maxLaps — the furthest-along rider's actual progress — so a
    // leader a third of the way through lap 4 puts the edge at 3.33 and "L3" under-labels
    // it by a third of a lap. ceil() names the lap that edge is inside, and is a no-op at
    // per-lap resolution (maxLaps is then exactly maxLap), so the default rendering stays
    // byte-identical.
    const int edgeLap = static_cast<int>(std::ceil(domainEndLaps));
    if (edgeLap > 1) {
        char buf[8];
        snprintf(buf, sizeof(buf), "L%d", edgeLap);
        addString(buf, px + pw, py + ph + dims.lineHeightSmall * 0.2f, Justify::RIGHT, f, lc, dims.fontSizeSmall);
    }
}

// ---------------------------------------------------------------------------
// Defaults
// ---------------------------------------------------------------------------

void SessionChartsHud::resetToDefaults() {
    m_bVisible = false;  // Disabled by default - enable via settings
    m_bShowTitle = true;
    setTextureVariant(0);
    m_fBackgroundOpacity = SettingsLimits::DEFAULT_OPACITY;
    m_fScale = 1.0f;
    // Upper-right by default. A single chart fits comfortably here; multi-chart
    // stacks are tall (each chart is 10 rows) and a full stack exceeds the screen,
    // so users reposition/scale to taste like any HUD.
    setPosition(0.7315f, 0.011734f);

    m_enabledCharts = CHART_DEFAULT;
    m_enabledElements = ELEM_DEFAULT;
    m_riderColorMode = RiderColorMode::POSITION_PALETTE;
    m_topPositionsCount = 3;
    m_displayRowCount = 10;   // ~fills a 10-row chart's end-of-line tags without overlap
    m_outlierFactor = 1.4f;

    setDataDirty();
}
