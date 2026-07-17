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
    // Chart data only changes on lap completion / spectate / session change, all
    // of which set the data-dirty flag; positions are derived, not polled.
    processDirtyFlags();
}

bool SessionChartsHud::handlesDataType(DataChangeType dataType) const {
    switch (dataType) {
        case DataChangeType::LapLog:         // a rider completed a lap (the data source)
        case DataChangeType::SpectateTarget: // player-window recenters on the new target
        case DataChangeType::SessionData:    // new session clears the field
        case DataChangeType::RaceEntries:    // grid/entries changed
            return true;
        default:
            // Deliberately NOT Standings: it fires many times/sec on full grids,
            // and the chart series only change when a lap completes (LapLog).
            return false;
    }
}

// ---------------------------------------------------------------------------
// Data collection
// ---------------------------------------------------------------------------

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

    // Cumulative race time (drives the race trace; race only).
    field.cumulative.assign(order.size(), {});
    for (size_t i = 0; i < order.size(); ++i) {
        field.cumulative[i] = SessionChartsMath::cumulative(field.lapMs[i]);
    }

    // Ranking basis for the position and gap charts: cumulative race time in a
    // race, best-lap-so-far otherwise. The latter gives the provisional
    // qualifying/practice order and each rider's gap to the session-best lap.
    std::vector<std::vector<long long>> rank(order.size());
    for (size_t i = 0; i < order.size(); ++i) {
        rank[i] = field.isRace ? field.cumulative[i]
                               : SessionChartsMath::bestLapSoFar(field.lapMs[i], field.lapValid[i]);
    }
    field.positions = SessionChartsMath::positionsPerLap(rank, field.raceNums);
    field.gaps = SessionChartsMath::gapToLeaderPerLap(rank);

    // Reference pace / trace need a mass start — race only.
    if (field.isRace) {
        int leader = SessionChartsMath::leaderIndex(field.cumulative, field.raceNums);
        if (leader >= 0 && !field.cumulative[leader].empty()) {
            field.refPaceMs = SessionChartsMath::referencePaceMs(
                field.cumulative[leader].back(),
                static_cast<int>(field.cumulative[leader].size()));
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
    for (int lap = 0; lap < field.maxLap; ++lap) {
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
            Pt cur{ xForLap(px, pw, lap, field.maxLap), yForRow(rowOf[di][lap]), true };
            if (prev.ok) addLineSegment(prev.x, prev.y, cur.x, cur.y, d.color, thick);
            if (m_enabledElements & ELEM_DOTS) addDot(cur.x, cur.y, d.color, dotSize);
            prev = cur; tag = cur;
        }
        if ((m_enabledElements & ELEM_LEGEND) && tag.ok)
            addRiderTag(tag.x, tag.y, field.raceNums[d.fieldIdx], d.color);
    }

    if (m_enabledElements & ELEM_AXIS_LABELS) {
        // Y is order among the SHOWN riders, so label the top/bottom rows with the
        // actual positions of the shown riders occupying them at the latest lap.
        int topPos = 0, botPos = 0;
        for (int lap = field.maxLap - 1; lap >= 0; --lap) {
            int best = -1, worst = -1;
            for (int di = 0; di < K; ++di) {
                const std::vector<int>& p = field.positions[drawn[di].fieldIdx];
                if (lap >= static_cast<int>(p.size()) || p[lap] <= 0) continue;
                if (best < 0 || p[lap] < best)  best = p[lap];
                if (worst < 0 || p[lap] > worst) worst = p[lap];
            }
            if (best > 0) { topPos = best; botPos = worst; break; }
        }
        char topBuf[8], botBuf[8];
        if (topPos > 0) {
            snprintf(topBuf, sizeof(topBuf), "P%d", topPos);
            snprintf(botBuf, sizeof(botBuf), "P%d", botPos);
        }
        addChartAxisLabels(px, py, pw, ph, field.maxLap,
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
            vals.push_back(SessionChartsMath::traceValueMs(field.refPaceMs, static_cast<int>(l) + 1, cum[l]));
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
            long long v = SessionChartsMath::traceValueMs(field.refPaceMs, static_cast<int>(l) + 1, cum[l]);
            Pt cur{ xForLap(px, pw, static_cast<int>(l), field.maxLap), yForVal(v), true };
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
        addChartAxisLabels(px, py, pw, ph, field.maxLap,
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
            Pt cur{ xForLap(px, pw, static_cast<int>(l), field.maxLap), yForGap(gaps[l]), true };
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
        addChartAxisLabels(px, py, pw, ph, field.maxLap, "0.0s", botBuf, dims);
    }
}

// ---------------------------------------------------------------------------
// Chart 4: Pace (raw lap time per lap)
// ---------------------------------------------------------------------------

void SessionChartsHud::drawPaceChart(float px, float py, float pw, float ph,
                                  const FieldData& field, const std::vector<DrawnRider>& drawn) {
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
            Pt cur{ xForLap(px, pw, static_cast<int>(l), field.maxLap), yForVal(laps[l]), true };
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
        addChartAxisLabels(px, py, pw, ph, field.maxLap,
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
    // Just right of the line's last point, vertically centred on it. The plotting
    // rect already reserves TAG_WIDTH_CHARS on the right so a finisher's tag fits.
    addString(buf, x + dims.paddingH * 0.25f, y - dims.lineHeightSmall * 0.5f,
              Justify::LEFT, this->getFont(FontCategory::SMALL), color, dims.fontSizeSmall);
}

// ---------------------------------------------------------------------------
// Shared axis labels (Y-range pair beside the plot + "L1"/"L<max>" below it)
// ---------------------------------------------------------------------------

void SessionChartsHud::addChartAxisLabels(float px, float py, float pw, float ph, int maxLap,
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
    if (maxLap > 1) {
        char buf[8];
        snprintf(buf, sizeof(buf), "L%d", maxLap);
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
