// ============================================================================
// hud/dash_hud.cpp
// Dash - see dash_hud.h.
// ============================================================================
#include "dash_hud.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "delta_trace_hud.h"
#include "center_stack.h"
#include "digit_roll.h"
#include "gap_bar_hud.h"
#include "notice_tracker.h"
#include "notices_hud.h"
#include "rider_flag_icons.h"
#include "fuel_widget.h"
#include "telemetry_hud.h"
#include "timing_hud.h"
#include "../core/asset_manager.h"
#include "../core/motion.h"
#include "../core/ui_config.h"
#include "../core/color_config.h"
#include "../core/hotkey_manager.h"
#include "../core/hud_manager.h"
#include "../core/plugin_constants.h"
#include "../core/plugin_data.h"
#include "../core/plugin_utils.h"

using namespace PluginConstants;

namespace {
// The icon strip under the card: one icon's pitch across it in icon widths,
// and the page showing at this many times the others' size.
constexpr float ICON_PITCH = 1.6f;
constexpr float ACTIVE_ICON_SCALE = 1.35f;
// The other pages' icons, muted and at this opacity, so the one showing stands out.
constexpr float INACTIVE_ICON_OPACITY = 0.5f;
// Page names: how long a stepped-to page's name holds before its content.
constexpr float NAME_HOLD_MS = 600.0f;

// Indexed by DashHud::Page.
// The name is the one the page's row has in Settings > Dash > Pages; `mark` is
// the strip's text stand-in for the icon with Appearance > UI icons off;
// `title` the panel's caption while the page is up.
struct PageInfo {
    const char* key; const char* enabledKey; const char* icon; const char* name; const char* mark; const char* title;
};
// The longest title, which the panel is planned for, so stepping never
// changes its width.
constexpr const char* WIDEST_TITLE = "Dash: Delta Trace";
constexpr PageInfo kPages[static_cast<int>(DashHud::Page::COUNT)] = {
    { "standings", "page_standings", "hud-standings", "Standings", "ST", "Dash: Standings" },
    { "timing",  "page_timing",  "hud-timing",    "Timing", "TI", "Dash: Timing" },
    { "laps",    "page_laps",    "hud-laplog",    "Laps", "LA", "Dash: Laps" },
    { "time",    "page_time",    "hud-clock",     "Time", "CL", "Dash: Time" },
    { "fuel",    "page_fuel",    "hud-fuel",      "Fuel", "FU", "Dash: Fuel" },
    { "gapbar",  "page_gapbar",  "hud-gapbar",    "Gap Bar", "GB", "Dash: Gap Bar" },
    { "flatmap", "page_flatmap", "hud-flatmap",   "Flat Map", "FM", "Dash: Flat Map" },
    { "ideal",   "page_ideal",   "hud-ideallap",  "Ideal", "ID", "Dash: Ideal" },
    { "rpm",     "page_rpm",     "hud-gauge",     "RPM", "RP", "Dash: RPM" },
    { "deltatrace", "page_deltatrace", "hud-deltatrace", "Delta Trace", "DT", "Dash: Delta Trace" },
    { "gapchart", "page_gapchart", "hud-sessioncharts", "Gap Chart", "GC", "Dash: Gap Chart" },
    { "session", "page_session", "hud-session", "Session", "SE", "Dash: Session" },
    { "telemetry", "page_telemetry", "hud-telemetry", "Telemetry", "TE", "Dash: Telemetry" },
    { "eventlog", "page_eventlog", "hud-eventlog", "Event Log", "EL", "Dash: Event Log" },
    { "nowplaying", "page_nowplaying", "hud-nowplaying", "Now Playing", "NP", "Dash: Now Playing" },
    // Not in the cycle: only up while a notice holds the card.
    { "notices", nullptr,        "hud-notices",   "Notices", "NO", "Dash: Notices" },
};
// Every page icon is an identity icon (assets/icons/README.md): a flat hud-*
// one, never the outlined status/marker set.
constexpr bool identityIcons() {
    for (const PageInfo& p : kPages) {
        const char* i = p.icon;
        if (!(i[0] == 'h' && i[1] == 'u' && i[2] == 'd' && i[3] == '-')) return false;
    }
    return true;
}
static_assert(identityIcons(), "Dash page icons must be flat hud-* identity icons");
static_assert((DashHud::DEFAULT_PAGES & DashHud::pageBit(DashHud::DEFAULT_PAGE)) != 0,
              "the default page must be in the default cycle");


}  // namespace

const char* DashHud::pageKey(Page page) {
    const int i = static_cast<int>(page);
    return kPages[i < CYCLE_PAGES ? i : 0].key;
}

const char* DashHud::pageEnabledKey(Page page) {
    const int i = static_cast<int>(page);
    return kPages[i < CYCLE_PAGES ? i : 0].enabledKey;
}

DashHud::Page DashHud::pageFromKey(const char* key) {
    for (int i = 0; key && i < CYCLE_PAGES; ++i)
        if (std::strcmp(kPages[i].key, key) == 0) return static_cast<Page>(i);
    return DEFAULT_PAGE;
}

namespace {
constexpr const char* kStripPlaceKeys[] = { "top", "right", "bottom", "left", "off" };
static_assert(sizeof(kStripPlaceKeys) / sizeof(kStripPlaceKeys[0]) == static_cast<size_t>(DashHud::StripPlace::COUNT),
              "one key per strip place");
}  // namespace

const char* DashHud::stripPlaceKey(StripPlace place) {
    const int i = static_cast<int>(place);
    return kStripPlaceKeys[i < static_cast<int>(StripPlace::COUNT) ? i : static_cast<int>(DEFAULT_STRIP_PLACE)];
}

DashHud::StripPlace DashHud::stripPlaceFromKey(const char* key) {
    for (int i = 0; key && i < static_cast<int>(StripPlace::COUNT); ++i)
        if (std::strcmp(kStripPlaceKeys[i], key) == 0) return static_cast<StripPlace>(i);
    return DEFAULT_STRIP_PLACE;
}

DashHud::DashHud() {
    setDraggable(true);
    // A body card PER SECTION: the page and the strip are two things, as the
    // Timing HUD's time and comparisons are (see BaseHud::m_bContentSections).
    m_bContentCard = true;
    m_bContentSections = true;
    // The panel themed (frame, title band and two section cards, nine quads
    // each), one icon per page, and the most two crossfading pages draw: the
    // Telemetry page's grid and traces (eight inputs, a segment between each
    // two samples) beside the Delta Trace page's chart (its grid, split lines,
    // peaks, marker and up to three quads per profile point), the two
    // neighbours in the cycle.
    m_quads.reserve(4 * 9 + static_cast<int>(Page::COUNT) + 3 + TELEMETRY_MAX_SEGMENTS +
                    6 + Unified::MAX_SPLITS + LapDeltaProfile::POINTS * 3);
    // The two pages of a crossfade, the Flat map (a label per marker, with the
    // Gap Bar's labels on) beside Ideal (label, time and gap, three rows) the
    // most; their two names; and the strip's marks with UI icons off.
    m_strings.reserve(GapBarHud::MAX_MARKERS + 9 + 2 + static_cast<int>(Page::COUNT));
    resetToDefaults();
    rebuildRenderData();
}

