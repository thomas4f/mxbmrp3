// ============================================================================
// hud/deck_hud.h
// The decks: small groups of readouts drawn as one arcade-style graphic,
// each its own movable panel, all on the Decks tab.
//   Speed    the gear (rolling), the speed and its unit, the rider's stance
//            once the game sends it; a block meter of the revs, hot from the
//            shift point; off the gate, the reaction as a stamp (REACT_US)
//   Race     the position of the field and the session's short name, the lap
//            and the session clock under it; the session's progress
//   Timing   the lap ticking, the delta to the reference (both held at each
//            split and the line, with the sector's top speed), the last and
//            the best lap, a record tag; the lap's sectors, toned once done
//   Wheelie  a gauge: the nose-up angle on an arc with the balance zone and
//            a needle, the hold timer toned MORE / HOLD / EASE and the
//            session's longest (MAX) (deck_coach.h)
//   Slide    a warning gauge: the angle on a pendulum (grip green, then
//            sliding, ease, save it), how long the slide has lasted, MAX
//   Combo    the chain's points and multiplier, its tricks as chips (the one
//            turning lit), the session score rolling up and the best combo;
//            the chain's time left as blocks shrinking to the middle, red
//            and blinking as it runs out; LANDED +points or BAILED stamped
// The later decks (deck_hud_more.cpp, Standings deck_hud_standings.cpp;
// rules in deck_score.h):
//   Gap      the gap to the reference lap (held at the splits) and its time,
//            the last sector against its best, the ideal lap; a gap bar
//   Rival    the rider fighting the player (the one ahead, or the one behind
//            when closer): number, name, gap held at the splits; in a race
//            their last lap, in practice their best; the gap GAINED or LOST
//            since the last split tagged, against the same rider
//   Spin     a gauge: the rear wheel's slip on a block meter (hooked green,
//            spinning red), how long it has slipped, MAX
//   Inputs   the throttle and the front brake: their last 2 s as lines (the
//            Telemetry HUD's history) and a column each, filled to now
//   Standings  a list: the leader always on top, then the riders around the
//            player (half the rest ahead, the others behind), each with the
//            gap to the player and any time penalty
//   Session  the server's name, then the track and the format (the Session
//            HUD's rows)
//   Notice   one queue for what the plugin tells the player: the race
//            notices (NoticeTracker), achievements and its own messages
//            (ToastFeed). A notice that calls for action or marks a lap goes
//            first; a toast next; the race's standing ones (last lap,
//            finished, default setup) when nothing else is up. The tab names
//            the source, the stamp counts what waits (deck_hud_notice.cpp)
//   Fuel     the laps the tank has left, the fuel in it and the average a
//            lap (the Fuel widget's figures); the tank as blocks, warm under
//            two laps
//   Media    Now Playing: the track and artist, the time played and left;
//            shuffle, previous, play, next, loop and the volume to click, and
//            the track as a bar to drag (deck_hud_media.cpp)
// And one more, the CAROUSEL (deck_hud_carousel.cpp): a deck of its own
// (its own place, size and settings) showing one of the wide decks at a time,
// in turn, as that deck draws.
// Combo needs the FMX signals (GAME_HAS_FMX); the others work in every game.
//
// What a deck shows is gathered here (deck_hud_gather.cpp); how it LOOKS is
// its theme's layout (themes/<name>/decks.ini or decks-slanted.ini,
// hud/deck_layout.h), drawn by one generic drawer (deck_hud_draw.cpp). The
// Slanted switch on the Decks tab picks the Slanted layout for every deck, so
// a set reads as one graphic.
//
// REBUILDS. A deck rebuilds only when what it SHOWS changed: update() reads a
// handful of quantized numbers (the shown tenth, the lit block, the gear) into
// a key and compares it with the last one, so the formatting and the quads
// run at the rate the picture changes, not the frame rate.
// ============================================================================
#pragma once

#include "base_hud.h"
#include "deck_coach.h"
#include "deck_score.h"
#include "deck_layout.h"
#include "deck_meter.h"
#include "../core/fmx_trick_stats.h"
#include "digit_roll.h"
#include "now_playing_card.h"
#include "official_gap_freeze.h"
#include "page_flip.h"
#include "row_slide.h"
#include "split_crossing.h"
#include "standings_gap_plan.h"

