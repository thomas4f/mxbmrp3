// ============================================================================
// hud/dash_hud_pages.cpp
// Dash - the pages that are the card's version of a whole HUD: the Standings
// HUD's rows around the displayed rider, the Telemetry HUD's graph, the Delta
// Trace HUD's chart, the Event Log's newest rows and the Session HUD's rows.
// See dash_hud.h.
// ============================================================================
#include "dash_hud.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

#include "delta_trace_hud.h"
#include "event_log_hud.h"
#include "gap_bar_hud.h"
#include "marker_label.h"
#include "session_charts_hud.h"
#include "session_charts_math.h"
#include "plate_geometry.h"
#include "session_hud.h"
#include "standings_gap_plan.h"
#include "standings_hud.h"
#include "telemetry_hud.h"
#include "timing_hud.h"
#include "../core/asset_manager.h"
#include "../core/color_config.h"
#include "../core/hotkey_manager.h"
#include "../core/hud_manager.h"
#include "../core/plugin_constants.h"
#include "../core/plugin_data.h"
#include "../core/plugin_utils.h"
#include "../core/tracked_riders_manager.h"
#include "../core/pixel_text.h"
#include "../core/update_checker.h"

using namespace PluginConstants;

namespace {
// Standings rows in digit cells: the position ("40"), the race-number plate
// (four wide, the number centred on it) and its brand mark, then the short name,
// as on the Standings HUD; the gap right-aligned at the card's edge.
constexpr float PLATE_COLUMN = 3.5f;
constexpr float PLATE_CELLS = 4.0f;
constexpr float MARK_GAP_CELLS = 0.3f;     // plate to brand mark, as StandingsHud::PlateGeometry
constexpr float MARK_CELLS = 0.85f;
constexpr float NAME_COLUMN = 9.0f;
constexpr int LAP_TIME_CELLS = 9;          // "1:23.456" and the space before it
// The Standings HUD's status column before the position, when that HUD shows
// it: its width and icon size (StandingsHud's COL_TRACKED_WIDTH and
// STATUS_ICON_HALF_RATIO), the icon centred one cell in.
constexpr float STATUS_CELLS = 3.0f;
constexpr float STATUS_ICON_HALF_RATIO = 0.30f;
}  // namespace

