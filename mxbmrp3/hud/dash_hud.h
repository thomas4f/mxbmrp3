// ============================================================================
// hud/dash_hud.h
// Dash - one compact card that cycles through compact pages, with an icon strip
// naming the pages and marking the one showing: under it by default, above it,
// down either side (a column with room for the page showing's icon alone) or
// off (Appearance > Page strip). One panel with a section each for the page and
// the strip, themed and controlled like any HUD.
// The page's rows are as tall as the Gear and Speed widgets' content, so a
// rider who wants them beside it drags them there.
//
// The pages: Standings (the displayed rider's row between the riders just
// ahead and behind: position, race number, short name and gap), Timing (the
// running lap time and the gap to the reference lap), Laps (the last three laps
// against the best), Time (the session clock: down in a timed session, up
// otherwise, then the overtime label once a timed race runs into its extra
// laps), Fuel (the Fuel widget's rows), Gap bar (the Gap Bar HUD's fill, from
// the centre on the live gap, under the gap), Flat map (the Gap Bar's rider
// markers along the card, the race leader's crowned as on the Map), Ideal
// (last, best and ideal lap, as the Ideal Lap HUD's lap rows), RPM (the RPM
// widget's shift lights across the card, the revs under them at the right),
// Delta trace (the Delta Trace HUD's chart, drawn by the same DeltaTracePlot),
// Gap chart (the Session Charts HUD's Gap chart for the rider and the riders
// just ahead and behind), Session
// (the Session HUD's rows), Telemetry (the Telemetry HUD's traces) and Event
// log (the Event Log's three newest events). The pages carry no title: the
// strip's highlighted icon names the page, and the card's three rows go to
// data. Settings > Dash > Appearance > Page names (on by default) shows a
// stepped-to page's name in its place for a moment first (see PAGE NAMES
// below).
// Dash + / Dash - step through them (Settings > Hotkeys), crossfading with
// Motion on (see MOTION below). Settings > Dash > Pages picks which pages are in
// the cycle. With Notice focus on, a notice takes the card over while it is up,
// on the Notices HUD's coloured slab (see NOTICE TAKEOVER below). The startup
// messages (an update, "Updated to", the welcome) take it over the same way
// (see STARTUP MESSAGES below).
//
// NOTHING HOSTED. Every page is drawn here from the central data (PluginData,
// and NoticeTracker for the notice up right now) -- never by borrowing another
// HUD's primitives. The first Dash (PR #469) hosted the real HUDs scaled into
// its slots, and spent most of its effort forcing their looks into line.
//
// HOT PATH. update() runs every frame and only compares integers; the strings
// are formatted in rebuildRenderData(), which runs when a shown value changes.
// ============================================================================
#pragma once

#include <chrono>
#include <climits>
#include <vector>

#include "base_hud.h"
#include "gap_auto_range.h"
#include "hold_timer.h"
#include "official_gap_freeze.h"
#include "rpm_widget.h"
#include "session_charts_hud.h"
#include "split_crossing.h"
#include "delta_trace_plot.h"
#include "rider_flag_icons.h"
#include "../core/system_messages.h"
#include "../game/unified_types.h"

class DashHud : public BaseHud {
public:
    // The pages, in cycle order (and the strip's, left to right), the most used
    // parents first. NOTICES is not in the cycle: it is only ever up while a
    // notice holds the card (Notice focus).
    enum class Page : uint8_t {
        STANDINGS, TIMING, LAPS, TIME, FUEL, GAP_BAR, FLAT_MAP, IDEAL, RPM, DELTA_TRACE,
        GAP_CHART, SESSION, TELEMETRY, EVENT_LOG, NOTICES, COUNT
    };
    static constexpr int CYCLE_PAGES = static_cast<int>(Page::NOTICES);
    // The page a fresh install opens on: Timing, which stands in for the Timing
    // HUD (off by default since the Dash carries it).
    static constexpr Page DEFAULT_PAGE = Page::TIMING;
    // The content's width in characters, as the Ideal Lap HUD's; the panel's at
    // 100% in grid cells, padding included (both pinned by the Dash's lines-up
    // test against Ideal Lap).
    static constexpr int CONTENT_WIDTH_CHARS = 27;
    static constexpr float DEFAULT_WIDTH_CELLS = 29.0f;
    // ...and with the default strip (Right) beside it, which is what centres
    // the panel at its default place (pinned by the Dash's default-place test).
    static constexpr float DEFAULT_PANEL_WIDTH_CELLS = 32.25f;