#include <chrono>
#include <climits>
#include <cstdint>
#include <string>
#include <vector>

struct DeckAsset;

class DeckHud : public BaseHud {
public:
    // Same order as DeckLayout::DeckId: a kind indexes its layout deck directly.
    enum class Kind : int {
        Speed = 0, Race, Timing, Wheelie, Slide, Combo,
        Gap, Rival, Spin, Inputs, Standings, Session, Notice, Fuel, Media, LapLog,
        COUNT
    };
    static_assert(static_cast<int>(Kind::COUNT) == DeckLayout::DECK_COUNT, "one layout deck per kind");

    // HudManager's slots: one per kind, then the carousel's.
    static constexpr int CAROUSEL_SLOT = static_cast<int>(Kind::COUNT);
    static constexpr int SLOT_COUNT = CAROUSEL_SLOT + 1;
    // The registration name (HudManager), the INI section and the harness id,
    // by slot.
    static const char* harnessId(int slot);
    static const char* sectionName(int slot);
    // The deck's name, as its title tab says it ("" for the Notice, whose tab
    // names the notice). The one list: each deck's gather sets it, and the
    // carousel measures its decks' names to give the dots one place.
    static const char* title(Kind kind);

    explicit DeckHud(Kind kind);
    struct CarouselTag {};
    explicit DeckHud(CarouselTag);
    virtual ~DeckHud() = default;

    void update() override;
    // The style's [fonts] between the player's pick and the theme's
    int getFont(FontCategory category) const override;
    bool handlesDataType(DataChangeType dataType) const override;
    void resetToDefaults();

    // The kind it draws (a carousel's: the deck showing now)
    Kind kind() const { return m_kind; }
    bool isCarousel() const { return m_carousel; }
    // A carousel on screen whose notices take it over: NoticeTracker must step
    // for it, as for the Dash (DashHud::watchesNotices), with no other panel up
    bool watchesNotices() const { return m_carousel && isVisibleAnySurface() && (m_carOptions & CAR_NOTICES) != 0; }

    // THE CAROUSEL. One deck-sized frame showing the decks it is given, one at
    // a time, in this order (the wide ones, the Decks workbench's set). A step
    // -- the rider's (Deck + / Deck -, a click on the title), or the Auto
    // clock's (off by default) -- shows the next deck. The rider's step puts
    // the deck's name up in its place first, then its readouts
    // (PageFlip, page_flip.h, as the Dash's pages). The title tab carries a
    // marker of where in the turn it is. With Notice focus on, a new race
    // notice takes it over for CAROUSEL_NOTICE_US (its clock waits), as on the
    // Dash; a step dismisses it. Combo only where the game has FMX; Now Playing
    // only while a player plays or pauses (carouselOn). Notice stays out:
    // Notice focus is its turn.
    static constexpr Kind CAROUSEL_KINDS[] = { Kind::Timing, Kind::Gap, Kind::Rival, Kind::Session, Kind::Inputs,
                                               Kind::LapLog,
#if GAME_HAS_FMX
                                               Kind::Combo,
#endif
                                               Kind::Media };
    static constexpr int CAROUSEL_SIZE = static_cast<int>(sizeof(CAROUSEL_KINDS) / sizeof(CAROUSEL_KINDS[0]));
    static constexpr uint32_t CAROUSEL_ALL = (1u << CAROUSEL_SIZE) - 1u;
    static constexpr uint32_t CAROUSEL_MEDIA = 1u << (CAROUSEL_SIZE - 1);
    static_assert(CAROUSEL_KINDS[CAROUSEL_SIZE - 1] == Kind::Media, "CAROUSEL_MEDIA is the last bit");
    static constexpr uint32_t CAROUSEL_TIMING = 1u;
    static_assert(CAROUSEL_KINDS[0] == Kind::Timing, "CAROUSEL_TIMING is the first bit");
    // The turn a new install gets: all but Timing, a starter deck already on
    // screen beside it (resetToDefaults)
    static constexpr uint32_t CAROUSEL_DEFAULT = CAROUSEL_ALL & ~CAROUSEL_TIMING;
    static constexpr uint32_t CAR_NAMES = 1u, CAR_NOTICES = 2u;   // m_carOptions
    enum class Marker : uint8_t { Off = 0, Dots, Count, COUNT };
    static constexpr int AUTO_STEPS_S[] = { 0, 5, 8 };            // Auto: Off, 5 s, 8 s
    static constexpr int AUTO_STEP_COUNT = static_cast<int>(sizeof(AUTO_STEPS_S) / sizeof(AUTO_STEPS_S[0]));
    static constexpr int64_t CAROUSEL_NOTICE_US = 4000000;
    // One step through the decks switched on, +1 or -1; named: the rider's,
    // which puts the deck's name up first (Deck names on)
    void carouselStep(int direction, bool named);