void DashHud::drawStandings(float rowLeft, float right, float contentY, const ScaledDimensions& dim) {
    findNeighbours(m_shownRows);
    const PluginData& pd = PluginData::getInstance();
    StandingsHud& standings = HudManager::getInstance().getStandingsHud();
    const float charW = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize);
    // The status icons first where the Standings HUD shows them, the row after.
    const bool status = standings.showsStatusColumn();
    const bool penalties = standings.showsPenaltyColumn();
    const float left = status ? rowLeft + charW * STATUS_CELLS : rowLeft;
    const float plateX = left + charW * PLATE_COLUMN;
    const float plateW = charW * PLATE_CELLS;
    const float nameX = left + charW * NAME_COLUMN;
    const int digits = getFont(FontCategory::DIGITS);
    const int normal = getFont(FontCategory::NORMAL);
    const unsigned long primary = getColor(ColorSlot::PRIMARY);
    const unsigned long secondary = getColor(ColorSlot::SECONDARY);
    const unsigned long muted = getColor(ColorSlot::MUTED);
    const bool classic = standings.isClassicLayout();
    const bool highlight = standings.getPlayerRowHighlight();
    // The Standings HUD's Short name length, cut to what the card leaves
    // beside a full lap time.
    // (A hair over, so a whole number of cells is not floored one short.)
    const int fitChars = static_cast<int>((right - nameX) / charW + 0.01f) - LAP_TIME_CELLS;
    const int nameChars = std::clamp(standings.getShortNameChars(), 1, std::max(1, fitChars));

    // The tracked colours, re-read by name only for a new rider or tracked list.
    const uint32_t revision = pd.getTrackedRidersRevision();
    if (revision != m_plateTintRevision) {
        m_plateTintRevision = revision;
        for (PlateTint& t : m_plateTint) t.raceNum = -1;
    }
    // The brand mark, the Standings HUD's caret where the icon set has one.
    const AssetManager& assets = AssetManager::getInstance();
    if (m_caretSpriteGen != assets.themeGeneration()) {
        m_caretSpriteGen = assets.themeGeneration();
        m_caretSprite = assets.getIconSpriteIndex("caret-up");
    }

    auto row = [&](const Neighbour& n, float y, PlateTint& tint) {
        const bool isSelf = n.self;
        const RaceEntryData* entry = pd.getRaceEntry(n.raceNum);
        const char* riderName = entry ? entry->name : "";
        if (tint.raceNum != n.raceNum || std::strncmp(tint.name, riderName, sizeof(tint.name) - 1) != 0) {
            tint.raceNum = n.raceNum;
            snprintf(tint.name, sizeof(tint.name), "%s", riderName);
            const TrackedRiderConfig* tracked =
                entry ? TrackedRidersManager::getInstance().getTrackedRider(std::string(entry->name)) : nullptr;
            tint.color = tracked ? tracked->color : 0;
            tint.sprite = tracked ? AssetManager::getInstance().iconSpriteForShape(tracked->shapeIndex) : 0;
        }
        const bool out = n.state == RiderState::DNS || n.state == RiderState::DSQ ||
                         n.state == RiderState::RETIRED;
        // The displayed rider's band, as the Standings HUD's player row.
        if (isSelf && highlight) {
            const unsigned long brand = entry ? entry->bikeBrandColor : BrandColors::DEFAULT;
            const unsigned long band = standings.getPlayerRowHighlightBrand()
                ? (brand == BrandColors::DEFAULT ? muted : brand)
                : getColor(ColorSlot::ACCENT);
            addRowHighlight(rowLeft, y, right - rowLeft, dim.lineHeightNormal,
                            PluginUtils::applyOpacity(band, ROW_SELECT_ALPHA));
        }
        // The status marker, else the tracked rider's shape, as the Standings
        // HUD's status column; none for a rider out of the race.
        if (status && !out) {
            StandingsHud::Status mark;
            mark.mark = static_cast<StandingsHud::StatusMark>(n.statusMark);
            mark.flag = static_cast<RiderFlagIcons::Kind>(n.statusFlag);
            RiderFlagIcons::Icon icon = standings.statusIcon(mark);
            if (mark.mark == StandingsHud::StatusMark::None && tint.sprite > 0) {
                icon.sprite = tint.sprite;
                icon.color = tint.color;
            }
            if (icon.sprite > 0) {
                const float halfH = dim.fontSize * STATUS_ICON_HALF_RATIO;
                const float halfW = halfH / UI_ASPECT_RATIO;
                float x = rowLeft + charW - halfW;
                float iy = y + dim.lineHeightNormal * 0.5f - halfH;
                applyOffset(x, iy);
                SPluginQuad_t quad;
                setQuadPositions(quad, x, iy, halfW * 2.0f, halfH * 2.0f);
                quad.m_iSprite = icon.sprite;
                quad.m_ulColor = icon.color;
                m_quads.push_back(quad);
                shadowLastIcon();
            }
        }
        // The position in its podium colour, else tertiary.
        unsigned long posColor = getColor(ColorSlot::TERTIARY);
        if (out) posColor = muted;
        else if (n.position == 1) posColor = PodiumColors::GOLD;
        else if (n.position == 2) posColor = PodiumColors::SILVER;
        else if (n.position == 3) posColor = PodiumColors::BRONZE;
        char pos[8];
        snprintf(pos, sizeof(pos), "%d", n.position);
        addString(pos, left, y, Justify::LEFT, digits, posColor, dim.fontSize);
        // The race number on its plate with the brand mark beside it, or bare in
        // the classic layout, as the Standings HUD draws it.
        unsigned long numColor = out ? muted : secondary;
        if (!classic) {
            const float plateH = PlateLayout::plateHeight(dim.lineHeightNormal);
            const float plateY = y + PlateLayout::platePadY(dim.lineHeightNormal);
            addRect(plateX, plateY, plateW, plateH,
                    PluginUtils::applyOpacity(StandingsHud::plateColor(out, tint.color, n.finished, n.position,
                                                                       muted, secondary),
                                              PlateLayout::kPlateAlpha));
            const float markH = dim.fontSize * PlateLayout::kBrandMarkHeightRatio;
            float markX = plateX + plateW + charW * MARK_GAP_CELLS;
            float markY = plateY + (plateH - markH) * 0.5f;
            applyOffset(markX, markY);
            SPluginQuad_t mark;
            if (m_caretSprite > 0) {
                // Pixel-square and centred in the mark's slot, as the Standings HUD.
                const float w = markH / UI_ASPECT_RATIO;
                setQuadPositionsRotatedCW(mark, markX + (charW * MARK_CELLS - w) * 0.5f, markY, w, markH);
                mark.m_iSprite = m_caretSprite;
            } else {
                setQuadPositionsArrowRight(mark, markX, markY, charW * MARK_CELLS, markH);
                mark.m_iSprite = SpriteIndex::SOLID_COLOR;
            }
            mark.m_ulColor = PluginUtils::applyOpacity(entry ? entry->bikeBrandColor : BrandColors::DEFAULT,
                                                       out ? PlateLayout::kBrandMarkOutAlpha : PlateLayout::kPlateAlpha);
            m_quads.push_back(mark);
            // Dark on the plate; light on a dark tracked one.
            numColor = getColor(ColorSlot::BACKGROUND);
            if (!out && tint.color != 0 && PluginUtils::isColorDark(tint.color)) numColor = primary;
        }
        // On a plate in the title face without a shadow, as the Standings HUD's
        // plate number; bare in the classic layout, a digits column there.
        char num[8];
        snprintf(num, sizeof(num), "%d", n.raceNum);
        addString(num, plateX + plateW * 0.5f, y, Justify::CENTER, classic ? digits : getFont(FontCategory::TITLE),
                  numColor, dim.fontSize, /*skipShadow=*/!classic);
        // The plan into characters and colours, as the Standings HUD's gap
        // column (formatEntryGap and renderRiderRow): the side tint first,
        // else tertiary for a label, secondary for a live gap.
        char gapText[16];
        unsigned long gapColor = primary;
        if (n.gapStyle == static_cast<uint8_t>(StandingsGap::Style::LABEL)) gapColor = getColor(ColorSlot::TERTIARY);
        else if (n.gapStyle == static_cast<uint8_t>(StandingsGap::Style::LIVE)) gapColor = secondary;
        switch (static_cast<StandingsGap::Kind>(n.gapKind)) {
            case StandingsGap::Kind::TimeDiff:
                PluginUtils::formatTimeDiff(gapText, sizeof(gapText), n.gapValue);
                break;
            case StandingsGap::Kind::LapDiff:
                snprintf(gapText, sizeof(gapText), "%+dL", n.gapValue);
                break;
            case StandingsGap::Kind::LapTime:
                PluginUtils::formatLapTime(n.gapValue, gapText, sizeof(gapText));
                break;
            case StandingsGap::Kind::StateAbbr:
                snprintf(gapText, sizeof(gapText), "%s", PluginUtils::getRiderStateAbbreviation(n.state));
                break;
            case StandingsGap::Kind::Label:   // the leader's row against the leader (the self row shows its best lap)
                snprintf(gapText, sizeof(gapText), "%s", "Leader");
                break;
            case StandingsGap::Kind::Empty:
                gapText[0] = '\0';
                break;
            default:   // Placeholder
                snprintf(gapText, sizeof(gapText), "%s", Placeholders::GENERIC);
                gapColor = muted;
                break;
        }
        const auto side = static_cast<StandingsGap::Tint>(n.gapTint);
        if (side == StandingsGap::Tint::Ahead) gapColor = getColor(ColorSlot::NEGATIVE);
        else if (side == StandingsGap::Tint::Behind) gapColor = getColor(ColorSlot::POSITIVE);
        if (out) gapColor = muted;
        if (gapText[0]) addString(gapText, right, y, Justify::RIGHT, digits, gapColor, dim.fontSize);
        // A time penalty, while the Standings HUD's penalty column is on: in its
        // amber, right before the gap, the name cut to make room on that row.
        int rowNameChars = nameChars;
        if (penalties && n.penaltyMs > 0) {
            char penalty[16];
            StandingsHud::formatPenalty(n.penaltyMs, penalty, sizeof(penalty));
            addString(penalty, right - charW * static_cast<float>(LAP_TIME_CELLS), y, Justify::RIGHT, digits,
                      out ? muted : getColor(ColorSlot::WARNING), dim.fontSize);
            const int penaltyChars = static_cast<int>(std::strlen(penalty)) + 1;
            rowNameChars = std::clamp(fitChars - penaltyChars, 1, nameChars);
        }
        // The short name; the accent marks the displayed rider when the band is off.
        char name[32];
        snprintf(name, sizeof(name), "%.*s", rowNameChars, entry ? entry->name : Placeholders::GENERIC);
        const unsigned long nameColor = out ? muted : (isSelf && !highlight) ? getColor(ColorSlot::ACCENT) : primary;
        addString(name, nameX, y, Justify::LEFT, normal, nameColor, dim.fontSize);
    };
    // With no standings yet, one "-" in the middle row.
    if (m_shownRows[1].raceNum < 0 && m_shownRows[0].raceNum < 0) {
        addString(Placeholders::GENERIC, left, contentY + dim.lineHeightNormal, Justify::LEFT, digits, muted,
                  dim.fontSize);
        return;
    }
    for (int i = 0; i < STANDINGS_ROWS; ++i) {
        if (m_shownRows[i].raceNum >= 0)
            row(m_shownRows[i], contentY + dim.lineHeightNormal * static_cast<float>(i), m_plateTint[i]);
    }
}