    DashHud();
    virtual ~DashHud() = default;

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;
    const char* getIconName() const override { return "hud-dash"; }
    void resetToDefaults();

    // Dash + / Dash -: the next (+1) or previous (-1) enabled page, wrapping.
    void step(int direction);
    // The page the rider chose (saved), and the page up right now, which differs
    // while a notice holds the card.
    Page getPage() const { return m_noticeHold ? m_returnPage : m_page; }
    Page getShownPage() const { return m_page; }
    // Whether the rider has stepped a page yet: until then the card shows which
    // keys step it instead of the page (FIRST-STEP HINT). Global, not per
    // profile: saved in [Messages] beside what the startup messages have told.
    bool stepTried() const { return m_stepTried; }
    void setStepTried(bool tried) { m_stepTried = tried; setDataDirty(); }
    // Where the strip's icons start in m_quads, as last built: after the panel's
    // own quads (one unthemed, more with a theme) and the page's.
    int getFirstIconQuad() const { return m_firstIconQuad; }
    // The Delta Trace page's chart, as last drawn (test hooks).
    const DeltaTracePlot& getDeltaPlot() const { return m_deltaPlot; }
#ifdef MXBMRP3_TEST_BUILD
    // The startup message the card drew last, and time on screen for it
    // without waiting it out (STARTUP MESSAGES).
    SystemMessages::Startup testShownMessage() const { return m_shownMessage; }
    void testAdvanceMessage(float ms) { m_messageShownMs += ms; }
#endif

    // A cycle page's settings key ("standings", "timing", "laps", "time", "fuel",
    // "gapbar", "flatmap", "ideal", "rpm", "deltatrace", "gapchart", "session",
    // "telemetry", "eventlog"), and back; an unknown key is DEFAULT_PAGE. Saved by name so
    // reordering the pages moves nobody's choice.
    static const char* pageKey(Page page);
    static Page pageFromKey(const char* key);
    // The key saving whether a page is in the cycle ("page_standings", ...).
    static const char* pageEnabledKey(Page page);

    // Settings > Dash > Pages: one bit per Page. Never empty: the settings row of
    // the last page left on is locked, and a load that clears them all restores all.
    static constexpr uint32_t pageBit(Page page) { return 1u << static_cast<int>(page); }
    // The Telemetry page's traces at most: eight inputs, a segment between each
    // two of HistoryBuffers::MAX_TELEMETRY_HISTORY samples.
    static constexpr int TELEMETRY_MAX_SEGMENTS = 8 * static_cast<int>(HistoryBuffers::MAX_TELEMETRY_HISTORY - 1);
    static constexpr uint32_t ALL_PAGES = (1u << CYCLE_PAGES) - 1u;
    // A fresh install's cycle: all but Standings, Time and RPM, whose HUDs (the
    // Standings HUD, the session clock, the RPM widget) are on screen anyway.
    static constexpr uint32_t DEFAULT_PAGES =
        ALL_PAGES & ~((1u << static_cast<int>(Page::STANDINGS)) | (1u << static_cast<int>(Page::TIME)) |
                      (1u << static_cast<int>(Page::RPM)));
    bool pageEnabled(Page page) const { return (m_enabledPages & pageBit(page)) != 0; }
    // Settings > Dash options, one bit each in m_options, saved by key.
    static constexpr uint32_t OPT_PAGE_NAMES = 1u << 0;        // on by default
    static constexpr uint32_t OPT_NOTICE_TAKEOVER = 1u << 1;   // on by default
    static constexpr uint32_t DEFAULT_OPTIONS = OPT_PAGE_NAMES | OPT_NOTICE_TAKEOVER;
    struct OptionKey { const char* key; uint32_t bit; };
    static constexpr OptionKey kOptionKeys[] = {
        { "page_names", OPT_PAGE_NAMES },
        { "notice_takeover", OPT_NOTICE_TAKEOVER },
    };
    // Settings > Dash > Appearance > Page strip: where the icon strip sits
    // beside the page, clockwise from the top, or off. Saved by name
    // (stripPlaceKey), so the order can change.
    enum class StripPlace : uint8_t { TOP, RIGHT, BOTTOM, LEFT, OFF, COUNT };
    // Right by default: the strip beside the page keeps the panel three rows
    // tall, so it sits in the Timing HUD's spot without reaching down the screen.
    static constexpr StripPlace DEFAULT_STRIP_PLACE = StripPlace::RIGHT;
    static const char* stripPlaceKey(StripPlace place);    // "top", "right", ...
    static StripPlace stripPlaceFromKey(const char* key);  // unknown = the default
    StripPlace getStripPlace() const { return m_stripPlace; }
    // Shown for a startup message while switched off (STARTUP MESSAGES): drawn
    // on both surfaces, and its steps answer, until the message is done.
    bool isBorrowed() const { return m_borrowed; }
    // The broken-install message in block letters (no fonts to draw text with)
    // or in the real font; HudManager::buildInstallWarning decides.
    void setInstallPixelText(bool pixels) { m_installPixels = pixels; setDataDirty(); }
    // Whether the Dash shows notices at all (visible, Notice focus on):
    // NoticeTracker steps fully only while something does.
    bool watchesNotices() const { return isVisibleAnySurface() && (m_options & OPT_NOTICE_TAKEOVER) != 0; }
    // The riders' track positions, pushed from HudManager::updateRiderPositions;
    // kept only while the Flat map page is up.
    void updateRiderPositions(int numVehicles, const Unified::TrackPositionData* positions);
    // Whether the Dash draws the telemetry history (visible, on the Telemetry
    // page): PluginData fills the buffers only while something does
    // (HudManager::isTelemetryHistoryNeeded). Only the page up, since the Dash
    // ships on: stepping to Telemetry starts its traces fresh, as showing the
    // Telemetry HUD does.
    bool wantsTelemetryHistory() const {
        return isVisibleAnySurface() && m_page == Page::TELEMETRY;
    }