    // SLANTED, ONE SWITCH FOR EVERY DECK (Settings > Decks), so a set reads as
    // one graphic: the theme's decks-slanted.ini layout, else its decks.ini
    // (AssetManager::getThemeDeck). Saved once, in [Decks]
    // (SettingsManager::cap_Decks).
    static void setSlanted(bool slanted);
    static bool slanted() { return s_slanted; }
    // BARS AND STAMPS, likewise one switch each for every deck (Settings >
    // Decks > Style), saved in [Decks]. Bars off drops the bar a deck runs
    // along its foot (isBar); a meter that is the deck's readout (Speed's
    // revs, the riding decks' arcs, blocks and traces) stays. Stamps off
    // drops the tag a deck raises at its top right (NEW BEST, +2, PAUSED).
    static void setBars(bool on);
    static bool bars() { return s_bars; }
    static void setStamps(bool on);
    static bool stamps() { return s_stamps; }
    // TITLE, OPACITY AND SCALE, one each for every deck too (Settings > Decks >
    // Style), saved in [Decks]: a set keeps one look, and lines up on its cell
    // grid at any size. Each deck's own BaseHud value follows them
    // (applyShared), and so does its theme: a deck draws in the panel theme,
    // never in one of its own. app_Decks applies them after the per-deck
    // sections, so a hand-edited [XxxDeck] key cannot set one deck apart.
    static void setSharedTitle(bool on);
    static bool sharedTitle() { return s_title; }
    static void setSharedOpacity(float opacity);   // 0..1, to the percent
    static float sharedOpacity() { return s_opacity; }
    static void setSharedScale(float scale);       // MIN_SCALE..MAX_SCALE, to the percent
    static float sharedScale() { return s_scale; }
    static constexpr float MIN_SCALE = 0.1f, MAX_SCALE = 3.0f;   // a HUD's Scale range
    static void applyShared();
    const DeckAsset* activeLayout() const;
    // Measure the faces the shown decks draw in (AssetManager::digitInk), at a
    // settings load rather than in their first rebuild: 1-5 ms a face.
    static void warmFonts();

    // How long a stamp ("NEW BEST", "+620") stays up; Speed's reaction longer.
    static constexpr int64_t STAMP_US = 2500000;
    static constexpr int64_t REACT_US = 10000000;
    // The least gap change at a split the Rival deck tags (less shows 0.00).
    static constexpr int RIVAL_TAG_MIN_MS = 5;
    // The Combo deck's chain meter: under this share of the chain's time
    // left it is running out (red), blinking on and off every BLINK_US.
    static constexpr float CHAIN_LOW = 0.25f;
    static constexpr int64_t BLINK_US = 100000;
    static bool chainBlinkOn(int64_t nowUs) { return (nowUs / BLINK_US) % 2 == 0; }

protected:
    // An animation frame (rows sliding, chips rising, the gear rolling): what
    // the last gather collected, moved on and drawn again. New data gathers.
    void rebuildLayout() override;

private:
    void rebuildRenderData() override;
    // The gathered fields that move with time alone: the Standings rows'
    // slide and the Combo chips' rise. Both rebuilds call it.
    void stepAnimations();
    // Draw what the last gather collected (bounds, theme pictures, the deck)
    void drawGathered();
    bool m_drawsNothing = false;   // the last gather left nothing up (Notice)