void DashHud::drawTelemetry(float x, float y, float w, float h) {
    // The Telemetry HUD's chart: its grid lines, then its traces, its inputs
    // (its Elements) in its colours, newest at the right, throttle drawn last so
    // it sits on top. Without its axis labels, which the card has no room for.
    // Rear brake, clutch and the suspension come only on track (RunTelemetry),
    // so are left off otherwise.
    const PluginData& pd = PluginData::getInstance();
    const HistoryBuffers& history = pd.getHistoryBuffers();
    m_shownTelemetryRevision = history.revision;
    const uint32_t on = HudManager::getInstance().getTelemetryHud().getEnabledElements();
    const bool full = pd.getDrawState() == ViewState::ON_TRACK;
    const BikeTelemetryData& bike = pd.getBikeTelemetry();
    addStripChartFrame(x, y, w, h, "", "", "", getScaledDimensions());
    const float thickness = stripChartLineThickness();
    constexpr size_t N = HistoryBuffers::MAX_TELEMETRY_HISTORY;
    auto trace = [&](uint32_t element, bool available, const HistoryRing<float, N>& data, unsigned long color) {
        if ((on & element) && available) addStripChartHistoryLine(data, color, x, y, w, h, thickness, N);
    };
    trace(TelemetryHud::ELEM_FRONT_BRAKE, true, history.frontBrake, SemanticColors::FRONT_BRAKE);
    trace(TelemetryHud::ELEM_REAR_BRAKE, full, history.rearBrake, SemanticColors::REAR_BRAKE);
    trace(TelemetryHud::ELEM_CLUTCH, full, history.clutch, SemanticColors::CLUTCH);
    trace(TelemetryHud::ELEM_RPM, true, history.rpm, ColorPalette::GRAY);
    trace(TelemetryHud::ELEM_FRONT_SUSP, full && bike.frontSuspMaxTravel > 0, history.frontSusp,
          SemanticColors::FRONT_SUSP);
    trace(TelemetryHud::ELEM_REAR_SUSP, full && bike.rearSuspMaxTravel > 0, history.rearSusp,
          SemanticColors::REAR_SUSP);
    trace(TelemetryHud::ELEM_GEAR, true, history.gear, SemanticColors::GEAR);
    trace(TelemetryHud::ELEM_THROTTLE, true, history.throttle, SemanticColors::THROTTLE);
}

void DashHud::drawDeltaTrace(float x, float y, float w, float h, const ScaledDimensions& dim) {
    // The Delta Trace HUD's chart, against its reference and with its Peaks,
    // across the card. Without its axis labels, which the card has no room for.
    const DeltaTraceHud& hud = HudManager::getInstance().getDeltaTraceHud();
    m_deltaPlot.draw(*this, hud.getReference(), x, y, w, h, hud.showsPeaks(), /*axisLabels=*/false, dim);
}