    friend class SettingsHud;
    friend class SettingsManager;

protected:
    void rebuildLayout() override;

private:
    void rebuildRenderData() override;
    // A plain filled rectangle, at this HUD's offset.
    void addRect(float x, float y, float w, float h, unsigned long color);

    // The displayed rider (the one spectated, when spectating) and the riders
    // around, as the Standings page shows them: the gap cell as
    // StandingsGap::planGap() decides it with the Standings HUD's own gap
    // table, so the two never disagree.
    // Integers only, so update() can compare a fresh one with the shown one.
    struct Neighbour {
        int raceNum = -1;    // -1 = none (leader / last / no standings)
        int position = 0;
        uint8_t gapKind = 0; // StandingsGap::Kind (uint8_t keeps the header out of here)
        uint8_t gapTint = 0; // StandingsGap::Tint
        uint8_t gapStyle = 0; // StandingsGap::Style
        int gapValue = 0;    // the plan's value: ms, laps or a lap time, by kind
        int state = 0;       // RiderState, for a state abbreviation (DNF, DSQ, ...)
        bool finished = false;   // through the flag, for the podium plates
        bool self = false;       // the displayed rider's own row
        uint8_t statusMark = 0;  // StandingsHud::StatusMark, the status column's marker
        uint8_t statusFlag = 0;  // RiderFlagIcons::Kind, for a flag marker
        int penaltyMs = 0;       // time penalty, 0 = none
        bool operator==(const Neighbour& o) const {
            return raceNum == o.raceNum && position == o.position && gapKind == o.gapKind &&
                   gapTint == o.gapTint && gapStyle == o.gapStyle && gapValue == o.gapValue && state == o.state &&
                   finished == o.finished && self == o.self && statusMark == o.statusMark &&
                   statusFlag == o.statusFlag && penaltyMs == o.penaltyMs;
        }
        bool operator!=(const Neighbour& o) const { return !(*this == o); }
    };
    static constexpr int STANDINGS_ROWS = 3;
    // The page's rows, top to bottom: the displayed rider between the riders
    // just ahead and behind, or, leading or last, with the two behind or ahead
    // so all three rows stay filled. The self row carries the rider's best lap
    // where Standings labels it; no standings leaves every row empty.
    static void findNeighbours(Neighbour (&rows)[STANDINGS_ROWS]);