    // What a deck shows, gathered once per rebuild into fixed buffers; the
    // layout draws it.
    struct Rows {
        // 48: the Session deck's format line ("Race 1 (10:00 + 2 laps), In
        // Progress") whole; a layout's `fit` shrinks or cuts what is wider
        char text[DeckLayout::FIELD_COUNT][48];
        uint8_t tone[DeckLayout::FIELD_COUNT];
        // The pill field's words, oldest first, each its own tone
        // (TONE_NORMAL: the field's), and how far each stands above its
        // place at rest in a stack, in pills (0 the newest's place; between
        // whole pills while they slide)
        char pillWord[DeckLayout::MAX_PILLS][DeckLayout::PILL_WORD];
        uint8_t pillTone[DeckLayout::MAX_PILLS];
        float pillLift[DeckLayout::MAX_PILLS];
        int pillCount = 0;
        int pillFirst = 0;   // the chain's trick number pill 0 shows, for its rise
        DeckMeter::Data meter[DeckLayout::METER_COUNT];
        // A list deck's rows (the ROW_ fields of each), and the first row
        // after a skip in the places (-1 none)
        char rowText[DeckLayout::MAX_ROWS][DeckLayout::ROW_FIELD_COUNT][32];
        uint8_t rowTone[DeckLayout::MAX_ROWS][DeckLayout::ROW_FIELD_COUNT];
        int rowCount = 0;
        int rowBreakAt = -1;
        // Standings: rows each row still has to slide to its place (+ below,
        // - above; 0 at rest), from m_placeSlide, and whose row it is
        float rowShift[DeckLayout::MAX_ROWS];
        int rowNum[DeckLayout::MAX_ROWS];
        // A field in a colour the deck picks rather than a tone (the Notice
        // deck: the notice's own, an achievement's metal); 0 = its tone's
        unsigned long ink[DeckLayout::FIELD_COUNT];
        // The deck's icon (an `icon` box), its colour, and a glow under the
        // `glow` boxes: its opacity (0 none) and colour
        int icon = 0;
        unsigned long iconInk = 0;
        // The Now Playing deck's buttons: each one's icon (0: not drawn) and colour
        int controlSprite[DeckLayout::CONTROL_COUNT] = {};
        unsigned long controlInk[DeckLayout::CONTROL_COUNT] = {};
        float glow = 0.0f;
        unsigned long glowColor = 0;
    };
    Rows m_rows;   // reused: no per-rebuild stack churn, nothing allocated