void DashHud::drawGapChart(float x, float y, float w, float h, const ScaledDimensions& dim) {
    // The Session Charts HUD's Gap chart for three riders: the leader the top
    // edge, the gap growing downward, the displayed rider's line in the accent
    // colour and weight it has there, the rider ahead's in the Standings' ahead
    // colour and the rider behind's in its behind colour. Its grid (lap lines
    // included), dots and Line labels ("#num" at each line's end, in a right
    // margin as wide as its own), at its resolution (Sector points in a race);
    // without its axis labels, which the card has no room for. Each line is its
    // rider's whole race, so a pass redraws them.
    m_shownGapChart = readGapChart();
    const uint32_t on = m_shownGapChart.elements;
    SessionChartsHud::NeighbourGaps& gaps = m_gapSeries;
    HudManager::getInstance().getSessionChartsHud().neighbourGaps(gaps);
    const bool tags = (on & SessionChartsHud::ELEM_LEGEND) != 0;
    const float plotW = tags
        ? w - PluginUtils::calculateMonospaceTextWidth(SessionChartsHud::TAG_WIDTH_CHARS, dim.fontSizeSmall)
        : w;

    // The parent's robust range: a rider minutes behind does not squash the
    // others against the leader; their line clips to the bottom edge.
    m_gapRangeSample.clear();
    for (const std::vector<long long>* s : { &gaps.ahead, &gaps.mine, &gaps.behind })
        for (long long g : *s)
            if (g < SessionChartsMath::kNoValidLap / 2) m_gapRangeSample.push_back(g);
    const SessionChartsMath::AxisRange range = SessionChartsMath::robustRange(m_gapRangeSample);
    const long long gapMax = std::max<long long>(1000, range.valid ? range.hi : 0);

    // One x scale for the three, the longest series across the card.
    const size_t points = std::max(gaps.mine.size(), std::max(gaps.ahead.size(), gaps.behind.size()));
    if (on & SessionChartsHud::ELEM_GRID) {
        const float grid = stripChartGridThickness();
        const unsigned long gridColor = getColor(ColorSlot::MUTED);
        addHorizontalGridLine(x, y, plotW, gridColor, grid);       // the leader
        addHorizontalGridLine(x, y + h, plotW, gridColor, grid);
        // Point i is (i + 1) / pointsPerLap laps in, as the parent places it.
        const float perLap = static_cast<float>(std::max(1, gaps.pointsPerLap));
        if (points > 1) addChartLapLines(x, y, plotW, h, 1.0f / perLap, static_cast<float>(points) / perLap);
    }
    if (gaps.mine.empty()) {
        addString(Placeholders::GENERIC, x + w * 0.5f, y + (h - dim.lineHeightNormal) * 0.5f, Justify::CENTER,
                  getFont(FontCategory::DIGITS), getColor(ColorSlot::MUTED), dim.fontSize);
        return;
    }
    const float lastIndex = static_cast<float>(points - 1);
    const float thin = 0.0022f * dim.scale;
    const float dotSize = 0.004f * dim.scale;
    // Where each line ends, for its tag: top to bottom once drawn.
    struct Tag { float y; int raceNum; unsigned long color; };
    Tag tagAt[3];
    int tagCount = 0;
    auto line = [&](const std::vector<long long>& series, int raceNum, unsigned long color, float thickness) {
        bool hasPrev = false;
        float prevX = 0.0f, prevY = 0.0f;
        for (size_t i = 0; i < series.size(); ++i) {
            const long long g = std::max<long long>(0, std::min(gapMax, series[i]));
            const float px = x + plotW * SessionChartsMath::xFracForLaps(static_cast<float>(i), 0.0f, lastIndex);
            const float py = y + static_cast<float>(g) / static_cast<float>(gapMax) * h;
            if (hasPrev) addLineSegment(prevX, prevY, px, py, color, thickness);
            if (on & SessionChartsHud::ELEM_DOTS) addDot(px, py, color, dotSize);
            hasPrev = true;
            prevX = px;
            prevY = py;
        }
        if (hasPrev && raceNum >= 0) tagAt[tagCount++] = { prevY, raceNum, color };
    };
    // The neighbours first, so the rider's own line sits on top.
    line(gaps.ahead, gaps.aheadNum, getColor(ColorSlot::NEGATIVE), thin);
    line(gaps.behind, gaps.behindNum, getColor(ColorSlot::POSITIVE), thin);
    line(gaps.mine, gaps.mineNum, getColor(ColorSlot::ACCENT), thin * 1.6f);
    if (!tags) return;

    // The three lines end close together by definition (the riders around the
    // rider), and the card is three rows tall where the parent's chart is ten, so
    // each tag starts at its line's end and is pushed one small row clear of the
    // one above, then the stack is pulled back inside the card from the bottom.
    std::sort(tagAt, tagAt + tagCount, [](const Tag& a, const Tag& b) { return a.y < b.y; });
    const float rowH = dim.lineHeightSmall;
    const float top = y + rowH * 0.5f, bottom = y + h - rowH * 0.5f;
    for (int i = 0; i < tagCount; ++i) {
        tagAt[i].y = std::max(tagAt[i].y, top);
        if (i > 0) tagAt[i].y = std::max(tagAt[i].y, tagAt[i - 1].y + rowH);
    }
    for (int i = tagCount - 1; i >= 0; --i) {
        tagAt[i].y = std::min(tagAt[i].y, i == tagCount - 1 ? bottom : tagAt[i + 1].y - rowH);
    }
    // The parent's tag: just right of the line's end, centred on it.
    const float tagX = x + plotW + dim.paddingH * 0.25f;
    for (int i = 0; i < tagCount; ++i) {
        char buf[8];
        snprintf(buf, sizeof(buf), "#%d", tagAt[i].raceNum);
        addString(buf, tagX, tagAt[i].y - rowH * 0.5f, Justify::LEFT, getFont(FontCategory::SMALL),
                  tagAt[i].color, dim.fontSizeSmall);
    }
}

void DashHud::drawEvents(float left, float right, float contentY, const ScaledDimensions& dim) {
    // The Event Log's newest rows that its filter lets through, in its order
    // (newest at the bottom by default, as there), each with its icon and its
    // message, the detail in the digits font after it. No timestamp: the card
    // has room for the message or the time, and the message is the event.
    m_shownEvents = readEvents();
    const EventLogHud& hud = HudManager::getInstance().getEventLogHud();
    const std::deque<EventLogEntry>& log = PluginData::getInstance().getEventLog();
    const EventLogEntry* rows[EVENT_ROWS] = {};
    int count = 0;
    for (auto it = log.rbegin(); it != log.rend() && count < EVENT_ROWS; ++it)
        if (m_shownEvents.filter & eventLogTypeToFlag(it->type)) rows[count++] = &*it;
    const bool newestFirst = m_shownEvents.order == static_cast<uint8_t>(EventLogHud::DisplayOrder::NEWEST_FIRST);

    const float charW = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize);
    const float iconColW = charW * static_cast<float>(EventLogHud::ICON_COL_WIDTH);
    const float messageX = left + (m_shownEvents.icons ? iconColW : 0.0f);
    const int chars = std::max(1, static_cast<int>((right - messageX) / charW + 0.001f));
    const float iconSize = EventLogHud::ICON_BASE_SIZE * getScale() * 2.0f;
    const int normal = getFont(FontCategory::NORMAL);
    const int digits = getFont(FontCategory::DIGITS);
    const unsigned long secondary = getColor(ColorSlot::SECONDARY);
    const unsigned long primary = getColor(ColorSlot::PRIMARY);

    for (int i = 0; i < count; ++i) {
        const EventLogEntry& e = *rows[i];
        // rows[] runs newest first; oldest first puts the newest on the bottom row.
        const int row = newestFirst ? i : EVENT_ROWS - 1 - i;
        const float y = contentY + dim.lineHeightNormal * static_cast<float>(row);
        const int sprite = hud.getIconForEvent(e.type);
        if (sprite > 0) {
            const unsigned long ink = e.iconColorSlot >= 0 ? hud.getColor(static_cast<ColorSlot>(e.iconColorSlot))
                                                            : hud.getIconColorForEvent(e.type);
            addIcon(left + iconColW * 0.25f, y + dim.lineHeightNormal * 0.5f, sprite, ink, iconSize);
            shadowLastIcon();
        }
        char text[72];
        if (e.detail[0] == '\0') {
            const int len = snprintf(text, sizeof(text), "%s", e.message);
            PluginUtils::fitTextInPlace(text, static_cast<size_t>(std::max(0, len)), chars);
            addString(text, messageX, y, Justify::LEFT, normal, secondary, dim.fontSize);
            continue;
        }
        // "Message:" then the detail, the message cut so the detail always fits.
        const int detailLen = static_cast<int>(strlen(e.detail));
        const int len = snprintf(text, sizeof(text), "%s:", e.message);
        const size_t cut = PluginUtils::fitTextInPlace(text, static_cast<size_t>(std::max(0, len)),
                                                       std::max(1, chars - detailLen - 1));
        addString(text, messageX, y, Justify::LEFT, normal, secondary, dim.fontSize);
        addString(e.detail, messageX + charW * static_cast<float>(cut + 1), y, Justify::LEFT, digits, primary,
                  dim.fontSize);
    }
}