    // The Timing page's figures, to the millisecond as the Timing HUD shows
    // them (so, like that HUD, the page redraws each frame while the clock
    // runs); the gap always, whatever the Timing HUD's Gap toggle.
    // With custom segments set and the player on track, the page is the Timing
    // HUD's segment line instead (TimingHud::segmentLine): the segment time, and
    // under it the delta to the summed segment bests while a completed segment is
    // held, else those bests as the target ("Best" on the title row).
    struct LapClock {
        int timeMs = -1;       // the running lap, the held split/lap time or the finish time; -1 = none
        bool finished = false; // timeMs is the finish time
        bool invalid = false;  // INVALID held for a struck lap
        bool hasGap = false;
        int gapMs = 0;         // gap to the reference lap (segments: to the summed bests)
        int targetMs = -1;     // segments, no delta: the summed bests; -1 = none
        bool operator==(const LapClock& o) const {
            return timeMs == o.timeMs && finished == o.finished && invalid == o.invalid &&
                   hasGap == o.hasGap && gapMs == o.gapMs && targetMs == o.targetMs;
        }
        bool operator!=(const LapClock& o) const { return !(*this == o); }
    };
    LapClock readLapClock() const;
    // Each gap page follows the HUD it is drawn after: the Timing page the Timing
    // HUD's live gap reference and freeze, the Gap bar page the Gap Bar's. Both
    // HUDs default to Settings > General's.
    static PbGapTracker::Ref timingReference();
    static int timingFreezeMs();
    static PbGapTracker::Ref barReference();
    static int barFreezeMs();
    // The reference lap a page's gap is against, named on the title row as on
    // its parent's: Timing's Gap, the Gap Bar's, the Delta Trace's; else none.
    static const char* pageReferenceNote(Page page);

    // The Gap bar page: the fill as the Gap Bar HUD draws it (the LIVE gap, never
    // frozen, against that HUD's Range) in BAR_STEPS steps either side of the
    // centre, and the gap text as the Gap Bar shows it, to the millisecond; under
    // the bar the last and best lap, as the Ideal page reads them.
    struct BarView {
        int fill = INT_MIN;   // -BAR_STEPS (ahead, full) .. BAR_STEPS (behind, full)
        bool hasGap = false;
        int gapMs = 0;
        bool noTrace = false; // GapBarHud::NO_TRACE_NOTE in place of the gap
        int lastMs = -1, bestMs = -1;
        bool operator!=(const BarView& o) const {
            return fill != o.fill || hasGap != o.hasGap || gapMs != o.gapMs || noTrace != o.noTrace ||
                   lastMs != o.lastMs || bestMs != o.bestMs;
        }
    };
    static constexpr int BAR_STEPS = 200;
    // Not const: resolving an Auto range moves its peak (m_barRange).
    BarView readBar();

    // The Time page: the game's session clock in whole seconds (it counts down in
    // a timed session and up otherwise) and the leader's laps to go once a timed
    // race is into its extra laps (-1 before), the two formatSessionClock reads.
    struct SessionClock {
        int seconds = INT_MIN;
        int lapsToGo = -1;
        bool operator!=(const SessionClock& o) const { return seconds != o.seconds || lapsToGo != o.lapsToGo; }
    };
    static SessionClock readSessionClock();

    // Show `next`, crossfading from what is on screen (see MOTION).
    void goTo(Page next);
    // A page's place in the strip: how many enabled pages come before it.
    int slotOf(Page page) const;
    // How many pages are in the cycle.
    int enabledCount() const;
    // An offset along the strip, in icons, taken the short way round the ring
    // of enabled pages (into -n/2 .. n/2).
    float ringOffset(float d) const;
    // The first enabled page from `from` on, wrapping.
    Page firstEnabledFrom(Page from) const;
    // Ends any fade on the spot (Motion Off, or the page set changed under it).
    void stopFade();
    // NOTICE TAKEOVER, once a frame: shows Notices when a new notice or a startup
    // message comes up, and brings the rider's page back when it ends.
    void followNotices();

    // The Laps page: the newest completed laps, newest first, and the session
    // best they are measured against. Integers only, for update()'s compare.
    static constexpr int LAPS_ROWS = 3;
    struct LapRow {
        int lapNum = -1;     // 0-based, as LapLogEntry; -1 = no lap for this row
        int lapTime = -1;
        bool hasTime = false, valid = true, viaPits = false;
        bool operator==(const LapRow& o) const {
            return lapNum == o.lapNum && lapTime == o.lapTime && hasTime == o.hasTime &&
                   valid == o.valid && viaPits == o.viaPits;
        }
    };
    struct LapsView {
        LapRow rows[LAPS_ROWS];   // newest first
        int bestMs = -1;
        int bestLapNum = -1;      // the best lap's number: one row marked, as the Lap Log
        bool oldestFirst = false; // the Lap Log's Order: drawn bottom up
        bool operator==(const LapsView& o) const {
            if (bestMs != o.bestMs || bestLapNum != o.bestLapNum || oldestFirst != o.oldestFirst) return false;
            for (int i = 0; i < LAPS_ROWS; ++i) if (!(rows[i] == o.rows[i])) return false;
            return true;
        }
        bool operator!=(const LapsView& o) const { return !(*this == o); }
    };
    static LapsView readLaps();