void DashHud::updateRiderPositions(int numVehicles, const Unified::TrackPositionData* positions) {
    if (numVehicles <= 0 || !positions || !isVisibleAnySurface() ||
        (m_page != Page::FLAT_MAP && !(m_fromPage == Page::FLAT_MAP && m_fadeMs > 0.0f))) {
        m_riderPositions.clear();
        return;
    }
    // Kept through the page's fade out too. update() redraws the page at its own
    // rate, so nothing is marked here.
    m_riderPositions.assign(positions, positions + numVehicles);
}

bool DashHud::handlesDataType(DataChangeType dataType) const {
    // update() polls the few values the page showing reads, but for the Gap
    // chart's lap data, which it takes as the Session Charts HUD does: a lap
    // logged by any rider moves every gap to the leader.
    return m_page == Page::GAP_CHART && dataType == DataChangeType::LapLog;
}

uint64_t DashHud::hintKeys() {
    const HotkeyManager& keys = HotkeyManager::getInstance();
    uint64_t packed = 0;
    for (const HotkeyAction action : { HotkeyAction::DASH_PREV, HotkeyAction::DASH_NEXT }) {
        const HotkeyBinding& b = keys.getBinding(action);
        packed = (packed << 32) | b.keyboard.keyCode | (static_cast<uint64_t>(b.keyboard.modifiers) << 8) |
                 (static_cast<uint64_t>(b.controller) << 16);
    }
    return packed;
}

void DashHud::goTo(Page next) {
    if (next == m_page) return;
    // Start from what is on screen now (see the header's MOTION note).
    const float e = fadeProgress();
    const float stripNow = m_stripFrom * (1.0f - e);   // offset from the current page
    const float inNow = m_inFrom + (1.0f - m_inFrom) * e;
    const float outNow = m_fromAlpha * (1.0f - e);
    // Back to the page fading out: it comes in again from where it had got to.
    m_inFrom = next == m_fromPage ? outNow : 0.0f;
    m_fromPage = m_page;
    m_fromAlpha = inNow;
    // The name showing leaves with its page; the new page opens on its content
    // unless the step that brought it puts its name up (step()).
    m_fromName = m_nameUp ? nameShare() : 0.0f;
    m_nameUp = false;
    // Round the ring the short way, so a wrap (last to first) slides on by one
    // like any other step. The strip follows the rider's page, not a notice
    // taking the card over: with a hold on, before and after, it stays put
    // (callers flip the hold around this call to keep it so).
    const Page stripOld = m_noticeHold ? m_returnPage : m_page;
    const Page stripNew = m_noticeHold ? m_returnPage : next;
    m_stripFrom = ringOffset(static_cast<float>(slotOf(stripOld)) + stripNow - static_cast<float>(slotOf(stripNew)));
    m_page = next;
    m_fadeMs = Motion::profileFor(UiConfig::getInstance().getMotion()).inMs;
    m_fadeStartUs = DigitRoll::nowUs();
    if (m_fadeMs <= 0.0f) stopFade();
    setDataDirty();
}

int DashHud::enabledCount() const {
    int n = 0;
    for (int i = 0; i < CYCLE_PAGES; ++i)
        if (pageEnabled(static_cast<Page>(i))) ++n;
    return n;
}

float DashHud::ringOffset(float d) const {
    // Two pages or fewer: a ring and a line are the same strip.
    const int n = enabledCount();
    if (n < 3) return d;
    const float count = static_cast<float>(n);
    const float half = count * 0.5f;
    while (d >= half) d -= count;
    while (d < -half) d += count;
    return d;
}

int DashHud::slotOf(Page page) const {
    int slot = 0;
    for (int i = 0; i < static_cast<int>(page); ++i)
        if (pageEnabled(static_cast<Page>(i))) ++slot;
    return slot;
}

DashHud::Page DashHud::firstEnabledFrom(Page from) const {
    const int count = CYCLE_PAGES;
    for (int i = 0; i < count; ++i) {
        const Page p = static_cast<Page>((static_cast<int>(from) + i) % count);
        if (pageEnabled(p)) return p;
    }
    return from;
}

void DashHud::stopFade() {
    m_fadeMs = 0.0f;
    m_fromAlpha = 0.0f;
    m_inFrom = 0.0f;
    m_stripFrom = 0.0f;
}

float DashHud::nameShare() const {
    if (!m_nameUp) return 0.0f;
    const float ms = static_cast<float>(DigitRoll::nowUs() - m_nameStartUs) * 0.001f - NAME_HOLD_MS;
    if (ms < 0.0f) return 1.0f;
    const float fadeMs = Motion::profileFor(UiConfig::getInstance().getMotion()).inMs;
    if (fadeMs <= 0.0f || ms >= fadeMs) return 0.0f;
    return 1.0f - Motion::smooth(ms / fadeMs);
}

float DashHud::fadeProgress() const {
    if (m_fadeMs <= 0.0f) return 1.0f;
    const float ms = static_cast<float>(DigitRoll::nowUs() - m_fadeStartUs) * 0.001f;
    return ms >= m_fadeMs ? 1.0f : Motion::smooth(ms / m_fadeMs);
}