void DashHud::drawSessionInfo(float left, float right, float contentY, const ScaledDimensions& dim) {
    // The Session HUD's rows (its Rows and icons) in its order: the server as
    // the heading, then the track, the format and the weather, each with its
    // icon. The card holds three: with all four on, the weather gives way.
    readSessionInfo(m_shownSessionInfo);
    const SessionView& v = m_shownSessionInfo;
    const SessionData& session = PluginData::getInstance().getSessionData();
    bool server = (v.rows & SessionHud::ROW_SERVER) != 0;
    bool track = (v.rows & SessionHud::ROW_TRACK) != 0;
    bool format = (v.rows & SessionHud::ROW_FORMAT) != 0;
    bool weather = (v.rows & SessionHud::ROW_WEATHER) != 0 && v.conditions >= 0;
    if (server && track && format && weather) weather = false;
    const int shown = (server ? 1 : 0) + (track ? 1 : 0) + (format ? 1 : 0) + (weather ? 1 : 0);
    if (shown == 0) return;

    const float charW = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize);
    // The row icon and the text after it, as on the Session HUD.
    const float iconSize = dim.fontSize * SessionHud::ICON_SIZE_FACTOR;
    const float textX = left + (v.icons ? iconSize / UI_ASPECT_RATIO + dim.paddingH * 0.3f : 0.0f);
    const int chars = std::max(1, static_cast<int>((right - textX) / charW + 0.001f));
    const int headingChars = std::max(1, static_cast<int>((right - left) / charW + 0.001f));
    const unsigned long primary = getColor(ColorSlot::PRIMARY);
    const unsigned long muted = getColor(ColorSlot::MUTED);
    const int normal = getFont(FontCategory::NORMAL);
    const AssetManager& assets = AssetManager::getInstance();

    // Fewer rows than the card holds sit in its middle.
    float y = contentY + dim.lineHeightNormal * static_cast<float>(3 - shown) * 0.5f;
    char text[128];
    auto row = [&](const char* icon, unsigned long color) {
        if (v.icons) {
            const int sprite = assets.getIconSpriteIndex(icon);
            if (sprite > 0) {
                addIcon(left + iconSize * 0.5f / UI_ASPECT_RATIO, y + dim.fontSize * 0.5f, sprite, primary, iconSize);
                shadowLastIcon();
            }
        }
        PluginUtils::fitTextInPlace(text, strlen(text), chars);
        addString(text, textX, y, Justify::LEFT, normal, color, dim.fontSize);
        y += dim.lineHeightNormal;
    };
    if (server) {
        snprintf(text, sizeof(text), "%s",
                 PluginUtils::serverLabel(v.serverType, session.serverName, v.multiRider ? 2 : 1));
        PluginUtils::fitTextInPlace(text, strlen(text), headingChars);
        addSectionHeading(text, left, y, dim);
        y += sectionHeadingRowHeight(dim);
    }
    if (track) {
        const bool has = v.trackName[0] != '\0';
        snprintf(text, sizeof(text), "%s", has ? v.trackName : Placeholders::GENERIC);
        row(SessionHud::TRACK_ICON, has ? primary : muted);
    }
    if (format) {
        const bool real = SessionHud::formatLine(text, sizeof(text));
        row(SessionHud::FORMAT_ICON, real ? primary : muted);
    }
    if (weather) {
        SessionHud::weatherLine(text, sizeof(text));
        row(SessionHud::WEATHER_ICON, primary);
    }
}