    // The Gap chart page: the Session Charts HUD's Gap chart for the displayed
    // rider and the riders just ahead and behind. Redrawn when a lap is logged
    // (handlesDataType), when who those riders are changes, or the parent's look.
    struct GapChartView {
        int mine = -1, ahead = -1, behind = -1;   // race numbers; -1 = none
        int sessionGeneration = 0;
        uint32_t elements = 0;                    // the Session Charts HUD's Elements
        bool operator==(const GapChartView& o) const {
            return mine == o.mine && ahead == o.ahead && behind == o.behind &&
                   sessionGeneration == o.sessionGeneration && elements == o.elements;
        }
        bool operator!=(const GapChartView& o) const { return !(*this == o); }
    };
    static GapChartView readGapChart();
    void drawGapChart(float x, float y, float w, float h, const ScaledDimensions& dim);

    // The Ideal page: the Ideal Lap HUD's lap rows (its central IdealLapData), the
    // previous ideal kept so a lap that beats the ideal shows by how much.
    struct IdealView {
        int lastMs = -1, bestMs = -1, idealMs = -1, prevIdealMs = -1;
        bool operator==(const IdealView& o) const {
            return lastMs == o.lastMs && bestMs == o.bestMs && idealMs == o.idealMs && prevIdealMs == o.prevIdealMs;
        }
        bool operator!=(const IdealView& o) const { return !(*this == o); }
    };
    static IdealView readIdeal();

    // The Fuel page: the Fuel widget's rows (its Rows and unit), up to the card's
    // three. Each figure in tenths, rounded as that widget prints it, `whole`
    // from 10 up where it drops the decimal; -1 = none yet, -2 = not available
    // (not on track: the tank is the player's).
    static constexpr int FUEL_ROWS = 4;   // Fuel, Used, Avg, Laps left
    // The Fuel page's tank bar: a stack of segments, the RPM widget's look
    // stood on end, lit from the bottom to the share of the tank left, the top
    // one faded up in RpmWidget::LEAD_STEPS steps as the RPM lights are.
    static constexpr int FUEL_BAR_SEGMENTS = 10;
    struct FuelView {
        int value[FUEL_ROWS] = { INT_MIN, INT_MIN, INT_MIN, INT_MIN };   // tenths, signed
        bool whole[FUEL_ROWS] = {};
        uint8_t state[FUEL_ROWS] = {};   // FUEL_SHOWN, FUEL_NONE ("-") or FUEL_NA ("N/A")
        uint8_t lapsTint = 0;   // 0 plain, 1 warning, 2 critical (FuelEstimate)
        uint8_t level = 0;      // the tank bar's lit segments, of FUEL_BAR_SEGMENTS
        uint8_t step = RpmWidget::LEAD_STEPS - 1;   // the top lit one's fade step (last = whole)
        uint32_t rows = 0;      // FuelWidget::RowFlags shown
        int unit = 0;
        bool operator!=(const FuelView& o) const {
            if (lapsTint != o.lapsTint || level != o.level || step != o.step || rows != o.rows ||
                unit != o.unit) return true;
            for (int i = 0; i < FUEL_ROWS; ++i)
                if (value[i] != o.value[i] || whole[i] != o.whole[i] || state[i] != o.state[i]) return true;
            return false;
        }
    };
    static constexpr uint8_t FUEL_SHOWN = 0, FUEL_NONE = 1, FUEL_NA = 2;
    static FuelView readFuel();

    // The RPM widget's light count, which the RPM page follows.
    static int rpmSegments();
    // The RPM page's figure: the displayed bike's revs to the nearest RPM_STEP,
    // -1 without telemetry. Stepped so it settles enough to read and rebuilds the
    // card at the step, not at the telemetry rate.
    static constexpr int RPM_STEP = 50;
    static int readRpm();
    // What the RPM and Fuel pages take from their widgets besides the values:
    // the Fuel widget's font and both widgets' colours (overrides included),
    // compared in update() so a change made on a widget reaches the Dash at
    // once, not at the next value change.
    struct PageStyle {
        int fuelFont = 0;
        unsigned long fuelInk[6] = {};   // secondary, muted, tertiary, primary, warning, negative
        unsigned long rpmInk[3] = {};    // positive, warning, negative
        bool operator!=(const PageStyle& o) const {
            auto differ = [](const unsigned long* a, const unsigned long* b, int n) {
                for (int i = 0; i < n; ++i) if (a[i] != b[i]) return true;
                return false;
            };
            return fuelFont != o.fuelFont || differ(fuelInk, o.fuelInk, 6) || differ(rpmInk, o.rpmInk, 3);
        }
    };
    static PageStyle readPageStyle();
    PageStyle m_shownStyle;