    void clearRows();
    void setText(DeckLayout::Field f, const char* s, uint8_t tone = DeckLayout::TONE_NORMAL);
    void gatherSpeed();
    void gatherRace();
    void gatherTiming();
    // A riding gauge's two times: the spell now (or the last, dimmed) and MAX
    void setGaugeTimes(float t, bool active, float max);
    void gatherWheelie();
    void gatherSlide();
    void gatherCombo();
    void gatherStamp();
    // The later decks (deck_hud_more.cpp)
    void gatherGap();
    void gatherRival();
    void gatherSpin();
    void gatherInputs();
    void gatherStandings(int rows);
    // Lap Log (deck_hud_laplog.cpp): the last laps, oldest on top, and the
    // lap running at the foot, into the rows
    void gatherLapLog(int rows);
    void gatherSession();
    void gatherFuel();
    // Now Playing (deck_hud_media.cpp): what plays, into the rows, and the
    // controls' hit boxes from the layout's boxes and meters; the clicks and
    // drags once a frame
    void gatherMedia(const DeckLayout::Deck* deck);
    void followMedia();
    // Notice (deck_hud_notice.cpp): what is up, into the rows; false when
    // nothing is (the deck then draws nothing, and takes no click)
    bool gatherNotice(const DeckLayout::Deck* deck);
    // Notice: what the deck shows, the toast or the notice. Read by the
    // gather, the key and the click alike.
    enum class NoticeUp : uint8_t { None, Notice, Toast, Preview };
    NoticeUp noticeUp() const;
    void clickNotice();
    // Notice: the detail over the two detail lines, broken where the layout's
    // note-detail text runs out of room
    void setNoticeDetail(const DeckLayout::Deck* deck, const char* detail);
    // Standings: the places shown in `rows` rows (the leader, then the window
    // around the player), into places[]; the count, and the first row after
    // a skip (-1 none). Read by the gather and the key alike.
    int standingsPlaces(int rows, int* places, int* breakAt) const;
    // The signed gap from the player to the rider in place `p` (ahead < 0),
    // ms; INT_MAX when there is none (alone, no lap yet, a lap apart: then
    // *laps says how many, signed the same way). In a race, the Standings
    // HUD's own rule (StandingsGap::planGap, measured from the player), so its
    // Live updates and the deck's gaps never disagree.
    int gapToPlayer(int p, int* laps) const;
    // The order the places are in: the Standings HUD's running order, live
    // with Live updates on Both, as the Position widget and Dash read it
    static const std::vector<int>& runningOrder();
    // Speeds in the Speed widget's unit: the decks borrow units, not settings
    static bool speedKmh();
    // Race, Standings: a place won or lost in a ranked session, stamped
    void stampPlaceChange(bool running);
    void fillSectorMeter(DeckMeter::Data& m) const;   // the Timing deck's sectors
    static bool isLaterKind(Kind kind) { return kind >= Kind::Gap && kind < Kind::COUNT; }
    void resetMore();
    void stepMoreTelemetry(float dt, bool crashed);
    void stepMoreEvents(int64_t nowUs);
    void detectDrop(int64_t nowUs);   // Speed's reaction: the gate drop, back-dated
    // Timing, Gap and Rival: a split or the line crossed, and the holds after it
    bool stepCrossings(int64_t nowUs);
    // The live gap's reference lap (General's), its freeze stepped, and the
    // Lap Log's running time toned by it
    static PbGapTracker::Ref freezeRef();
    void stepFreeze();
    uint8_t runningTone() const;
    void onSector(int sector, int runMs, bool valid);
    bool held(int64_t nowUs) const { return nowUs < m_holdUntilUs; }
    void stepRival(int64_t nowUs);
    // Race, Rival: the shown rider's place, -1 while there is nothing to rank
    // (alone, or before a timed lap where the order is by best lap)
    int rankedPosition() const { return m_rank.pos; }
    // The place and, in a race, the gap table measured from the player: read
    // once per update (and rebuild) by the decks that rank, not per row or key
    struct RankCache {
        int pos = -1;
        bool hasTable = false;
        StandingsGap::Table table;
    };
    RankCache m_rank;
    void refreshRank();
    bool ranks() const { return m_kind == Kind::Race || m_kind == Kind::Rival || m_kind == Kind::Standings; }
    void rivalGaps(int* ahead, int* behind, int* shown) const;
    int rivalLastLap() const;   // ms, -1 before their first

    // The quantized picture key update() compares (see REBUILDS above).
    static constexpr int KEY_SIZE = 14;
    void computeKey(int (&key)[KEY_SIZE]) const;
    void computeMoreKey(int (&key)[KEY_SIZE]) const;
    int m_key[KEY_SIZE] = {};

    // The riding decks' game state, stepped on each fresh telemetry sample.
    void stepCoaches(int64_t nowUs);
    void resetCoaches();
    // The racing and FMX decks' events (a place gained, a new best, a chain
    // landed) and the Combo deck's rolling score, every frame
    // the deck is shown: a handful of compares, nothing formatted.
    void stepEvents(int64_t nowUs);
    // What a deck forgets while not shown (a hidden deck, a carousel's deck
    // stepped away from), so nothing that happened meanwhile reads as news
    void forgetUnshown();