void DashHud::drawFlatMap(float left, float width, float boxY, float boxH, const ScaledDimensions& dim) {
    const PluginData& data = PluginData::getInstance();
    const GapBarHud& bar = HudManager::getInstance().getGapBarHud();

    // The Gap Bar's split ticks: an eighth of the box at its top and bottom edges.
    if (bar.showsSplits()) {
        float splits[GapBarHud::SPLIT_SLOTS];
        const int n = bar.collectSplitPositions(splits);
        const float thickness = stripChartLineThickness();
        const unsigned long tickColor = PluginUtils::applyOpacity(getColor(ColorSlot::SECONDARY), 0.8f);
        const float tick = boxH * 0.125f;
        for (int i = 0; i < n; ++i) {
            const float x = left + width * splits[i];
            addLineSegment(x, boxY, x, boxY + tick, tickColor, thickness);
            addLineSegment(x, boxY + boxH - tick, x, boxY + boxH, tickColor, thickness);
        }
    }

    // Sized as the Gap Bar sizes them (its Marker scale), at this HUD's scale; the
    // displayed rider's a touch larger (MarkerLabel::PLAYER_BOOST). Centred as
    // there: the icon, or the icon and its label together.
    const GapBarHud::LabelMode labelMode = bar.getLabelMode();
    const GapBarHud::LabelAnchor anchor = bar.getLabelAnchor();
    const float markerScale = bar.getMarkerScale();
    const float iconSize = GapBarHud::DEFAULT_MARKER_BASE_SIZE * m_fScale * markerScale;
    const float boxCenterY = boxY + boxH / 2.0f;
    auto centerFor = [&](float halfSize, float boost) {
        if (labelMode == GapBarHud::LabelMode::NONE) return boxCenterY;
        return boxCenterY + MarkerLabel::blockCenterShift(anchor, halfSize, dim.fontSizeSmall * markerScale * boost);
    };

    // The race leader's crown, as on the Map (never the displayed rider's own
    // marker, which stays theirs).
    int leaderRaceNum = -1;
    if (data.isRaceSession()) {
        const std::vector<int>& order = data.getDisplayClassificationOrder();
        if (!order.empty()) leaderRaceNum = order.front();
    }
    const RiderFlagIcons::Icon crown = leaderRaceNum >= 0
        ? m_flagIcons.get(RiderFlagIcons::Kind::Leader, getColor(ColorSlot::NEGATIVE)) : RiderFlagIcons::Icon{};

    // The riders only: the Gap Bar's ghost (your best lap) has no place on a page
    // about where everyone is.
    GapBarHud::Marker markers[GapBarHud::MAX_MARKERS];
    const int count = bar.collectMarkers(m_riderPositions.data(), static_cast<int>(m_riderPositions.size()),
                                         true, false, markers, GapBarHud::MAX_MARKERS);
    for (int i = 0; i < count; ++i) {
        GapBarHud::Marker m = markers[i];
        if (m.kind == GapBarHud::MarkerKind::OPPONENT && m.raceNum == leaderRaceNum && crown.sprite > 0) {
            m.sprite = crown.sprite;
            m.shape = AssetManager::getInstance().shapeIndexForSprite(crown.sprite);
            m.color = crown.color;
        }
        const float boost = MarkerLabel::boost(m.kind == GapBarHud::MarkerKind::SELF);
        const float size = iconSize * boost;
        const float x = left + width * m.trackPos;
        const float y = centerFor(size / 2.0f, boost);
        SPluginQuad_t quad;
        GapBarHud::markerQuad(x, y, size, m.sprite, m.color, m.shape, quad);
        for (int c = 0; c < 4; ++c) applyOffset(quad.m_aafPos[c][0], quad.m_aafPos[c][1]);
        m_quads.push_back(quad);

        // The Gap Bar's labels.
        if (labelMode == GapBarHud::LabelMode::NONE) continue;
        const int position = data.getDisplayPositionForRaceNum(m.raceNum);
        char label[20];
        if (!MarkerLabel::format(labelMode, position, m.raceNum, label, sizeof(label))) continue;
        const float fontSize = dim.fontSizeSmall * markerScale * boost;
        const MarkerLabel::Placement lp = MarkerLabel::place(anchor, x, y, size / 2.0f, fontSize);
        addString(label, lp.x, lp.y, lp.justify, getFont(FontCategory::SMALL),
                  MarkerLabel::color(labelMode, position, getColor(ColorSlot::PRIMARY)), fontSize, false);
    }
}

void DashHud::addStepHint(float centerX, float width, float y, const ScaledDimensions& dim, float alpha) {
    // The keys as bound ("Page Up / Page Down"), the keyboard's or else the
    // controller's, over what they do and where to rebind them; with either
    // step unbound, where to bind them.
    m_shownHintKeys = hintKeys();
    const HotkeyManager& keys = HotkeyManager::getInstance();
    char names[2][40] = {};
    bool bound = true;
    const HotkeyAction actions[2] = { HotkeyAction::DASH_PREV, HotkeyAction::DASH_NEXT };
    for (int i = 0; i < 2; ++i) {
        const HotkeyBinding& b = keys.getBinding(actions[i]);
        if (b.hasKeyboard()) formatKeyBinding(b.keyboard, names[i], sizeof(names[i]));
        else if (b.hasController()) snprintf(names[i], sizeof(names[i]), "%s", getControllerButtonName(b.controller));
        else bound = false;
    }
    char line[96];
    if (bound) snprintf(line, sizeof(line), "%s / %s", names[0], names[1]);
    else snprintf(line, sizeof(line), "Bind Dash + / Dash -");
    const char* what = "to turn the pages";
    char where[32];
    snprintf(where, sizeof(where), "%s", bound ? "Rebind in Settings > Hotkeys" : "in Settings > Hotkeys");
    // A long binding (modifiers on both) is cut to the card, never wrapped; so is
    // the small line, should a theme's padding leave the card narrower.
    const int fits = static_cast<int>(width / PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize));
    PluginUtils::fitTextInPlace(line, std::strlen(line), fits);
    const int fitsSmall = static_cast<int>(width / PluginUtils::calculateMonospaceTextWidth(1, dim.fontSizeSmall));
    PluginUtils::fitTextInPlace(where, std::strlen(where), fitsSmall);
    // One line on each of the card's three rows.
    const unsigned long muted = Motion::scaleAlpha(getColor(ColorSlot::MUTED), alpha);
    addString(line, centerX, inkCenteredY(y, dim.lineHeightNormal, dim.fontSize), Justify::CENTER,
              getFont(FontCategory::STRONG), Motion::scaleAlpha(getColor(ColorSlot::PRIMARY), alpha), dim.fontSize);
    addString(what, centerX, inkCenteredY(y + dim.lineHeightNormal, dim.lineHeightNormal, dim.fontSizeSmall),
              Justify::CENTER, getFont(FontCategory::NORMAL), muted, dim.fontSizeSmall);
    addString(where, centerX, inkCenteredY(y + dim.lineHeightNormal * 2.0f, dim.lineHeightNormal, dim.fontSizeSmall),
              Justify::CENTER, getFont(FontCategory::NORMAL), muted, dim.fontSizeSmall);
}