    // The Event Log page: the newest events the Event Log HUD shows (its event
    // filter), in its order, with its icons. What decides a rebuild: a new
    // event, the log cleared, or one of those settings changed.
    static constexpr int EVENT_ROWS = 3;
    struct EventsView {
        size_t count = 0;            // entries in the log
        long long newestTick = 0;    // the newest entry's steady time
        uint32_t filter = 0;
        uint8_t order = 0;
        bool icons = false;
        bool operator!=(const EventsView& o) const {
            return count != o.count || newestTick != o.newestTick || filter != o.filter ||
                   order != o.order || icons != o.icons;
        }
    };
    static EventsView readEvents();
    // The Session page: the Session HUD's rows (its Rows and icons), up to the
    // card's three. What its text is made of, as last drawn; update() compares
    // it with the live values in place (sessionInfoChanged).
    struct SessionView {
        int state = -1, length = -1, numLaps = -1, session = -1, eventType = -1, serverType = -1,
            conditions = -1;
        float air = -1.0f, track = -1.0f;
        bool multiRider = false, icons = false;
        uint32_t rows = 0;
        int units = 0;
        char server[100] = {};
        char trackName[100] = {};
    };
    static void readSessionInfo(SessionView& out);
    bool sessionInfoChanged() const;
    // The Delta Trace page: the Delta Trace HUD's chart (its reference and
    // Peaks), drawn by the same DeltaTracePlot, without its axis labels.
    DeltaTracePlot m_deltaPlot;
    void drawDeltaTrace(float x, float y, float w, float h, const ScaledDimensions& dim);
    void drawEvents(float left, float right, float contentY, const ScaledDimensions& dim);
    void drawSessionInfo(float left, float right, float contentY, const ScaledDimensions& dim);
    void drawGapBar(float rowL, float rowR, float contentY, float pageCenterX, const ScaledDimensions& dim);
    void drawLaps(float rowL, float rowR, float contentY, const ScaledDimensions& dim);
    // Laps rows: the lap number right-aligned in its first three characters, the
    // lap time right-aligned eight further on ("1:23.456"), then the gap to the
    // best at the row's end.
    static constexpr int LAPS_LAP_CHARS = 3;
    static constexpr int LAPS_TIME_CHARS = 9;   // a space and "1:23.456"
    // Ideal rows: "Ideal" in its five characters, then the time as on Laps.
    static constexpr int IDEAL_LABEL_CHARS = 5;
    // The Flat map page: the Gap Bar's markers (GapBarHud::collectMarkers) along
    // the content from `left`, `width` wide, centred in the card's box, with the
    // Gap Bar's split ticks on the box edges and labels. Opponents always (they
    // are the page), the ghost as the Gap Bar's Markers shows it; in a race the
    // leader's marker is the crown, as on the Map. Redrawn at the Gap Bar's own
    // rate while up (FLAT_MAP_INTERVAL_MS).
    void drawFlatMap(float left, float width, float boxY, float boxH, const ScaledDimensions& dim);
    static constexpr int FLAT_MAP_INTERVAL_MS = 16;
    // The same convenience copy as GapBarHud::m_riderPositions: only raceNum +
    // trackPos are read, which PluginData also stores but exposes no per-rider
    // accessor for. Replaced wholesale by assign(), emptied off the page.
    // raw-cache: as GapBarHud's (see there)
    std::vector<Unified::TrackPositionData> m_riderPositions;
    std::chrono::steady_clock::time_point m_mapDrawnAt{};
    RiderFlagIcons m_flagIcons;
    // The first-step hint, over the card's content box at the page's alpha.
    void addStepHint(float centerX, float width, float y, const ScaledDimensions& dim, float alpha);
    // A startup message (STARTUP MESSAGES) on the card: its name large, the
    // way on under it.
    void addMessage(SystemMessages::Startup message, float centerX, float width, float y, const ScaledDimensions& dim);
    // One row of block text (core/pixel_text.h), centred on centerX with its ink
    // centred in the row the font would give the same string: the broken-install
    // message, when there are no fonts to draw it with.
    void addPixelText(const char* text, float centerX, float rowTop, float fontSize, unsigned long color);
    // The Telemetry page (dash_hud_pages.cpp): the traces over the box at x, y, w, h.
    void drawTelemetry(float x, float y, float w, float h);
    // The Standings page (dash_hud_pages.cpp): three rows across the card's width.
    void drawStandings(float rowLeft, float right, float contentY, const ScaledDimensions& dim);