    // Drawing (deck_hud_draw.cpp), in design px relative to the panel.
    void drawDeck(const DeckLayout::Deck& deck);
    // One pass over the deck's elements: the row template's (rows) or the rest
    void drawPass(const DeckLayout::Deck& deck, bool rows);
    void drawMeter(const DeckLayout::MeterEl& el, const DeckMeter::Data& d);
    void drawKnob(const DeckLayout::MeterEl& el, const DeckMeter::Data& d);
    bool addArt(DeckLayout::Art art, int sprite, float x, float y, float w, float h, unsigned long fill);
    // A `caps` box: the theme's frame leaning over an upright rect, design px
    void addSlantFrame(const ThemeAsset& theme, float x, float y, float w, float h, unsigned long fill);
    float drawPills(const DeckLayout::Text& t, int font, float fs, unsigned long color, bool draw);
    float lx(float px) const;
    float ly(float py) const;
    void addCorners(const float* corners, unsigned long color, int sprite = PluginConstants::SpriteIndex::SOLID_COLOR);
    // A sheared part's slanted sides drawn smooth (deck_hud_draw.cpp)
    static constexpr unsigned SMOOTH_LEFT = 1u, SMOOTH_RIGHT = 2u;
    void addRectPx(float l, float t, float r, float b, unsigned long color, int sprite = PluginConstants::SpriteIndex::SOLID_COLOR,
                   float shear = 0.0f, unsigned smooth = SMOOTH_LEFT | SMOOTH_RIGHT);
    // An upright rect's rim, k px thick (a box's and a pill's border)
    void addRimPx(float l, float t, float r, float b, float k, unsigned long color);
    unsigned long resolve(const DeckLayout::Color& c, ColorSlot fallback) const;
    // A layout image as this rebuild's theme draws it (DeckAsset::themedImages).
    int themed(int sprite) const;
    unsigned long imageColor(int sprite, int drawn, unsigned long fill) const;
    const DeckAsset* m_drawLayout = nullptr;   // set by each rebuild, for themed(), getFont() and getColor()
    int m_drawTheme = -1;                    // its theme's index, -1 = none
    // activeLayout()'s memo: the layout and theme index resolved for one theme
    // generation and theme (the name lookups are string scans, and Speed and
    // Inputs rebuild near every frame). gen 0 = invalid; a Style change moves
    // s_slantedGen past it.
    struct LayoutMemo {
        unsigned int gen = 0;
        unsigned int slantedGen = 0;
        const ThemeAsset* theme = nullptr;
        const DeckAsset* layout = nullptr;
        int themeIndex = -1;
    };
    mutable LayoutMemo m_layoutMemo;
    const LayoutMemo& resolveLayout() const;
    unsigned long toneColor(uint8_t tone, const DeckLayout::Text* t, unsigned long normal) const;

    // A stamp: a word (or a +points) for STAMP_US after an event.
    enum class Stamp : uint8_t {
        None = 0, NewBest, FinalLap, PlacesUp, PlacesDown,
        Landed, Bailed,                     // Combo: a chain landed (value: its points) or lost
        AllTimePb, FastestLap, SessionPb,   // Timing: the record a lap set, the best of them
        BestSector,                         // Gap: a sector beat its best (value: its number)
        RivalGained, RivalLost,             // Rival: the gap's change at a split (value: ms)
        Reaction,                           // Speed: off the gate (value: ms, tone: its grade)
        SessionType, SessionState           // Session: the session, or its state, changed (shown: the new one)
    };
    void raiseStamp(Stamp s, int value = 0, uint8_t tone = DeckLayout::TONE_NORMAL);
    Stamp m_stamp = Stamp::None;
    int m_stampValue = 0;
    uint8_t m_stampTone = DeckLayout::TONE_NORMAL;
    int64_t m_stampUntilUs = 0;

    Kind m_kind;                         // const but for a carousel's steps
    const bool m_carousel = false;
    static bool s_slanted;               // setSlanted()
    static unsigned int s_slantedGen;    // bumped by each change of it (LayoutMemo)
    static bool s_bars;                  // setBars()
    static bool s_stamps;                // setStamps()
    static bool s_title;                 // setSharedTitle()
    static float s_opacity;              // setSharedOpacity()
    static float s_scale;                // setSharedScale()
    static void dirtyAllDecks();         // after a switch every deck reads

    // Session / rider the state below belongs to
    int m_sessionGen = -1;
    int m_raceNum = -1;

    // Speed: the gear's roll (Motion, digit_roll.h)
    DigitRoll::Roller m_gearRoll;
    // Standings: rows sliding to a new place (Motion on), as long as the
    // Standings HUD's do (row_slide.h)
    RowSlide m_placeSlide;
    int m_rollRaceNum = -1;

    // EACH DECK'S STATE in a struct of its own, every member at its reset
    // value: a reset is `m_x = X{}` (plus what a reset reads from the game),
    // so a member added later cannot be missed by it.

    // Race: the shown position (the +/- stamp), the progress held at its
    // highest (the lap count lands after the position at the line), and in a
    // timed race the laps done when the clock ran out (-1: still running)
    struct RaceState {
        int lastPos = -1;
        bool finalLapStamped = false;
        float high = 0.0f;
        int clockOutLaps = -1;
    };
    RaceState m_race;