void DashHud::addMessage(Startup message, float centerX, float width, float y, const ScaledDimensions& dim) {
    // Formatted per rebuild, which on the Notices page is per change, never per
    // frame. The name in the notices' large caption, and under it the way on:
    // every one of these points at the settings menu, which answers them.
    if (message == Startup::BrokenInstall) {
        // Three rows, as the first-step hint: the plugin has no way on but a
        // reinstall. In block letters while there are no fonts to draw text with.
        const char* rows[3] = { "Not installed correctly", SystemMessages::getInstance().installProblem(),
                                "Reinstall it with mxbmrp3_data" };
        const float sizes[3] = { dim.fontSize, dim.fontSizeSmall, dim.fontSizeSmall };
        for (int i = 0; i < 3; ++i) {
            const float rowTop = y + dim.lineHeightNormal * static_cast<float>(i);
            const unsigned long color = getColor(i == 0 ? ColorSlot::NEGATIVE : ColorSlot::SECONDARY);
            if (m_installPixels) {
                addPixelText(rows[i] ? rows[i] : "", centerX, rowTop, sizes[i], color);
            } else {
                addString(rows[i] ? rows[i] : "", centerX, inkCenteredY(rowTop, dim.lineHeightNormal, sizes[i]),
                          Justify::CENTER, getFont(i == 0 ? FontCategory::STRONG : FontCategory::NORMAL), color,
                          sizes[i]);
            }
        }
        return;
    }
    char name[24];
    char line[64];
    switch (message) {
        case Startup::Update:
            snprintf(name, sizeof(name), "UPDATE AVAILABLE");
            snprintf(line, sizeof(line), "Click here to get %s",
                     UpdateChecker::getInstance().getLatestVersion().c_str());
            break;
        case Startup::Updated:
            // One fixed line: the news itself is the "New" markers in the menu.
            snprintf(name, sizeof(name), "UPDATED TO %s", SystemMessages::getInstance().updatedLine());
            snprintf(line, sizeof(line), "Click here to see what's new");
            break;
        case Startup::Welcome:
        case Startup::BrokenInstall:   // above
        case Startup::None:
        default:
            snprintf(name, sizeof(name), "WELCOME");
            // The key that opens the menu, as bound; the menu button when none is.
            HotkeyManager::getInstance().formatOpenSettingsHint(line, sizeof(line));
            break;
    }
    // Cut to the card, never wrapped, should a theme's padding leave it narrower.
    const int fits = static_cast<int>(width / PluginUtils::calculateMonospaceTextWidth(1, dim.fontSizeLarge));
    PluginUtils::fitTextInPlace(name, std::strlen(name), fits);
    const int fitsSmall = static_cast<int>(width / PluginUtils::calculateMonospaceTextWidth(1, dim.fontSizeSmall));
    PluginUtils::fitTextInPlace(line, std::strlen(line), fitsSmall);
    // The caption on the large row and the line under it, as a PB notice's.
    addString(name, centerX, inkCenteredY(y, dim.lineHeightLarge, dim.fontSizeLarge), Justify::CENTER,
              getFont(FontCategory::TITLE), getColor(ColorSlot::PRIMARY), dim.fontSizeLarge);
    addString(line, centerX, inkCenteredY(y + dim.lineHeightLarge, dim.lineHeightNormal, dim.fontSizeSmall),
              Justify::CENTER, getFont(FontCategory::NORMAL), getColor(ColorSlot::SECONDARY), dim.fontSizeSmall);
}

// The shipped NORMAL font's capital, in its own cell (fontSize high, one
// charWidth wide), measured off the real string in the comparison capture
// (companion_demo "brokeninstall real"). Ratios rather than pixels, so the
// blocks follow the scale like the font does.
namespace {
    constexpr float PIXEL_CAP_TOP = 0.29f;   // cell top to cap top, of fontSize
    constexpr float PIXEL_CAP_H   = 0.555f;  // cap height, of fontSize
    constexpr float PIXEL_INK_W   = 0.80f;   // capital width, of charWidth
}

void DashHud::addPixelText(const char* text, float centerX, float rowTop, float fontSize, unsigned long color) {
    const float charW = PluginUtils::calculateMonospaceTextWidth(1, fontSize);
    const float pw = charW * PIXEL_INK_W / static_cast<float>(PixelText::GLYPH_W);
    const float ph = fontSize * PIXEL_CAP_H / static_cast<float>(PixelText::CAP_H);
    // The string's cells, centred like Justify::CENTER; each glyph centred in its cell.
    const float left = centerX - PluginUtils::calculateMonospaceTextWidth(
                                     static_cast<int>(strlen(text)), fontSize) * 0.5f
                     + (charW - pw * static_cast<float>(PixelText::GLYPH_W)) * 0.5f;
    const float top = rowTop + fontSize * PIXEL_CAP_TOP;
    PixelText::forEachRun(text, [&](int glyph, int col, int row, int len) {
        float x = left + charW * static_cast<float>(glyph) + pw * static_cast<float>(col);
        float py = top + ph * static_cast<float>(row);
        applyOffset(x, py);
        const float w = pw * static_cast<float>(len);
        SPluginQuad_t q;
        q.m_aafPos[0][0] = x;      q.m_aafPos[0][1] = py;
        q.m_aafPos[1][0] = x;      q.m_aafPos[1][1] = py + ph;
        q.m_aafPos[2][0] = x + w;  q.m_aafPos[2][1] = py + ph;
        q.m_aafPos[3][0] = x + w;  q.m_aafPos[3][1] = py;
        q.m_iSprite = SpriteIndex::SOLID_COLOR;
        q.m_ulColor = color;
        m_quads.push_back(q);
    });
}