void DashHud::update() {
    // The history buffers start fresh when the Dash starts drawing them, as on
    // the Telemetry HUD's show edge, unless that HUD is still drawing them.
    const bool historyUser = wantsTelemetryHistory();
    if (historyUser && !m_wasHistoryUser &&
        !HudManager::getInstance().getTelemetryHud().isVisibleAnySurface()) {
        PluginData::getInstance().clearHistoryBuffers();
    }
    m_wasHistoryUser = historyUser;

    // The page set changed (Settings > Dash > Pages): off a page that left, with
    // no fade, since the strip's slots moved under it.
    if (m_enabledPages != m_shownPages) {
        if ((m_enabledPages & ALL_PAGES) == 0) m_enabledPages = ALL_PAGES;
        stopFade();
        clearName();
        // A notice holding the card stays up; the page it returns to moves.
        if (m_noticeHold) m_returnPage = firstEnabledFrom(m_returnPage);
        else m_page = firstEnabledFrom(m_page);
        m_fromPage = m_page;
        m_shownPages = m_enabledPages;
        setDataDirty();
    }
    // The freeze watches the crossings whether or not the Dash or the page is up
    // (see OfficialGapFreeze): a split crossed while hidden must not read as new
    // the moment it is shown.
    if (m_gapFreeze.update(timingReference(), timingFreezeMs()) && m_page == Page::TIMING) {
        setDataDirty();
    }
    // A reference changed on the page's parent renames the title row's note.
    if (m_bShowTitle && pageReferenceNote(m_page) != m_shownNote) setDataDirty();
    {
        const int freeze = timingFreezeMs();
        const SplitCrossingDetector::Result crossing = m_timingCrossings.poll();
        if (crossing.adopted) {
            m_timingHold.stop();
        } else if (crossing.line || crossing.splitIndex >= 0) {
            // The line first, as the Timing HUD: a split read in the same poll
            // belongs to the new lap and takes the hold over.
            const bool pitLap = crossing.line && crossing.lapViaPits;
            m_heldTimeMs = crossing.splitIndex >= 0 ? crossing.splitTime : crossing.lapTime;
            m_heldInvalid = crossing.splitIndex < 0 && !crossing.lapValid && !pitLap;
            if (freeze > 0 && !pitLap) m_timingHold.start();
            else m_timingHold.stop();
        }
        if (m_timingHold.expire(freeze)) setDataDirty();
    }
    followSegmentHold();
    const PbGapTracker::Ref barRef = barReference();
    if (m_barFreeze.update(barRef, barFreezeMs()) && m_page == Page::GAP_BAR) {
        setDataDirty();
    }
    m_barRange.follow(barRef);

    followBorrowed();
    if (!isVisibleAnySurface() && !m_borrowed) {
        // Hidden: nothing held, so showing it again opens on the rider's page.
        if (m_noticeHold) {
            m_noticeHold = false;
            m_page = firstEnabledFrom(m_returnPage);
            stopFade();
        }
        clearName();
        clearDataDirty();
        clearLayoutDirty();
        return;
    }

    followNotices();
    tickMessage();
    followMessageClick();
    followMediaInput();

    // Integer compares only: a rebuild runs when a shown value changes, not at the
    // telemetry rate. The RPM and Fuel pages also follow their widgets' look
    // (PageStyle), and the hint the keys it names.
    bool changed = readPageStyle() != m_shownStyle;
    if (!m_stepTried) changed = changed || hintKeys() != m_shownHintKeys;
    // Only the page showing is watched, and not under the first-step hint, which
    // covers it: nothing the page shows can change what is drawn.
    if (!m_stepTried && m_page != Page::NOTICES) {
        // The hint's keys are compared above.
    } else if (m_page == Page::NOTICES) {
        changed = changed || NoticeTracker::getInstance().revision() != m_shownNoticeRevision
                          || cardMessage() != m_shownMessage;
    } else if (m_page == Page::STANDINGS) {
        Neighbour rows[STANDINGS_ROWS];
        findNeighbours(rows);
        for (int i = 0; i < STANDINGS_ROWS; ++i) changed = changed || rows[i] != m_shownRows[i];
        changed = changed || PluginData::getInstance().getTrackedRidersRevision() != m_plateTintRevision;
    } else if (m_page == Page::TIMING) {
        changed = changed || readLapClock() != m_shownClock;
    } else if (m_page == Page::GAP_BAR) {
        changed = changed || readBar() != m_shownBar;
    } else if (m_page == Page::FLAT_MAP) {
        // Every rider moves all the time: at the Gap Bar's rate, as it redraws.
        const auto now = std::chrono::steady_clock::now();
        if (now - m_mapDrawnAt >= std::chrono::milliseconds(FLAT_MAP_INTERVAL_MS)) {
            m_mapDrawnAt = now;
            changed = true;
        }
    } else if (m_page == Page::TIME) {
        changed = changed || readSessionClock() != m_shownSession;
    } else if (m_page == Page::LAPS) {
        changed = changed || readLaps() != m_shownLaps;
    } else if (m_page == Page::IDEAL) {
        changed = changed || readIdeal() != m_shownIdeal;
    } else if (m_page == Page::RPM) {
        // A light coming on or the limiter flash flipping, as on the RPM
        // widget, or the figure crossing a step.
        changed = changed || RpmWidget::readLights(rpmSegments()) != m_shownLights
                          || readRpm() != m_shownRpm;
    } else if (m_page == Page::FUEL) {
        changed = changed || readFuel() != m_shownFuel;
    } else if (m_page == Page::TELEMETRY) {
        // A sample in (~100 Hz on track), as on the Telemetry HUD.
        changed = changed || PluginData::getInstance().getHistoryBuffers().revision != m_shownTelemetryRevision;
    } else if (m_page == Page::DELTA_TRACE) {
        // The rider at the next profile point, the gap's validity flipping, a
        // lap committed or the reference changed, as on the Delta Trace HUD.
        changed = m_deltaPlot.due(HudManager::getInstance().getDeltaTraceHud().getReference(), false) || changed;
        // Between rebuilds the rider's marker moves in place, as on the Delta
        // Trace HUD (not mid-fade or under a page name, which rebuild anyway).
        if (!changed && m_fadeMs <= 0.0f && !m_nameUp) {
            m_deltaPlot.moveMarker(*this, HudManager::getInstance().getDeltaTraceHud().getReference());
        }
    } else if (m_page == Page::GAP_CHART) {
        changed = changed || readGapChart() != m_shownGapChart;
    } else if (m_page == Page::EVENT_LOG) {
        changed = changed || readEvents() != m_shownEvents;
    } else if (m_page == Page::NOW_PLAYING) {
        changed = changed || m_nowPlaying.changed();
    } else if (m_page == Page::SESSION) {
        changed = changed || sessionInfoChanged();
    }
    // A fade redraws every frame until it ends, then stops.
    if (m_fadeMs > 0.0f) {
        changed = true;
        if (fadeProgress() >= 1.0f) stopFade();
    }
    // A page name redraws every frame while it fades; held, or with Motion off,
    // only once more when it is done.
    if (m_nameUp) {
        const float share = nameShare();
        if (share <= 0.0f) {
            m_nameUp = false;
            changed = true;
        } else if (share < 1.0f) {
            changed = true;
        }
    }
    if (changed) setDataDirty();

    processDirtyFlags();
}

void DashHud::rebuildLayout() {
    rebuildRenderData();
}