    // The values shown, as last rendered: update() rebuilds when one differs.
    RpmWidget::Lights m_shownLights;   // the RPM page's, as RpmWidget::readLights gives them
    int m_shownRpm = -1;               // the RPM page's figure (readRpm)
    uint32_t m_shownNoticeRevision = 0;
    Neighbour m_shownRows[STANDINGS_ROWS];
    LapClock m_shownClock;
    // Last and best lap as the Gap bar page prints them: formatted when the
    // value changes, not on every rebuild.
    struct LapText {
        int ms = -2;
        char text[16] = "";
        const char* get(int lapMs);
    };
    LapText m_lastText, m_bestText;
    BarView m_shownBar;
    const char* m_shownNote = nullptr;   // the title row's reference, as drawn
    SessionClock m_shownSession;
    LapsView m_shownLaps;
    GapChartView m_shownGapChart;
    // The Gap chart's series and range sample, kept so a rebuild reuses their
    // storage.
    SessionChartsHud::NeighbourGaps m_gapSeries;
    std::vector<long long> m_gapRangeSample;
    IdealView m_shownIdeal;
    FuelView m_shownFuel;
    EventsView m_shownEvents;
    SessionView m_shownSessionInfo;
    uint32_t m_shownTelemetryRevision = 0;   // HistoryBuffers::revision, as drawn
    // The Standings page's tracked-rider plate colours, one per row, looked
    // up by name only when a row's rider (number or name) or the tracked list
    // changes (the page rebuilds with every gap change).
    // name: a reused number; sprite: the tracked rider's icon, 0 = not tracked
    struct PlateTint { int raceNum = -1; char name[32] = {}; unsigned long color = 0; int sprite = 0; };
    PlateTint m_plateTint[STANDINGS_ROWS];
    uint32_t m_plateTintRevision = 0;        // PluginData::getTrackedRidersRevision, as read
    int m_caretSprite = 0;                   // the brand mark's, as the Standings HUD's
    unsigned int m_caretSpriteGen = 0xFFFFFFFFu;   // AssetManager::themeGeneration it was read under
    bool m_wasHistoryUser = false;           // wantsTelemetryHistory() last frame
    // The Timing and Gap bar pages' gaps, each held on the official one for the
    // freeze after each split exactly as on the Timing HUD and Gap Bar: one freeze
    // each, since the two HUDs can follow different references and freezes.
    OfficialGapFreeze m_gapFreeze;
    OfficialGapFreeze m_barFreeze;
    // The Timing page's big time, held on each split's or lap's official time
    // (INVALID for a struck lap) for the Timing HUD's freeze, as that HUD
    // (TimingHud::processTimingUpdates); a pit lap holds nothing.
    SplitCrossingDetector m_timingCrossings;
    HoldTimer m_timingHold;
    int m_heldTimeMs = -1;
    bool m_heldInvalid = false;
    // In segment mode, each completed custom segment held for the same freeze, as
    // the Timing HUD holds it (its own hold, since that HUD stops while hidden).
    HoldTimer m_segHold;
    unsigned int m_segCompletion = 0;        // SegmentTimerData::completionCounter, as read
    void followSegmentHold();
    // The Gap bar page's Auto range: its own peak, so it scales as the Gap Bar
    // HUD does without reading that HUD's state.
    GapAutoRange m_barRange;
    Page m_page = DEFAULT_PAGE;
    uint32_t m_enabledPages = DEFAULT_PAGES;
    uint32_t m_shownPages = DEFAULT_PAGES;   // the set the strip was last laid out for
    uint32_t m_options = DEFAULT_OPTIONS;   // OPT_* bits
    StripPlace m_stripPlace = DEFAULT_STRIP_PLACE;
    PanelWant m_sideWant;   // the panel beside a Left/Right strip, refilled in place
    // What both Left/Right columns are asked for over the page's height, so the
    // panel rounds to whole cells with no remainder and both cards come out one
    // height.
    float m_sideStripExtra = 0.0f;
    // Down a side, the page showing's icon fills this share of its third.
    static constexpr float SIDE_ACTIVE_SHARE = 0.8f;

    // PAGE NAMES (OPT_PAGE_NAMES). A step by the rider shows the page's name in
    // its place for NAME_HOLD_MS, then the name crossfades into the content over
    // the Motion fade (a cut with Motion off). Only the rider's steps: a notice
    // taking over, or the rider's page coming back after one, goes straight to
    // the content. A step while a name shows fades that name out as the page it
    // belongs to leaves (m_fromName), so nothing pops.
    bool m_nameUp = false;               // m_page's name is showing or fading
    long long m_nameStartUs = 0;         // when it went up, on Motion's clock
    float m_fromName = 0.0f;             // the name's share of the page fading out
    // m_page's name's share of the card now: 1 while held, falling to 0.
    float nameShare() const;
    void clearName() { m_nameUp = false; m_fromName = 0.0f; }