    // Session: the session and its state last seen, so a change is news
    // (INT_MIN: none seen since shown, the first is taken silently)
    struct SessionSeen {
        int seenType = INT_MIN;
        int seenState = INT_MIN;
    };
    SessionSeen m_session;

    // Timing: when each record notice was last raised, so a new one is news;
    // the official time to the crossing held; the top speed through each
    // sector (the trap chip): this run's, the last's, the bests, and the
    // difference shown (in the shown unit; INT_MIN: no best to beat)
    static constexpr int MAX_SECTORS = 4;
    struct TimingState {
        std::chrono::steady_clock::time_point seenRecord[3] = {};
        int heldTime = -1;
        float trapRunMs = 0.0f, trapMs = 0.0f, trapBestMs[MAX_SECTORS] = {};
        int trapDiff = INT_MIN;
    };
    TimingState m_timing;

    // Wheelie / Slide / Combo
    DeckCoach::Wheelie m_wheelie;
    DeckCoach::Spell m_spell;   // Slide and Spin: the timer and MAX
    uint32_t m_seenWheelieBest = 0;
    // Combo: the chain end seen, the best combo a NEW BEST is measured
    // against, the score as it rolls up to the real one; the trick's
    // measurements (held as the FMX HUD holds them), and the chips: how many
    // of this chain's have been seen, when each rose in (by its place in the
    // chain, MAX_PILLS kept), whether one still slides (one more rebuild when
    // the last settles), and a trick turned with no chain behind it
    struct ComboState {
        bool seenChainEnd = false;
        int highScore = 0;
        int shownScore = 0;
        int64_t scoreStepUs = 0;
        Fmx::TrickStats trickStats;
        Fmx::ChainHold hold;   // the ended chain, held while down
        int chipSeen = 0;
        int64_t chipArriveUs[DeckLayout::MAX_PILLS] = {};
        bool chipsSliding = false;
        bool chipsAlone = false;
    };
    ComboState m_combo;
    // A chip's rise into the stack, 0 to 1 (eased; 1 at once with Motion off)
    float chipRise(int ordinal, int64_t nowUs) const;

    // ---- The later decks' state (deck_hud_more.cpp) ----
    // Crossings (Timing, Gap, Rival): the detector, the hold after one (the
    // General tab's freeze), the time to the last crossing this lap
    SplitCrossingDetector m_crossings;
    OfficialGapFreeze m_freeze;   // Timing's delta, Gap's hero: the shared rule
    int64_t m_holdUntilUs = 0;
    int m_crossMs = 0;            // the accumulated time to the last crossing this lap
    int m_crossSector = -1;       // which crossing that was (the line = NUM_SPLITS), -1 none
    // Gap: the last sector run, against PluginData's best of it; those bests
    // as they stood at the last crossing (the line's run is set against them)
    struct GapState {
        int lapBests[MAX_SECTORS] = { -1, -1, -1, -1 };
        int lastSector = -1, lastRun = -1, lastRunDelta = 0;
        bool lastRunHadBest = false;
    };
    GapState m_gap;
    // Speed: waiting on the gate, when it dropped, the reaction
    struct SpeedState {
        bool wasAwaitingDrop = false;
        int64_t dropUs = 0;
        float reaction = -1.0f;
    };
    SpeedState m_speed;
    // Spin: the shown share (-1 off the ground)
    float m_spinShown = -1.0f;
    // Inputs: drawing the shared history last frame (its show edge clears it)
    bool m_wasHistoryUser = false;
    // Not drawn last frame (hidden, or held back by a master switch or Hide
    // all): the first frame drawn again gathers afresh
    bool m_wasUnshown = true;
    // update() refreshed the rank this frame: its rebuild need not again
    bool m_rankFresh = false;
    // Standings: the layout's row count at the last rebuild (the key's window)
    int m_listRows = 0;
    // Rival: who and whether defending, the gap held at the last split (-1
    // none since the rival changed), and this frame's gaps to the riders
    // either side (stepRival; -1 none), which the key and gather read
    struct RivalState {
        int num = -1;
        bool defending = false;
        int heldGap = -1;
        int gapAhead = -1, gapBehind = -1;
    };
    RivalState m_rival;
    // Media: the controls (shared with the Now Playing widget and the Dash
    // page) and their icons
    struct MediaState {
        NowPlayingControls controls;
        NowPlayingIcons icons;
    };
    MediaState m_media;