void DashHud::addRect(float x, float y, float w, float h, unsigned long color) {
    applyOffset(x, y);
    SPluginQuad_t quad;
    setQuadPositions(quad, x, y, w, h);
    quad.m_iSprite = SpriteIndex::SOLID_COLOR;
    quad.m_ulColor = color;
    m_quads.push_back(quad);
}

void DashHud::rebuildRenderData() {
    clearStrings();
    m_quads.clear();

    m_shownStyle = readPageStyle();
    m_shownNoticeRevision = NoticeTracker::getInstance().revision();
    m_shownMessage = Startup::None;   // set again where the Notices page draws one

    const auto dim = getScaledDimensions();
    // ONE THEMED PANEL, TWO SECTIONS: the page's card on top and the strip's under
    // it, each its own section body, so a theme frames the Dash as it frames the
    // Timing HUD. The panel as wide as Standings, FMX and Ideal Lap
    // (CONTENT_WIDTH_CHARS), so Standings' short names fit whole; the
    // pages that can use the width (lights, bars, traces, rows) run edge to edge.
    // The page's rows as tall as the Gear and Speed widgets' content, the strip
    // one row.
    // THE STRIP'S PLACE (Settings > Dash > Appearance > Page strip): a row
    // section under or over the page's, a column beside it (a band of two
    // columns, each its own card, the page's as wide as always), or none.
    PanelWant localWant;
    // Borrowed (switched off, up for a message), or a broken install: no strip.
    const bool noStrip = m_borrowed || SystemMessages::getInstance().installProblem();
    const StripPlace stripPlace = noStrip ? StripPlace::OFF : m_stripPlace;
    const float pageW = PluginUtils::calculateMonospaceTextWidth(CONTENT_WIDTH_CHARS, dim.fontSize);
    const float pageH = dim.lineHeightLarge + dim.lineHeightNormal;
    const float iconSize = dim.lineHeightNormal;
    // Down a side, three icons a third of the page's height apart, the page
    // showing's filling most of its third.
    const float sidePitch = pageH / 3.0f;
    const float sideIcon = sidePitch * SIDE_ACTIVE_SHARE / ACTIVE_ICON_SCALE;
    const bool stripSide = stripPlace == StripPlace::LEFT || stripPlace == StripPlace::RIGHT;
    // Columns are vectors (PanelWant::bands), so the side layouts refill one
    // kept want in place, which keeps their capacity: no heap work per rebuild.
    PanelWant& want = stripSide ? m_sideWant : localWant;
    // The page's section (or, beside the strip, column) and the strip's.
    size_t pageSec = 0, stripSec = 1, pageCol = 0, stripCol = 0;
    if (stripSide) {
        if (stripPlace == StripPlace::LEFT) pageCol = 1;
        else stripCol = 1;
        want.bands.resize(1);
        std::vector<PanelWant::ColumnWant>& columns = want.bands[0].columns;
        columns.resize(2);
        columns[pageCol].contentW = pageW;
        columns[pageCol].sectionH.assign(1, pageH + m_sideStripExtra);
        // A slot as wide as it is tall.
        columns[stripCol].contentW = sidePitch / UI_ASPECT_RATIO;
        columns[stripCol].sectionH.assign(1, pageH + m_sideStripExtra);
        pageSec = stripSec = 0;
    } else {
        want.contentW = pageW;
        if (stripPlace == StripPlace::TOP) {
            want.sectionH = { dim.lineHeightNormal, pageH };
            pageSec = 1;
            stripSec = 0;
        } else if (stripPlace == StripPlace::OFF) {
            want.sectionH = { pageH };
        } else {
            want.sectionH = { pageH, dim.lineHeightNormal };
        }
    }
    // A HUD's caption, at the large tier, as the Timing HUD's, with room for
    // the longest reference note so stepping pages never changes the width.
    want.captionW = planTitleWidth(dim, WIDEST_TITLE, TitleTier::Large)
                  + planTitleNoteWidth(dim, PbGapTracker::refName(PbGapTracker::Ref::ALLTIME_PB));
    want.tier = TitleTier::Large;
    PanelPlan& p = planPanel(dim, want);
    if (stripSide) {
        // THE TWO CARDS ONE HEIGHT: the panel's rounding remainder goes to one
        // column's card alone (PanelBox: the tallest, ties the first), so both
        // are asked for that much more, and the panel comes out whole with
        // none left over. The ask is kept a hair under the whole cell: a float
        // hair over it rounds up a cell more, which a card alone takes. A diff
        // over half a cell is such an overshoot, so the extra steps back.
        // Settles within a few plans when the size changes, then the memo hits.
        const float tol = p.cellH * 1e-3f;
        for (int pass = 0; pass < 4; ++pass) {
            const PanelBox::ColumnGeom& pc = p.col(0, pageCol);
            const PanelBox::ColumnGeom& sc = p.col(0, stripCol);
            if (pc.sections.empty() || sc.sections.empty()) break;
            const float diff = std::fabs(p.H(pc.sections.back().bot - sc.sections.back().bot));
            if (diff < tol || pass == 3) break;
            float extra = m_sideStripExtra + (diff > p.cellH * 0.5f ? diff - p.cellH : diff)
                        - p.cellH * 1e-4f;
            extra = std::fmod(extra, p.cellH);
            if (extra < 0.0f) extra += p.cellH;
            m_sideStripExtra = extra;
            std::vector<PanelWant::ColumnWant>& columns = want.bands[0].columns;
            columns[pageCol].sectionH.assign(1, pageH + m_sideStripExtra);
            columns[stripCol].sectionH.assign(1, pageH + m_sideStripExtra);
            planPanel(dim, want);
        }
    }
    const float top = 0.0f;
    addPlanBackground(p, 0.0f, top);
    // The page up named after the HUD's own name ("Dash: Timing").
    addPlanTitle(p, kPages[static_cast<int>(m_page) % static_cast<int>(Page::COUNT)].title,
                 getColor(ColorSlot::PRIMARY));
    // The page's rows run between its content edges; centred values sit on its
    // card (PanelPlan::sectionBoxCenterX).
    // The page's card (boxX..) and rows (rowL..), and the strip's card.
    float boxX = p.sectionBoxX(), boxW = p.sectionBoxW();
    float boxY = p.sectionBoxY(pageSec), boxH = p.sectionBoxH(pageSec);
    float rowL = p.contentX(), rowW = p.contentW();
    float contentY = p.contentY(pageSec);
    float stripX = boxX, stripW = boxW;
    float stripY = p.sectionBoxY(stripSec), stripH = p.sectionBoxH(stripSec);
    if (stripSide) {
        const PanelBox::ColumnGeom& pc = p.col(0, pageCol);
        const PanelBox::ColumnGeom& sc = p.col(0, stripCol);
        boxX = p.X(pc.cardLeft);
        boxW = p.W(pc.cardW);
        rowL = p.colContentX(pc);
        rowW = p.colContentW(pc);
        contentY = p.colContentY(pc);
        if (!pc.sections.empty()) {
            boxY = p.Y(pc.sections[0].top);
            boxH = p.H(pc.sections[0].bot - pc.sections[0].top);
        }
        stripX = p.X(sc.cardLeft);
        stripW = p.W(sc.cardW);
        if (!sc.sections.empty()) {
            stripY = p.Y(sc.sections[0].top);
            stripH = p.H(sc.sections[0].bot - sc.sections[0].top);
        }
    }
    const float rowR = rowL + rowW;

    const unsigned long primary = getColor(ColorSlot::PRIMARY);
    const unsigned long muted = getColor(ColorSlot::MUTED);
    const int digits = getFont(FontCategory::DIGITS);

    // === THE PAGE SHOWING ===
    const float pageCenterX = boxX + boxW / 2.0f;
    // The page fading out first, then the one coming in; each page's strings take
    // its alpha after they are laid out (the panel's quads are shared and stay).
    // One large line sits in the middle of the card.
    const float largeY = inkCenteredY(boxY, boxH, dim.fontSizeLarge);
    const float fade = fadeProgress();
    for (int pass = 0; pass < 2; ++pass) {
        const Page page = pass == 0 ? m_fromPage : m_page;
        const float alpha = pass == 0 ? m_fromAlpha * (1.0f - fade)
                                      : m_inFrom + (1.0f - m_inFrom) * fade;
        if (pass == 0 && (page == m_page || alpha <= 0.0f)) continue;
        // The page's name's share of it (PAGE NAMES in the header); the content
        // takes the rest. The hint stands in the name's place, held.
        const bool hint = pass == 1 && !m_stepTried && page != Page::NOTICES;
        const float nameAlpha = hint ? 1.0f : pass == 0 ? m_fromName : nameShare();
        const size_t firstString = m_strings.size();
        const size_t firstQuad = m_quads.size();
        switch (page) {
            case Page::NOTICES:
            case Page::COUNT: {
                m_shownMessage = cardMessage();
                if (m_shownMessage != Startup::None) {
                    addMessage(m_shownMessage, pageCenterX, rowW, contentY, dim);
                    break;
                }
                const NoticeTracker::Notice& notice = NoticeTracker::getInstance().current();
                if (notice.kind == NoticeTracker::Kind::None) {
                    addString(Placeholders::GENERIC, pageCenterX, largeY, Justify::CENTER,
                              getFont(FontCategory::TITLE), getColor(ColorSlot::MUTED), dim.fontSizeLarge);
                    break;
                }
                // The Notices HUD's slab on this card's content box: its colours
                // (the message) at ITS opacity, the caption on it, so the page
                // follows the Notices settings like every page follows its parent's.
                const NoticesHud& notices = HudManager::getInstance().getNoticesHud();
                const float slabOpacity = notices.getBackgroundOpacity();
                unsigned long fill = 0, ink = 0;
                notices.slabColors(notice.kind, slabOpacity, fill, ink);
                // Its glow reaches as far as the Notices HUD's (whose slab is one
                // large row), not a share of this taller box.
                if (NoticesHud::glows(notice.kind))
                    addGlow(boxX, boxY, boxW, boxH, dim.lineHeightLarge * NoticesHud::GLOW_SPREAD, fill,
                            NoticesHud::glowOpacity(notice.kind));
                addButtonQuad(boxX, boxY, boxW, boxH,
                              PluginUtils::applyOpacity(fill, slabOpacity), false);
                // With a second line (a PB's lap and gain), the caption takes the
                // large row and the line the row under it, as the Timing page.
                if (notice.detail[0] == '\0') {
                    addString(NoticeTracker::caption(notice, rowW, dim.fontSizeLarge, getFont(FontCategory::TITLE)), pageCenterX,
                              largeY, Justify::CENTER, getFont(FontCategory::TITLE), ink, dim.fontSizeLarge);
                    break;
                }
                addString(NoticeTracker::caption(notice, rowW, dim.fontSizeLarge, getFont(FontCategory::TITLE)), pageCenterX,
                          inkCenteredY(contentY, dim.lineHeightLarge, dim.fontSizeLarge),
                          Justify::CENTER, getFont(FontCategory::TITLE), ink, dim.fontSizeLarge);
                // A lap time in the digits font; a track's artist is words.
                addString(notice.detail, pageCenterX, contentY + dim.lineHeightLarge, Justify::CENTER,
                          getFont(notice.kind == NoticeTracker::Kind::TrackChanged ? FontCategory::NORMAL
                                                                                    : FontCategory::DIGITS),
                          ink, dim.fontSize);
                break;
            }
            case Page::STANDINGS:
                drawStandings(rowL, rowR, contentY, dim);
                break;
            case Page::FLAT_MAP:
                drawFlatMap(rowL, rowW, boxY, boxH, dim);
                break;
            case Page::TIMING: {
                // As the Timing HUD: the running lap (the official split or lap
                // time through the freeze, or the finish time) large, with the
                // gap to the Timing HUD's reference lap (General's by default)
                // under it in its ahead/behind colour, both to the millisecond
                // (Compact times dropping the "0:"). Always the gap, the Timing
                // HUD's Gap toggle or not, and only that one comparison: the
                // title row names its reference, as every other gap.
                // A struck lap holds INVALID in the Timing HUD's red and words font.
                m_shownClock = readLapClock();
                char text[16];
                if (m_shownClock.invalid) snprintf(text, sizeof(text), "%s", Placeholders::INVALID_LAP);
                else if (m_shownClock.timeMs < 0) snprintf(text, sizeof(text), "%s", Placeholders::GENERIC);
                else PluginUtils::formatLapTime(m_shownClock.timeMs, text, sizeof(text));
                const unsigned long timeColor = m_shownClock.invalid ? getColor(ColorSlot::NEGATIVE)
                                              : m_shownClock.timeMs < 0 ? muted : primary;
                addString(text, pageCenterX, inkCenteredY(contentY, dim.lineHeightLarge, dim.fontSizeLarge),
                          Justify::CENTER, m_shownClock.invalid ? getFont(FontCategory::NORMAL) : digits, timeColor,
                          dim.fontSizeLarge);
                const float lapsY = contentY + dim.lineHeightLarge;
                unsigned long gapColor = muted;
                if (m_shownClock.hasGap) {
                    PluginUtils::formatTimeDiff(text, sizeof(text), m_shownClock.gapMs);
                    gapColor = deltaColor(m_shownClock.gapMs);
                } else if (m_shownClock.targetMs > 0) {
                    // Segments: the target, as the Timing HUD's Best row shows it.
                    PluginUtils::formatLapTime(m_shownClock.targetMs, text, sizeof(text));
                    gapColor = getColor(ColorSlot::SECONDARY);
                } else {
                    snprintf(text, sizeof(text), "%s", Placeholders::GENERIC);
                }
                addString(text, pageCenterX, lapsY, Justify::CENTER, digits, gapColor, dim.fontSize);
                break;
            }
            case Page::GAP_BAR:
                drawGapBar(rowL, rowR, contentY, pageCenterX, dim);
                break;
            case Page::TIME: {
                // The shared session clock text (MM:SS, or "2 TO GO" / "FINAL LAP"
                // / "CHECKERED" in a timed race's extra laps), as the Timing HUD's
                // Time readout and the Standings title, so all three agree.
                m_shownSession = readSessionClock();
                char text[16];
                PluginUtils::formatSessionClock(m_shownSession.lapsToGo,
                                                PluginData::getInstance().getSessionTime(), text, sizeof(text));
                addString(text, pageCenterX, largeY, Justify::CENTER, digits, primary, dim.fontSizeLarge);
                break;
            }
            case Page::LAPS:
                drawLaps(rowL, rowR, contentY, dim);
                break;
            case Page::IDEAL: {
                // Last and Best with their gap to the ideal, then the ideal itself
                // in the positive colour, as on the Ideal Lap HUD.
                m_shownIdeal = readIdeal();
                const float charW = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize);
                const float rowX = rowL;
                const float timeRight = rowX + charW * static_cast<float>(IDEAL_LABEL_CHARS + LAPS_TIME_CHARS);
                const float gapRight = rowR;
                const unsigned long labelColor = getColor(ColorSlot::TERTIARY);
                const int idealMs = m_shownIdeal.idealMs;
                const struct { const char* label; int ms; bool isIdeal; } rows[] = {
                    { "Last", m_shownIdeal.lastMs, false },
                    { "Best", m_shownIdeal.bestMs, false },
                    { "Ideal", idealMs, true },
                };
                for (int r = 0; r < 3; ++r) {
                    const float y = contentY + dim.lineHeightNormal * static_cast<float>(r);
                    addLabel(rows[r].label, rowX, y, Justify::LEFT, labelColor, dim);
                    char text[16];
                    if (rows[r].ms > 0) PluginUtils::formatLapTime(rows[r].ms, text, sizeof(text));
                    else snprintf(text, sizeof(text), "%s", Placeholders::GENERIC);
                    const unsigned long timeColor = rows[r].ms <= 0 ? muted
                        : rows[r].isIdeal ? getColor(ColorSlot::POSITIVE) : primary;
                    addString(text, timeRight, y, Justify::RIGHT, digits, timeColor, dim.fontSize);
                    if (rows[r].isIdeal) continue;
                    if (rows[r].ms <= 0 || idealMs <= 0) {
                        addString(Placeholders::GENERIC, gapRight, y, Justify::RIGHT, digits, muted, dim.fontSize);
                        continue;
                    }
                    // At or under the ideal: by how much it beat the previous one.
                    int diff = rows[r].ms - idealMs;
                    if (diff <= 0 && m_shownIdeal.prevIdealMs > 0) diff = rows[r].ms - m_shownIdeal.prevIdealMs;
                    PluginUtils::formatTimeDiff(text, sizeof(text), diff);
                    addString(text, gapRight, y, Justify::RIGHT, digits, deltaColor(diff), dim.fontSize);
                }
                break;
            }
            case Page::FUEL: {
                // The Fuel widget's rows, label left and figure right, in its
                // unit and colours: the laps left warn and go critical as there.
                // Beside them on the left, the tank bar over the page's height,
                // lit in the laps left's colour, with the RPM widget's gaps and
                // dimmed unlit as its lights; the top one fades as they do.
                m_shownFuel = readFuel();
                const float charW = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize);
                {
                    const float barH = dim.lineHeightLarge + dim.lineHeightNormal;
                    const float pitch = barH / static_cast<float>(FUEL_BAR_SEGMENTS);
                    const float gap = HudManager::getInstance().getRpmWidget().m_bSegmentGaps
                        ? pitch * RpmWidget::SEGMENT_GAP_SHARE : 0.0f;
                    const unsigned long* tank = m_shownStyle.fuelInk;
                    const unsigned long lit = m_shownFuel.lapsTint == 2 ? tank[5]
                                            : m_shownFuel.lapsTint == 1 ? tank[4] : tank[3];
                    const float barW = charW * 1.5f;
                    auto barY = [&](int i) { return contentY + barH - pitch * static_cast<float>(i + 1) + gap * 0.5f; };
                    // Bar i's opacity: whole, the top lit one part way, or unlit.
                    const int topBar = static_cast<int>(m_shownFuel.level) - 1;
                    auto barOpacity = [&](int i) {
                        if (i > topBar) return RpmWidget::UNLIT_OPACITY;
                        if (i < topBar || m_shownFuel.step == RpmWidget::LEAD_STEPS - 1) return 1.0f;
                        return RpmWidget::onOpacity(RpmWidget::stepShare(m_shownFuel.step));
                    };
                    // Every bar glows as the RPM lights do, the unlit ones as dim
                    // as they are drawn; all halos first, so none lies over a bar.
                    if (glowActive()) {
                        // Full only at the critical (red) laps left, the calm share otherwise.
                        const float strength = RpmWidget::GLOW_OPACITY *
                            (m_shownFuel.lapsTint == 2 ? 1.0f : UiConfig::GLOW_CALM);
                        for (int i = 0; i < FUEL_BAR_SEGMENTS; ++i)
                            addGlow(rowL, barY(i), barW, pitch - gap,
                                    barW * UI_ASPECT_RATIO * RpmWidget::GLOW_SPREAD, lit,
                                    strength * barOpacity(i));
                    }
                    for (int i = 0; i < FUEL_BAR_SEGMENTS; ++i) {
                        const float opacity = barOpacity(i);
                        addRect(rowL, barY(i), barW, pitch - gap,
                                opacity >= 1.0f ? lit : PluginUtils::applyOpacity(lit, opacity));
                    }
                }
                const float rowX = rowL + charW * 3.0f;
                const float valueRight = rowR;
                const unsigned long* ink = m_shownStyle.fuelInk;   // the widget's
                const unsigned long valueColor = ink[0], fuelMuted = ink[1], labelColor = ink[2];
                static constexpr const char* kLabels[FUEL_ROWS] = { "Fuel", "Used", "Per lap", "Laps left" };
                static constexpr uint32_t kBits[FUEL_ROWS] = {
                    FuelWidget::ROW_FUEL, FuelWidget::ROW_USED, FuelWidget::ROW_AVG, FuelWidget::ROW_EST };
                const char* unit = m_shownFuel.unit == static_cast<int>(FuelWidget::FuelUnit::GALLONS) ? "g" : "L";
                // The card holds three rows: with all four on (Used is INI-only on
                // the widget), Used gives way so laps left, the reading that
                // matters, stays.
                uint32_t rows = m_shownFuel.rows;
                int shown = 0;
                for (int r = 0; r < FUEL_ROWS; ++r)
                    if (rows & kBits[r]) ++shown;
                if (shown > 3) {
                    rows &= ~static_cast<uint32_t>(FuelWidget::ROW_USED);
                    shown = 3;
                }
                // Fewer rows than the card holds sit in its middle.
                float y = contentY + dim.lineHeightNormal * static_cast<float>(3 - shown) * 0.5f;
                for (int r = 0; r < FUEL_ROWS; ++r) {
                    if (!(rows & kBits[r])) continue;
                    addLabel(kLabels[r], rowX, y, Justify::LEFT, labelColor, dim);
                    char text[16];
                    const int v = m_shownFuel.value[r];
                    const char* suffix = r == 3 ? "" : unit;
                    unsigned long color = r == 3 ? ink[3] : valueColor;
                    const uint8_t state = m_shownFuel.state[r];
                    const int a = v < 0 ? -v : v;
                    const char* sign = v < 0 ? "-" : "";
                    if (state == FUEL_NA) { snprintf(text, sizeof(text), "%s", Placeholders::NOT_AVAILABLE); color = fuelMuted; }
                    else if (state == FUEL_NONE) { snprintf(text, sizeof(text), "%s", Placeholders::GENERIC); color = fuelMuted; }
                    else if (m_shownFuel.whole[r]) snprintf(text, sizeof(text), "%s%d%s", sign, a / 10, suffix);
                    else snprintf(text, sizeof(text), "%s%d.%d%s", sign, a / 10, a % 10, suffix);
                    if (r == 3 && state == FUEL_SHOWN && m_shownFuel.lapsTint != 0)
                        color = m_shownFuel.lapsTint == 2 ? ink[5] : ink[4];
                    addString(text, valueRight, y, Justify::RIGHT, m_shownStyle.fuelFont, color, dim.fontSize);
                    y += dim.lineHeightNormal;
                }
                break;
            }
            case Page::TELEMETRY:
                drawTelemetry(rowL, contentY, rowW,
                              dim.lineHeightLarge + dim.lineHeightNormal);
                break;
            case Page::DELTA_TRACE:
                drawDeltaTrace(rowL, contentY, rowW, dim.lineHeightLarge + dim.lineHeightNormal, dim);
                break;
            case Page::GAP_CHART:
                drawGapChart(rowL, contentY, rowW, dim.lineHeightLarge + dim.lineHeightNormal, dim);
                break;
            case Page::EVENT_LOG:
                drawEvents(rowL, rowR, contentY, dim);
                break;
            case Page::NOW_PLAYING:
                drawNowPlaying(rowL, rowR, contentY, dim);
                break;
            case Page::SESSION:
                drawSessionInfo(rowL, rowR, contentY, dim);
                break;
            case Page::RPM: {
                // The RPM widget's lights (its count, gaps and colours) across the
                // card on the middle of its three rows, the top one left empty,
                // and the revs under them at the right.
                m_shownLights = RpmWidget::readLights(rpmSegments());
                m_shownRpm = readRpm();
                char text[16];
                if (m_shownRpm >= 0) snprintf(text, sizeof(text), "%d", m_shownRpm);
                else snprintf(text, sizeof(text), "%s", Placeholders::GENERIC);
                addString(text, rowR, contentY + dim.lineHeightLarge, Justify::RIGHT, digits,
                          m_shownRpm >= 0 ? primary : muted, dim.fontSize);
                const int segments = rpmSegments();
                const float barX = rowL;
                const float barW = rowW;
                const float barH = dim.lineHeightNormal;
                const float barY = contentY + dim.lineHeightNormal;
                const float gapW = HudManager::getInstance().getRpmWidget().m_bSegmentGaps
                    ? barW / static_cast<float>(segments) * RpmWidget::SEGMENT_GAP_SHARE : 0.0f;
                const float segW = (barW - gapW * static_cast<float>(segments - 1)) / static_cast<float>(segments);
                const unsigned long* ink = m_shownStyle.rpmInk;   // the widget's positive, warning, negative
                // The widget's glow behind the lit lights, all halos first.
                if (glowActive()) {
                    for (int i = 0; i < segments; ++i) {
                        if (!RpmWidget::segmentLit(i, segments, m_shownLights)) continue;
                        const unsigned long color =
                            RpmWidget::segmentInk(i, segments, m_shownLights, ink[0], ink[1], ink[2]);
                        addGlow(barX + (segW + gapW) * static_cast<float>(i), barY, segW, barH,
                                barH * RpmWidget::GLOW_SPREAD, color,
                                RpmWidget::GLOW_OPACITY *
                                    (RpmWidget::segmentRed(i, segments, m_shownLights) ? 1.0f : UiConfig::GLOW_CALM));
                    }
                }
                for (int i = 0; i < segments; ++i) {
                    addRect(barX + (segW + gapW) * static_cast<float>(i), barY, segW, barH,
                            RpmWidget::segmentColor(i, segments, m_shownLights, ink[0], ink[1], ink[2]));
                }
                break;
            }
        }
        // The content is laid out even under a full name, so the shown values
        // stay current for update()'s compares; it is then dropped.
        const float contentAlpha = alpha * (1.0f - nameAlpha);
        if (contentAlpha <= 0.0f) {
            m_strings.resize(firstString);
            m_stringSkipShadow.resize(firstString);   // parallel to m_strings
            m_quads.resize(firstQuad);
        } else if (contentAlpha < 1.0f) {
            for (size_t i = firstString; i < m_strings.size(); ++i)
                m_strings[i].m_ulColor = Motion::scaleAlpha(m_strings[i].m_ulColor, contentAlpha);
            for (size_t i = firstQuad; i < m_quads.size(); ++i)
                m_quads[i].m_ulColor = Motion::scaleAlpha(m_quads[i].m_ulColor, contentAlpha);
        }
        if (hint) {
            addStepHint(pageCenterX, rowW, contentY, dim, alpha);
        } else if (nameAlpha > 0.0f) {
            addString(kPages[static_cast<int>(page) % static_cast<int>(Page::COUNT)].name, pageCenterX, largeY,
                      Justify::CENTER, getFont(FontCategory::TITLE), Motion::scaleAlpha(primary, alpha * nameAlpha),
                      dim.fontSizeLarge);
        }
    }

    // === THE STRIP: one icon per page, in its own section, the page showing larger ===
    m_firstIconQuad = static_cast<int>(m_quads.size());
    if (stripPlace != StripPlace::OFF) {
        const int pages = CYCLE_PAGES;
        const AssetManager& assets = AssetManager::getInstance();
        if (m_iconSpriteGen != assets.themeGeneration()) {
            m_iconSpriteGen = assets.themeGeneration();
            for (int i = 0; i < pages; ++i) m_iconSprite[i] = assets.getIconSpriteIndex(kPages[i].icon);
        }
        // Appearance > UI icons off: each page's mark instead, as every UI icon.
        const bool useIcons = UiConfig::getInstance().getTitleIcons();
        // Across a row strip, down a column one (an icon's height apart there).
        const float baseSize = stripSide ? sideIcon : iconSize;
        const float pitch = stripSide ? sidePitch : iconSize / UI_ASPECT_RATIO * ICON_PITCH;
        const float stripCX = stripX + stripW * 0.5f;
        const float stripCY = stripY + stripH * 0.5f;
        // Where the strip is, in icons: the page showing, plus what is left of the
        // slide. Only the pages in the cycle are on the strip, round a RING: each
        // icon at its shortest distance d from the page showing, so stepping past
        // the last page slides on to the first rather than back across the strip.
        // The icon opposite the page showing sits on the ring's seam, where it
        // fades out over half an icon and back in on the other side (so with an
        // even page count that one is hidden at rest). Should the pages outgrow
        // the strip, the outermost also fade out over their last icon.
        const float shown = static_cast<float>(slotOf(getPage())) + m_stripFrom * (1.0f - fade);
        // Down a side the icons fade by how close they come to the card's edge
        // instead (the renderer cannot clip), the page showing's always whole.
        const float room = stripSide ? static_cast<float>(pages) : std::max(1.0f, stripW * 0.5f / pitch - 0.5f);
        // Never larger than the strip: the renderer cannot clip, and a theme
        // with no panel padding leaves the strip about one row tall (or one
        // icon wide).
        const float maxSize = stripSide ? stripW * UI_ASPECT_RATIO : stripH;
        const int ring = enabledCount();
        int slot = 0;
        for (int i = 0; i < pages; ++i) {
            if (!pageEnabled(static_cast<Page>(i))) continue;
            const float d = ringOffset(static_cast<float>(slot++) - shown);
            const float ad = d < 0.0f ? -d : d;
            float edge = room - ad;
            if (ring >= 3) {
                const float seam = (static_cast<float>(ring) * 0.5f - ad) * 2.0f;
                if (seam < edge) edge = seam;
            }
            // How much this icon is the page showing: 1 at the centre, 0 one icon out.
            const float focus = ad < 1.0f ? 1.0f - ad : 0.0f;
            float size = baseSize * (1.0f + (ACTIVE_ICON_SCALE - 1.0f) * focus);
            if (size > maxSize) size = maxSize;
            if (stripSide) {
                // Gone by the time its edge would cross the card's, over its
                // last fifth of a slot.
                const float clear = stripH * 0.5f - pitch * ad - size * 0.5f;
                const float fit = clear / (pitch * 0.2f);
                if (fit < edge) edge = fit;
            }
            edge = edge < 0.0f ? 0.0f : (edge > 1.0f ? 1.0f : edge);
            if (edge <= 0.0f) continue;
            const float opacity = edge * (INACTIVE_ICON_OPACITY + (1.0f - INACTIVE_ICON_OPACITY) * focus);
            const unsigned long ink = Motion::scaleAlpha(focus >= 0.5f ? primary : muted, opacity);
            const float x = stripSide ? stripCX : stripCX + pitch * d;
            const float y = stripSide ? stripCY + pitch * d : stripCY;
            if (useIcons && m_iconSprite[i] > 0) {
                // Drop shadow on, it takes one like the title icon; marks are
                // strings and take the strings' own.
                addIcon(x, y, m_iconSprite[i], ink, size);
                shadowLastIcon();
            } else {
                // Two letters in the icon's place, grown with it: UI icons off,
                // or an icons folder without this one (a 0 sprite is a filled square).
                const float textSize = dim.fontSizeSmall * size / iconSize;
                addString(kPages[i].mark, x, inkCenteredY(y - stripH * 0.5f, stripH, textSize),
                          Justify::CENTER, getFont(FontCategory::STRONG), ink, textSize);
            }
        }
    }

    m_shownNote = pageReferenceNote(m_page);
    addPlanTitleNote(p, dim, m_shownNote);
    setBounds(0.0f, top, p.width(), top + p.height());
}