void DashHud::drawGapBar(float rowL, float rowR, float contentY, float pageCenterX, const ScaledDimensions& dim) {
    const unsigned long primary = getColor(ColorSlot::PRIMARY);
    const unsigned long muted = getColor(ColorSlot::MUTED);
    const int digits = getFont(FontCategory::DIGITS);
    // The Gap Bar's reading: a fill growing from the card's centre,
    // left and red behind, right and green ahead, a large row tall,
    // with the gap on top as the Gap Bar prints it; under it the
    // last lap at the left and the best at the right.
    m_shownBar = readBar();
    const float bandH = dim.lineHeightLarge;
    // To the nearer content edge, so an asymmetric theme border never
    // carries the fill past the card.
    const float halfW = std::min(pageCenterX - rowL, rowR - pageCenterX);
    // The Gap Bar's look: its fill at ITS background opacity, a
    // translucent reading the gap stays legible on, as the Notices
    // page draws the Notices HUD's slab.
    const float barOpacity = HudManager::getInstance().getGapBarHud().getBackgroundOpacity();
    unsigned long slab = 0;
    if (m_shownBar.fill != 0) {
        const float w = halfW * static_cast<float>(std::abs(m_shownBar.fill)) /
                        static_cast<float>(BAR_STEPS);
        slab = getColor(m_shownBar.fill > 0 ? ColorSlot::NEGATIVE : ColorSlot::POSITIVE);
        addButtonQuad(m_shownBar.fill > 0 ? pageCenterX - w : pageCenterX, contentY, w, bandH,
                      PluginUtils::applyOpacity(slab, barOpacity), false);
    }
    char text[16];
    unsigned long ink = muted;
    if (m_shownBar.hasGap) {
        PluginUtils::formatTimeDiff(text, sizeof(text), m_shownBar.gapMs);
        ink = deltaColor(m_shownBar.gapMs);
    } else {
        snprintf(text, sizeof(text), "%s", Placeholders::GENERIC);
    }
    if (m_shownBar.noTrace) {
        // The Gap Bar's note, as that bar prints it: muted, small.
        addString(GapBarHud::NO_TRACE_NOTE, pageCenterX, inkCenteredY(contentY, bandH, dim.fontSizeSmall),
                  Justify::CENTER, getFont(FontCategory::NORMAL), muted, dim.fontSizeSmall);
    } else {
        // The fill is live and the text may be frozen, so their signs can
        // differ: lift the ink off the slab actually under it.
        if (slab != 0) ink = inkOnSlabColor(ink, slab, barOpacity);
        addString(text, pageCenterX, inkCenteredY(contentY, bandH, dim.fontSizeLarge), Justify::CENTER,
                  digits, ink, dim.fontSizeLarge);
    }
    // Labelled as on the Ideal page; the best in the positive colour,
    // as the Laps page marks it.
    const float lapsY = contentY + bandH;
    const float charW = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize);
    const unsigned long labelColor = getColor(ColorSlot::TERTIARY);
    addLabel("Last", rowL, lapsY, Justify::LEFT, labelColor, dim);
    addLabel("Best", rowR - charW * static_cast<float>(LAPS_TIME_CHARS), lapsY, Justify::RIGHT,
             labelColor, dim);
    const struct { int ms; float x; int justify; unsigned long color; LapText& cache; } laps[] = {
        { m_shownBar.lastMs, rowL + charW * static_cast<float>(IDEAL_LABEL_CHARS), Justify::LEFT, primary,
          m_lastText },
        { m_shownBar.bestMs, rowR, Justify::RIGHT, getColor(ColorSlot::POSITIVE), m_bestText },
    };
    for (const auto& lap : laps) {
        addString(lap.cache.get(lap.ms), lap.x, lapsY, lap.justify, digits,
                  lap.ms > 0 ? lap.color : muted, dim.fontSize);
    }
}

const char* DashHud::LapText::get(int lapMs) {
    if (lapMs != ms) {
        ms = lapMs;
        if (lapMs > 0) PluginUtils::formatLapTime(lapMs, text, sizeof(text));
        else snprintf(text, sizeof(text), "%s", Placeholders::GENERIC);
    }
    return text;
}

void DashHud::drawLaps(float rowL, float rowR, float contentY, const ScaledDimensions& dim) {
    const unsigned long primary = getColor(ColorSlot::PRIMARY);
    const unsigned long muted = getColor(ColorSlot::MUTED);
    const int digits = getFont(FontCategory::DIGITS);
    // In the Lap Log's Order; the best in the positive colour, the
    // others with their gap to it.
    m_shownLaps = readLaps();
    int filled = 0;
    while (filled < LAPS_ROWS && m_shownLaps.rows[filled].lapNum >= 0) ++filled;
    const float charW = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize);
    const float rowX = rowL;
    const float lapRight = rowX + charW * static_cast<float>(LAPS_LAP_CHARS);
    const float timeRight = lapRight + charW * static_cast<float>(LAPS_TIME_CHARS);
    const float gapRight = rowR;
    const unsigned long labelColor = getColor(ColorSlot::TERTIARY);
    for (int r = 0; r < LAPS_ROWS; ++r) {
        // Oldest first: the laps there are top down, the newest last.
        const int at = m_shownLaps.oldestFirst && r < filled ? filled - 1 - r : r;
        const LapRow& lap = m_shownLaps.rows[at];
        const float y = contentY + dim.lineHeightNormal * static_cast<float>(r);
        char text[16];
        if (lap.lapNum < 0) {
            addLabel(Placeholders::GENERIC, lapRight, y, Justify::RIGHT, muted, dim);
            addString(Placeholders::GENERIC, timeRight, y, Justify::RIGHT, digits, muted, dim.fontSize);
            continue;
        }
        snprintf(text, sizeof(text), "L%d", lap.lapNum + 1);
        addLabel(text, lapRight, y, Justify::RIGHT, labelColor, dim);
        // A lap without a time says why, in words (the text font).
        int timeFont = digits;
        if (lap.hasTime) {
            PluginUtils::formatLapTime(lap.lapTime, text, sizeof(text));
        } else if (lap.viaPits || !lap.valid) {
            snprintf(text, sizeof(text), "%s",
                     lap.viaPits ? Placeholders::PIT_LAP : Placeholders::INVALID_LAP);
            timeFont = getFont(FontCategory::NORMAL);
        } else {
            snprintf(text, sizeof(text), "%s", Placeholders::GENERIC);
        }
        const bool isBest = lap.hasTime && lap.valid && lap.lapNum == m_shownLaps.bestLapNum;
        const unsigned long timeColor = !lap.hasTime || !lap.valid ? muted
            : isBest ? getColor(ColorSlot::POSITIVE) : primary;
        addString(text, timeRight, y, Justify::RIGHT, timeFont, timeColor, dim.fontSize);
        if (lap.hasTime && lap.valid && !isBest && m_shownLaps.bestMs > 0) {
            PluginUtils::formatTimeDiff(text, sizeof(text), lap.lapTime - m_shownLaps.bestMs);
            addString(text, gapRight, y, Justify::RIGHT, digits, deltaColor(lap.lapTime - m_shownLaps.bestMs),
                      dim.fontSize);
        }
    }
}