    // NOTICE TAKEOVER (OPT_NOTICE_TAKEOVER). A new notice (a NoticeTracker
    // revision not yet seen, other than "none") takes the card over; when no
    // notice is up any more, the page the rider was on comes back. A step during a
    // takeover brings that page back at once (the rider dismissing the notice):
    // that notice does not take over again, the next one does.
    bool m_noticeHold = false;
    Page m_returnPage = DEFAULT_PAGE; // the rider's page while held
    uint32_t m_noticeSeen = 0;           // the revision last taken over or stepped away from

    // STARTUP MESSAGES (SystemMessages: a broken install, an update available,
    // "Updated to", the welcome) hold the card the same way, under any race
    // notice the Dash takes over for (a broken install over those too) and
    // whatever Notice focus says: the Dash is where they are told. Switched off,
    // the Dash is still shown (BORROWED) for all but the welcome -- "Updated to"
    // only with the update check on -- and only the message shows, without the
    // strip, until it is done; a broken install drops the strip too (its icons
    // may be what is missing). A step while one shows answers it for this
    // launch; the update and "Updated to" also go after MESSAGE_SHOWN_MS on screen, the
    // welcome only when answered, a broken install never. The settings menu
    // opening answers all but the broken install (HudManager), and a click on
    // the card opens it, on the tab the message names.
    static constexpr float MESSAGE_SHOWN_MS = 5000.0f;
    using Startup = SystemMessages::Startup;
    // The message the Notices page shows (None under a race notice).
    Startup cardMessage() const;
    void answerMessage(Startup message);
    // Its time on screen, once a frame while visible: the clock runs only while
    // the card shows it and the HUDs are not hidden.
    void tickMessage();
    // A left click on the card while it shows one opens the menu, once a frame.
    void followMessageClick();
    // Whether the message owed shows with the Dash switched off, as above.
    static bool borrows(Startup message);
    // Once a frame: switched off, shown while a message it is shown for is owed.
    void followBorrowed();
    bool m_borrowed = false;
    bool m_installPixels = true;
    Startup m_shownMessage = Startup::None;   // the one last drawn
    Startup m_timedMessage = Startup::None;   // the one m_messageShownMs belongs to
    float m_messageShownMs = 0.0f;
    std::chrono::steady_clock::time_point m_messageTick{};

    // FIRST-STEP HINT: "Page Up / Page Down" (the Dash - / Dash + bindings, as
    // bound) over the page until the first step, which the Dash drops for good.
    // Laid out in a page name's place, so the page under it stays current.
    bool m_stepTried = false;
    uint64_t m_shownHintKeys = 0;        // the bindings the hint last named
    static uint64_t hintKeys();          // those bindings, packed

    // MOTION (Settings > Appearance > Motion; Off = a cut). A step crossfades the
    // card -- the game draws every quad before every string and clips nothing,
    // so sliding text would run past the card's edges -- and slides the strip so
    // the page showing sits under the card's centre, on the same clock. The
    // fade starts from what was on screen: a step mid-fade fades out the page
    // coming in from the alpha it had reached (never popping it to full), and the
    // strip slides on from where it was.
    Page m_fromPage = DEFAULT_PAGE;   // the page fading out
    float m_fromAlpha = 0.0f;          // its alpha when the fade started
    float m_inFrom = 0.0f;             // the incoming page's alpha when the fade started
                                       // (above 0 when stepping back to the page fading out)
    float m_stripFrom = 0.0f;          // the strip's offset from m_page, in icons, at the start
    float m_fadeMs = 0.0f;             // this fade's length; 0 = none running
    long long m_fadeStartUs = 0;       // on Motion's clock (DigitRoll::nowUs)
    // The strip's icon sprites, looked up once per theme (a lookup builds a
    // std::string, and the Timing page rebuilds every frame): a theme's icons
    // change only with AssetManager::themeGeneration(), which keys them.
    int m_iconSprite[static_cast<int>(Page::COUNT)] = {};
    unsigned int m_iconSpriteGen = 0xFFFFFFFFu;
    int m_firstIconQuad = 0;
    // 0..1 eased progress of the current fade (1 when none is running).
    float fadeProgress() const;
};