    // Telemetry sample detection (stepCoaches): the last values seen, and when.
    float m_lastSample[5] = {};
    int64_t m_lastSampleUs = 0;

    // ---- The carousel (deck_hud_carousel.cpp); settings, then state ----
    friend class SettingsHud;
    friend class SettingsManager;
    uint32_t m_carDecks = CAROUSEL_DEFAULT;        // bit i: CAROUSEL_KINDS[i] in the turn
    int m_carAuto = 0;                         // into AUTO_STEPS_S (Off)
    Marker m_carMarker = Marker::Dots;
    uint32_t m_carOptions = CAR_NAMES | CAR_NOTICES;
    void resetCarousel();
    int m_carIndex = 0;                        // into CAROUSEL_KINDS
    bool m_carHold = false;                    // a notice holds it (Kind::Notice)
    int64_t m_carHoldUs = 0;                   // when the hold began
    int64_t m_carStepUs = 0;                   // the Auto clock's last step
    uint32_t m_carSeenNotice = 0;              // NoticeTracker's revision last taken over
    PageFlip m_flip;
    // Once a frame while shown: the members, a click on the title, a notice,
    // the Auto clock and the flip's redraws
    void followCarousel();
    void carouselShow(Kind next, bool named);
    void endCarouselHold(int64_t nowUs);
    // The decks in the turn (never none): those switched on, less Now Playing
    // while no player plays or pauses -- unless it is the only one on
    uint32_t carouselOn() const;
    bool m_carMediaLive = false;               // a player plays or pauses (NowPlaying::hasPlayer)
    bool m_carMediaOn = false;                 // MediaManager::isEnabled, as last read
    uint32_t m_carMediaRev = 0xFFFFFFFFu;      // MediaManager's revision last read
    // The title's run as last drawn (design px; l == r: none), what a click steps
    float m_carTitleL = 0.0f, m_carTitleR = 0.0f, m_carTitleT = 0.0f, m_carTitleB = 0.0f;
    // After a gather: the deck's name kept, and the count marker on the title
    void carouselTitle();
    Kind carouselMember() const { return CAROUSEL_KINDS[m_carIndex]; }
    // What the frame drawn shows apart from the deck's art (its readouts),
    // marked by the drawer (a carousel only), as index ranges of m_quads and
    // m_strings: what the name crossfades with, and what fades out on a step
    struct Range { uint16_t from, to; };
    static constexpr int MAX_RANGES = 48;
    Range m_contentQuads[MAX_RANGES] = {};
    Range m_contentStrings[MAX_RANGES] = {};
    int m_contentQuadCount = 0, m_contentStringCount = 0;
    void markContent(size_t quadFrom, size_t stringFrom);
    // The content of the deck stepped away from, as it was on screen, fading out
    std::vector<SPluginQuad_t> m_fromQuads;
    std::vector<SPluginString_t> m_fromStrings;
    std::vector<bool> m_fromSkipShadow;
    int m_nameString = -1;                     // the name's string in the frame, -1 none
    char m_carName[48] = {};                   // the deck's name, as its title says it
    // The flip on the frame the deck drew: its readouts at their share, the
    // name over them, the deck stepped away from fading out
    void finishCarousel(const DeckLayout::Deck& deck);
    // The title tab's marker: the dots' width (design px) beside the title
    // (0: none), and the dots drawn from x on the title's band; the count
    // ("2/5") is the title's own text
    float markerWidth(const DeckLayout::Text& t) const;
    // The widest of the carousel's names at fs (design px), so the dots after
    // the title stand still as the decks step
    float widestName(int font, float fs) const;
    void drawMarker(const DeckLayout::Text& t, float x, unsigned long on);
    int m_dotSprite = 0;
    unsigned int m_dotSpriteGen = 0xFFFFFFFFu;
    // Notice: the icon last looked up, by name and theme generation
    char m_noticeIconName[32] = {};
    int m_noticeIconSprite = 0;
    unsigned int m_noticeIconGen = 0xFFFFFFFFu;
};