void DashHud::resetToDefaults() {
    m_bVisible = true;
    m_bShowTitle = false;
    m_fBackgroundOpacity = SettingsLimits::DEFAULT_OPACITY;
    setScale(1.0f);
    // In the Timing HUD's spot (off by default since the Dash carries it):
    // centred across the screen, at the centre stack's top.
    setPosition(0.5f - cellsX(DEFAULT_PANEL_WIDTH_CELLS * 0.5f), CenterStack::stackBoxTop());
    m_page = DEFAULT_PAGE;
    m_enabledPages = DEFAULT_PAGES;
    m_shownPages = DEFAULT_PAGES;
    m_options = DEFAULT_OPTIONS;
    m_stripPlace = DEFAULT_STRIP_PLACE;
    m_fromPage = DEFAULT_PAGE;
    stopFade();
    clearName();
    m_noticeHold = false;
    m_returnPage = DEFAULT_PAGE;
    m_noticeSeen = NoticeTracker::getInstance().revision();
    m_timingHold.stop();
    m_heldTimeMs = -1;
    m_heldInvalid = false;
    m_segHold.stop();
    for (Neighbour& n : m_shownRows) n = Neighbour{};
    m_shownClock = LapClock{};
    m_shownBar = BarView{};
    m_shownSession = SessionClock{};
    m_shownLaps = LapsView{};
    m_shownIdeal = IdealView{};
    m_shownFuel = FuelView{};
    m_shownLights = RpmWidget::Lights{};
    m_shownEvents = EventsView{};
    m_shownSessionInfo = SessionView{};
    m_shownRpm = -1;
    m_gapFreeze.reset();
    m_barFreeze.reset();
    m_barRange.reset();
    setDataDirty();
}
