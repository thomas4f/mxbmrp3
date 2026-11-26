// ============================================================================
// hud/deck_hud.cpp
// The decks - see deck_hud.h. The deck's life here: defaults, the
// rebuild gate (computeKey), the riding decks' game state and the stamps.
// What each deck shows is deck_hud_gather.cpp; how the layout draws it is
// deck_hud_draw.cpp.
// ============================================================================
#include "deck_hud.h"
#include "deck_format.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "../core/asset_manager.h"
#include "../core/hud_manager.h"
#include "../core/i18n.h"
#include "gap_bar_hud.h"
#include "../core/plugin_constants.h"
#include "../core/motion.h"
#include "../core/plugin_data.h"
#include "../core/rider_stance.h"
#include "../core/ui_config.h"
#include "hud_defaults.h"
#include "notice_tracker.h"
#include "standings_hud.h"
#include "toast_feed.h"
#include "../game/game_config.h"
#if GAME_HAS_FMX
#include "../core/fmx_manager.h"
#endif

using namespace PluginConstants;

namespace {
constexpr float REF_W = 1920.0f, REF_H = 1080.0f;
// A fresh telemetry sample further apart than this (a pause, a load) steps
// the game state by this much, not by the gap.
constexpr int64_t MAX_STEP_US = 100000;
// The Combo deck's score rolls up to the real one over about this long.
constexpr int64_t SCORE_ROLL_US = 600000;

}  // namespace

using DeckFormat::quantize;

const char* DeckHud::harnessId(int slot) {
    static constexpr const char* kIds[] = {
        "speed_deck", "race_deck", "timing_deck", "wheelie_deck", "slide_deck", "combo_deck",
        "gap_deck", "rival_deck", "spin_deck", "inputs_deck", "standings_deck", "session_deck",
        "notice_deck", "fuel_deck", "media_deck", "laplog_deck", "carousel_deck"
    };
    static_assert(sizeof(kIds) / sizeof(kIds[0]) == static_cast<size_t>(SLOT_COUNT), "one id per slot");
    return slot >= 0 && slot < SLOT_COUNT ? kIds[slot] : "";
}

const char* DeckHud::sectionName(int slot) {
    static constexpr const char* kSections[] = {
        "SpeedDeck", "RaceDeck", "TimingDeck", "WheelieDeck", "SlideDeck", "ComboDeck",
        "GapDeck", "RivalDeck", "SpinDeck", "InputsDeck", "StandingsDeck", "SessionDeck",
        "NoticeDeck", "FuelDeck", "MediaDeck", "LapLogDeck", "CarouselDeck"
    };
    static_assert(sizeof(kSections) / sizeof(kSections[0]) == static_cast<size_t>(SLOT_COUNT), "one section per slot");
    return slot >= 0 && slot < SLOT_COUNT ? kSections[slot] : "";
}

const char* DeckHud::title(Kind kind) {
    switch (kind) {
        case Kind::Speed:     return TR("SPEED");
        case Kind::Race:      return TR("RACE");
        case Kind::Timing:    return TR("TIMING");
        case Kind::Wheelie:   return TR("WHEELIE");
        case Kind::Slide:     return TR("SLIDE");
        case Kind::Combo:     return TR("COMBO");
        case Kind::Gap:       return TR("GAP");
        case Kind::Rival:     return TR("RIVAL");
        case Kind::Spin:      return TR("SPIN");
        case Kind::Inputs:    return TR("INPUTS");
        case Kind::Standings: return TR("STANDINGS");
        case Kind::Session:   return TR("SESSION");
        case Kind::Fuel:      return TR("FUEL");
        case Kind::Media:     return TR("NOW PLAYING");
        case Kind::LapLog:    return TR("LAP LOG");
        case Kind::Notice:
        case Kind::COUNT:     break;
    }
    return "";
}

DeckHud::DeckHud(Kind kind) : m_kind(kind) {
    m_panelKind = PanelKind::Widget;
    setDraggable(true);
    m_quads.reserve(64);
    m_strings.reserve(16);
    clearRows();
    resetToDefaults();
    rebuildRenderData();
}

// The carousel: a deck drawing the first of its turn, with room kept for the
// content it fades out on a step (a deck's readouts: tens of quads, a dozen strings)
DeckHud::DeckHud(CarouselTag) : m_kind(CAROUSEL_KINDS[0]), m_carousel(true) {
    m_panelKind = PanelKind::Widget;
    setDraggable(true);
    m_quads.reserve(96);
    m_strings.reserve(32);
    m_fromQuads.reserve(64);
    m_fromStrings.reserve(16);
    m_fromSkipShadow.reserve(16);
    clearRows();
    resetToDefaults();
    rebuildRenderData();
}

// Design px are laid out on the DEFAULT grid (a cell 10.56 x 11.88 px at
// 1080p: the shipped layouts size every box in whole cells), so a px scales
// with the live cell: a changed font size or theme moves the decks with the
// lattice, and a deck's box stays whole cells -- its edges on the same grid
// lines as the classic HUDs'. Pinned by deck_grid_test.
float DeckHud::lx(float px) const {
    static const float defaultCellW = [] { LayoutMetrics m; m.derive(); return m.cellW; }();
    return px * (layout().cellW / defaultCellW) * m_fScale / REF_W;
}
float DeckHud::ly(float py) const {
    static const float defaultCellH = [] { LayoutMetrics m; m.derive(); return m.cellH; }();
    return py * (layout().cellH / defaultCellH) * m_fScale / REF_H;
}

bool DeckHud::s_slanted = false;   // the theme's Regular layout
unsigned int DeckHud::s_slantedGen = 1;

bool DeckHud::s_bars = true;
bool DeckHud::s_stamps = true;
bool DeckHud::s_title = true;      // the deck's name, in its own art
float DeckHud::s_opacity = 1.0f;
float DeckHud::s_scale = 1.0f;

void DeckHud::dirtyAllDecks() {
    HudManager& hm = HudManager::getInstance();
    for (int i = 0; i < HudManager::DECK_HUD_COUNT; ++i) {
        if (DeckHud* deck = hm.getDeckHud(i)) deck->setDataDirty();
    }
}

void DeckHud::setSlanted(bool slanted) {
    if (s_slanted == slanted) return;
    s_slanted = slanted;
    ++s_slantedGen;
    dirtyAllDecks();
}

void DeckHud::setBars(bool on) {
    if (s_bars == on) return;
    s_bars = on;
    dirtyAllDecks();
}

void DeckHud::setStamps(bool on) {
    if (s_stamps == on) return;
    s_stamps = on;
    dirtyAllDecks();
}

void DeckHud::setSharedTitle(bool on) {
    s_title = on;
    applyShared();
}

void DeckHud::setSharedOpacity(float opacity) {
    if (!std::isfinite(opacity)) return;   // persisted: never a NaN or an Inf
    s_opacity = std::round(std::clamp(opacity, 0.0f, 1.0f) * 100.0f) / 100.0f;
    applyShared();
}

void DeckHud::setSharedScale(float scale) {
    if (!std::isfinite(scale)) return;
    s_scale = std::round(std::clamp(scale, MIN_SCALE, MAX_SCALE) * 100.0f) / 100.0f;
    applyShared();
}

void DeckHud::applyShared() {
    HudManager& hm = HudManager::getInstance();
    for (int i = 0; i < HudManager::DECK_HUD_COUNT; ++i) {
        DeckHud* deck = hm.getDeckHud(i);
        if (!deck) continue;
        deck->setShowTitle(s_title);
        deck->setBackgroundOpacity(s_opacity);
        deck->setScale(s_scale);
        deck->clearThemeOverride();
    }
}

void DeckHud::warmFonts() {
    const AssetManager& assets = AssetManager::getInstance();
    HudManager& hm = HudManager::getInstance();
    for (int i = 0; i < HudManager::DECK_HUD_COUNT; ++i) {
        const DeckHud* deck = hm.getDeckHud(i);
        if (!deck || !deck->isVisibleAnySurface()) continue;
        for (int c = 0; c < static_cast<int>(FontCategory::COUNT); ++c)
            assets.digitInk(deck->getFont(static_cast<FontCategory>(c)));
    }
}

const DeckHud::LayoutMemo& DeckHud::resolveLayout() const {
    const AssetManager& assets = AssetManager::getInstance();
    const ThemeAsset* theme = activeTheme();   // memoised on the same generation
    const unsigned int gen = assets.themeGeneration();
    if (m_layoutMemo.gen == gen && m_layoutMemo.slantedGen == s_slantedGen && m_layoutMemo.theme == theme) return m_layoutMemo;
    m_layoutMemo.gen = gen;
    m_layoutMemo.slantedGen = s_slantedGen;
    m_layoutMemo.theme = theme;
    // The layout of the panel theme (a deck has no Theme of its own:
    // applyShared), so a theme switch restyles every deck.
    m_layoutMemo.layout = theme ? assets.getThemeDeck(*theme, s_slanted) : nullptr;
    // The theme's index, for its own pictures of the layout's images
    const std::vector<ThemeAsset>& themes = assets.getThemes();
    m_layoutMemo.themeIndex = theme && !themes.empty() && theme >= themes.data() && theme < themes.data() + themes.size()
        ? static_cast<int>(theme - themes.data()) : -1;
    return m_layoutMemo;
}

const DeckAsset* DeckHud::activeLayout() const {
    return resolveLayout().layout;
}

bool DeckHud::handlesDataType(DataChangeType dataType) const {
    if (dataType == DataChangeType::SpectateTarget || dataType == DataChangeType::SessionData) return true;
    switch (m_kind) {
    case Kind::Race:
    case Kind::Rival:
    case Kind::Standings: return dataType == DataChangeType::Standings || dataType == DataChangeType::RaceEntries;
    case Kind::Timing:
    case Kind::Gap:     return dataType == DataChangeType::LapLog || dataType == DataChangeType::IdealLap;
    case Kind::Session: return dataType == DataChangeType::RaceEntries;   // the server label reads the field size
    case Kind::LapLog:  return dataType == DataChangeType::LapLog || dataType == DataChangeType::IdealLap;
    default:          return false;   // telemetry-driven: computeKey() sees it change
    }
}

void DeckHud::resetToDefaults() {
    // THE STARTER SET is on, under the Decks switch, which ships off
    // (HudManager::m_groupOff): one click on the Toggles strip shows Timing and
    // Race in the top-left corner, Speed and Fuel along the bottom right, and
    // the Carousel at the top, centred, where the Dash opens (its notices take
    // it over there as they take the Dash).
    // Every deck has a slot of its own, laid out in game and copied here, so
    // any set switched on together opens clear of the rest. Pinned by
    // default_positions_test.
    m_bVisible = m_kind == Kind::Timing || m_kind == Kind::Race || m_kind == Kind::Speed ||
                 m_kind == Kind::Fuel;
    // The shared Title, Opacity and Scale (applyShared)
    m_bShowTitle = s_title;
    m_fBackgroundOpacity = s_opacity;
    setScale(s_scale);
    // Whole cells, as every deck box is (deck_grid_test)
    switch (m_kind) {
    case Kind::Timing:    setPosition(cellsX(1.0f), cellsY(1.0f)); break;
    case Kind::Race:      setPosition(cellsX(1.0f), cellsY(11.0f)); break;
    case Kind::Standings: setPosition(cellsX(1.0f), cellsY(22.0f)); break;
    case Kind::Gap:       setPosition(cellsX(1.0f), cellsY(55.0f)); break;
    case Kind::LapLog:    setPosition(cellsX(1.0f), cellsY(65.0f)); break;
    case Kind::Rival:     setPosition(cellsX(1.0f), cellsY(75.0f)); break;
    case Kind::Wheelie:   setPosition(cellsX(32.0f), cellsY(55.0f)); break;
    case Kind::Spin:      setPosition(cellsX(32.0f), cellsY(65.0f)); break;
    case Kind::Slide:     setPosition(cellsX(32.0f), cellsY(75.0f)); break;
    case Kind::Notice:    setPosition(cellsX(76.0f), cellsY(11.0f)); break;   // the Notices HUD's top, centred (Regular)
    case Kind::Session:   setPosition(cellsX(76.0f), cellsY(76.0f)); break;
    case Kind::Combo:     setPosition(cellsX(87.0f), cellsY(66.0f)); break;
    case Kind::Inputs:    setPosition(cellsX(107.0f), cellsY(76.0f)); break;
    case Kind::Media:     setPosition(cellsX(118.0f), cellsY(66.0f)); break;
    case Kind::Speed:     setPosition(cellsX(138.0f), cellsY(76.0f)); break;
    case Kind::Fuel:      setPosition(cellsX(159.0f), cellsY(76.0f)); break;
    case Kind::COUNT:   break;
    }
    if (m_carousel) {
        m_bVisible = true;
        setPosition(cellsX(76.0f), cellsY(1.0f));   // above the Notice deck
        resetCarousel();
    }
    setDataDirty();
}

void DeckHud::resetCoaches() {
    m_wheelie.reset();
    m_spell.reset();
    m_seenWheelieBest = 0;
    m_race = RaceState{};
    m_combo = ComboState{};
    m_timing = TimingState{};
    {
        // A record already noticed is not this deck's news
        const PluginData& pd = PluginData::getInstance();
        m_timing.seenRecord[0] = pd.getAllTimePBTime();
        m_timing.seenRecord[1] = pd.getFastestLapTime();
        m_timing.seenRecord[2] = pd.getSessionPBTime();
    }
    m_stamp = Stamp::None;
    m_stampUntilUs = 0;
    m_gearRoll.reset();
#if GAME_HAS_FMX
    const FmxManager& fmx = FmxManager::getInstance();
    m_combo.seenChainEnd = fmx.getChainEndAnimation().active;
    m_combo.highScore = fmx.getScore().bestComboScore;
    m_combo.shownScore = fmx.getScore().sessionScore;   // a score already there shows, it does not roll
#endif
    resetMore();
}

float DeckHud::chipRise(int ordinal, int64_t nowUs) const {
    if (UiConfig::getInstance().getMotion() == Motion::Level::OFF) return 1.0f;
    const float ms = RowSlide::DURATION_MS;
    const int64_t at = m_combo.chipArriveUs[ordinal % DeckLayout::MAX_PILLS];
    const float t = std::clamp(static_cast<float>(nowUs - at) / (ms * 1000.0f), 0.0f, 1.0f);
    return RowSlide::easeOut(t);   // as the Standings rows slide
}

void DeckHud::raiseStamp(Stamp s, int value, uint8_t tone) {
    m_stamp = s;
    m_stampValue = value;
    m_stampTone = tone;
    m_stampUntilUs = DigitRoll::nowUs() + STAMP_US;
    setDataDirty();
}

// The riding decks advance on each FRESH telemetry sample (100 Hz), by the
// time since the last one: a frame that brings no new sample (a high frame
// rate, a paused game) advances nothing, so a paused wheelie does not keep
// counting.
void DeckHud::stepCoaches(int64_t nowUs) {
    const PluginData& pd = PluginData::getInstance();
    const BikeTelemetryData& bike = pd.getBikeTelemetry();
    const bool own = pd.getDisplayRaceNum() == pd.getPlayerRaceNum();
    if (!bike.isValid || !own) {
        m_lastSampleUs = 0;
        return;
    }
    const float sample[5] = { bike.speedometer, bike.pitch, bike.roll, bike.slideAngle, bike.rearWheelSpeed };
    if (std::memcmp(sample, m_lastSample, sizeof(sample)) == 0) return;
    std::memcpy(m_lastSample, sample, sizeof(sample));
    const int64_t gapUs = m_lastSampleUs > 0 ? std::min(nowUs - m_lastSampleUs, MAX_STEP_US) : 0;
    m_lastSampleUs = nowUs;
    if (gapUs <= 0) return;
    const float dt = static_cast<float>(gapUs) / 1e6f;

    const RiderTrackState* track = pd.getPlayerTrackPosition();
    const bool crashed = track && track->crashed;
    if (isLaterKind(m_kind)) {
        stepMoreTelemetry(dt, crashed);
    } else if (m_kind == Kind::Wheelie) {
        // A crash ends a hold as the front wheel touching would
        m_wheelie.step(crashed ? 0.0f : bike.wheelieAngle, crashed || bike.frontWheelDown, bike.pitch, dt);
        if (m_wheelie.bestSeq != m_seenWheelieBest) {
            m_seenWheelieBest = m_wheelie.bestSeq;
            raiseStamp(Stamp::NewBest);
        }
    } else if (m_kind == Kind::Slide) {
        // A slide is the bike sideways past grip; a crash ends it
        m_spell.step(!crashed && DeckCoach::slideBand(bike.slideAngle) != DeckCoach::SlideBand::GRIP, dt);
    } else if (m_kind == Kind::Timing) {
        // The top speed through the sector being run (the trap chip)
        if (std::isfinite(bike.speedometer)) m_timing.trapRunMs = std::max(m_timing.trapRunMs, bike.speedometer);
    }
}

void DeckHud::stepEvents(int64_t nowUs) {
    const PluginData& pd = PluginData::getInstance();
    switch (m_kind) {
    case Kind::Speed: {
        // Off the gate: the time from the drop to the bike moving, as a stamp
        detectDrop(nowUs);
        const BikeTelemetryData& bike = pd.getBikeTelemetry();
        const bool own = pd.getDisplayRaceNum() == pd.getPlayerRaceNum();
        if (m_speed.dropUs > 0 && m_speed.reaction < 0.0f && own && bike.isValid && bike.speedometer > DeckScore::REACT_SPEED) {
            m_speed.reaction = static_cast<float>(nowUs - m_speed.dropUs) / 1e6f;
            const DeckScore::Reaction g = DeckScore::reactionGrade(m_speed.reaction);
            raiseStamp(Stamp::Reaction, static_cast<int>(std::lround(m_speed.reaction * 1000.0f)),
                       g == DeckScore::Reaction::GREAT ? DeckLayout::TONE_GOOD
                       : g == DeckScore::Reaction::LATE ? DeckLayout::TONE_BAD : DeckLayout::TONE_NORMAL);
            m_stampUntilUs = DigitRoll::nowUs() + REACT_US;
        }
        break;
    }
    case Kind::Race: {
        // A place won or lost in any ranked session (rankedPosition: none
        // until there is something to rank); FINAL LAP in a race
        const SessionData& sd = pd.getSessionData();
        const int raceNum = pd.getDisplayRaceNum();
        const bool running = (sd.sessionState & SessionState::IN_PROGRESS) != 0;
        stampPlaceChange(running);
        const StandingsData* mine = raceNum > 0 ? pd.getStanding(raceNum) : nullptr;
        if (!pd.isRaceSession() || !mine) break;
        if (running && !m_race.finalLapStamped && sd.isRiderOnLastLap(mine->numLaps, mine->numLapsAtLeaderFinish)) {
            m_race.finalLapStamped = true;
            raiseStamp(Stamp::FinalLap);
        }
        // A timed race's clock running out: from here the laps left count
        if (sd.sessionLength > 0 && m_race.clockOutLaps < 0 && running && pd.getLeaderLapsToGo() >= 0)
            m_race.clockOutLaps = mine->numLaps;
        break;
    }
    case Kind::Session: {
        // The session or its state changing: the new one as a stamp (the
        // FORMAT row carries only the length). A new session wins over its
        // state, which changes with it.
        const SessionData& sd = pd.getSessionData();
        const int type = sd.eventType * 1000 + sd.session;
        const bool seen = m_session.seenType != INT_MIN;
        if (seen && type != m_session.seenType && sd.session >= 0) raiseStamp(Stamp::SessionType);
        else if (seen && sd.sessionState != m_session.seenState) raiseStamp(Stamp::SessionState);
        m_session.seenType = type;
        m_session.seenState = sd.sessionState;
        break;
    }
    case Kind::Timing: {
        // The record a lap set (the notices' own ladder: one per lap, the best)
        const std::chrono::steady_clock::time_point at[3] = { pd.getAllTimePBTime(), pd.getFastestLapTime(), pd.getSessionPBTime() };
        const Stamp record[3] = { Stamp::AllTimePb, Stamp::FastestLap, Stamp::SessionPb };
        for (int i = 0; i < 3; ++i) {
            if (at[i] == m_timing.seenRecord[i]) continue;
            m_timing.seenRecord[i] = at[i];
            // An all-time PB with nothing before it is a first lap, not a record
            if (i == 0 && !pd.getAllTimePBLap().hasGain()) continue;
            raiseStamp(record[i]);
        }
        stepCrossings(nowUs);
        break;
    }
    case Kind::Combo: {
#if GAME_HAS_FMX
        const FmxManager& fmx = FmxManager::getInstance();
        const Fmx::FmxScore& sc = fmx.getScore();
        const FmxManager::ChainEndAnimation& end = fmx.getChainEndAnimation();
        if (end.active && !m_combo.seenChainEnd) {
            if (end.success) raiseStamp(Stamp::Landed, end.bankedScore);
            else raiseStamp(Stamp::Bailed);
        }
        m_combo.seenChainEnd = end.active;
        if (sc.bestComboScore > m_combo.highScore) {
            if (m_combo.highScore > 0) raiseStamp(Stamp::NewBest);
            m_combo.highScore = sc.bestComboScore;
        }
        // The score rolls up to the real one, easing in over SCORE_ROLL_US
        const int target = sc.sessionScore;
        if (target < m_combo.shownScore) {
            m_combo.shownScore = target;   // a reset: no roll down
        } else if (target > m_combo.shownScore) {
            const int64_t dt = m_combo.scoreStepUs > 0 ? std::clamp<int64_t>(nowUs - m_combo.scoreStepUs, 0, SCORE_ROLL_US) : 0;
            const float share = std::min(1.0f, 4.0f * static_cast<float>(dt) / static_cast<float>(SCORE_ROLL_US));
            m_combo.shownScore += std::max(1, static_cast<int>(static_cast<float>(target - m_combo.shownScore) * share));
            m_combo.shownScore = std::min(m_combo.shownScore, target);
        }
        m_combo.scoreStepUs = nowUs;

        // The trick's measurements and the ended chain, as the FMX HUD holds them
        const RiderTrackState* own = pd.getPlayerTrackPosition();
        m_combo.trickStats.update(fmx, own && own->crashed);
        m_combo.hold.update(fmx, own && own->crashed);

        // The chips: each new one rises in from now. A new chain (a trick
        // turning with no chain behind it, from the frame it starts) counts
        // from zero again; a chain ending, or losing its turning trick, keeps
        // the chips it has where they are.
        const bool turning = Fmx::isTurning(fmx.getActiveTrick());
        const std::vector<Fmx::TrickInstance>& chain = fmx.getChainTricks();
        const bool ended = m_combo.hold.held && !turning && chain.empty();
        const int count = ended ? m_combo.hold.trickCount
                                : static_cast<int>(chain.size()) + (turning ? 1 : 0);
        const bool alone = chain.empty() && turning;
        if (alone && !m_combo.chipsAlone) m_combo.chipSeen = 0;
        else if (count < m_combo.chipSeen) m_combo.chipSeen = count;
        m_combo.chipsAlone = alone;
        for (; m_combo.chipSeen < count; ++m_combo.chipSeen) m_combo.chipArriveUs[m_combo.chipSeen % DeckLayout::MAX_PILLS] = nowUs;
#endif
        break;
    }
    default:
        if (isLaterKind(m_kind)) stepMoreEvents(nowUs);
        break;
    }
}

void DeckHud::forgetUnshown() {
    m_lastSampleUs = 0;
    m_wasHistoryUser = false;
    // A split crossed while hidden is not news once shown: adopt then
    m_crossings.reset();
    m_freeze.reset();
    // Rows snap into place when shown again, never slide through what
    // changed while hidden
    m_placeSlide.forget();
    // A session change while hidden is not news once shown
    m_session = SessionSeen{};
}

void DeckHud::stampPlaceChange(bool running) {
    const int pos = rankedPosition();
    if (pos > 0 && m_race.lastPos > 0 && pos != m_race.lastPos && running)
        raiseStamp(pos < m_race.lastPos ? Stamp::PlacesUp : Stamp::PlacesDown, std::abs(pos - m_race.lastPos));
    m_race.lastPos = pos;
}

void DeckHud::update() {
    // Shown or not: a drag must end with the press, never on the next show
    if (m_kind == Kind::Media) followMedia();
    // Hidden, or held back (the Decks switch off, Hide all): no deck work at
    // all, as nothing it gathers reaches the screen
    if (!isVisibleAnySurface() || isHeldBack()) {
        m_wasUnshown = true;
        forgetUnshown();
        // A carousel shown again opens on its deck, nothing held, its Auto
        // clock starting then
        if (m_carousel) {
            if (m_carHold) {
                m_carHold = false;
                carouselShow(carouselMember(), false);
            }
            m_flip.stopFade();
            m_flip.clearName();
            m_carStepUs = 0;
        }
        clearDataDirty();
        clearLayoutDirty();
        return;
    }
    if (m_wasUnshown) {
        m_wasUnshown = false;
        setDataDirty();
    }
    const PluginData& pd = PluginData::getInstance();
    const int gen = pd.getSessionData().sessionGeneration;
    const int raceNum = pd.getDisplayRaceNum();
    if (gen != m_sessionGen || raceNum != m_raceNum) {
        m_sessionGen = gen;
        m_raceNum = raceNum;
        resetCoaches();
        setDataDirty();
    }
    m_rankFresh = ranks();   // the rebuild below reads this frame's
    if (m_rankFresh) refreshRank();
    const int64_t now = DigitRoll::nowUs();
    // The riding decks step on telemetry, the others on events; the decks
    // with a riding side (Timing's trap, Spin) on both.
    const bool telemetry = m_kind == Kind::Speed || m_kind == Kind::Wheelie || m_kind == Kind::Slide ||
                           m_kind == Kind::Timing || m_kind == Kind::Spin;
    if (telemetry) stepCoaches(now);
    stepEvents(now);

    // Stamps expire on their own
    if (m_stamp != Stamp::None && now >= m_stampUntilUs) {
        m_stamp = Stamp::None;
        setDataDirty();
    }
    // Animation alone lays the gathered fields out again, never re-gathers.
    // A rolling gear moves every frame until it settles
    if (m_kind == Kind::Speed && m_gearRoll.active(now)) setLayoutDirty();
    // So do Standings rows sliding to a new place, and once more when the
    // last has arrived (it drops the cut the slide was drawn with)
    if (m_kind == Kind::Standings && m_placeSlide.active()) {
        m_placeSlide.prune(now);
        setLayoutDirty();
    }

    // And Combo's chips rising into their stack, and once more when settled
    if (m_kind == Kind::Combo) {
        bool sliding = false;
        for (int o = std::max(0, m_combo.chipSeen - DeckLayout::MAX_PILLS); o < m_combo.chipSeen && !sliding; ++o)
            sliding = chipRise(o, now) < 1.0f;
        if (sliding || m_combo.chipsSliding) setLayoutDirty();
        m_combo.chipsSliding = sliding;
    }

    if (m_kind == Kind::Notice) clickNotice();
    if (m_carousel) followCarousel();

    int key[KEY_SIZE] = {};
    computeKey(key);
    if (std::memcmp(key, m_key, sizeof(key)) != 0) {
        std::memcpy(m_key, key, sizeof(key));
        setDataDirty();
    }
    processDirtyFlags();
    m_rankFresh = false;
}

// What the deck shows, as a few integers at the resolution it is SHOWN at:
// a change below that resolution must not rebuild.
void DeckHud::computeKey(int (&key)[KEY_SIZE]) const {
    const PluginData& pd = PluginData::getInstance();
    const BikeTelemetryData& bike = pd.getBikeTelemetry();
    const SessionData& sd = pd.getSessionData();
    const int raceNum = pd.getDisplayRaceNum();
    switch (m_kind) {
    case Kind::Speed: {
        const int top = sd.rpmRangeTop();
        key[0] = bike.isValid ? 1 : 0;
        key[1] = bike.gear;
        key[2] = quantize(bike.speedometer * UnitConversion::MS_TO_KMH, 1.0f);   // finer than either unit shows
        // The meter's 64th, and the gear's hot colour at the shift point
        key[3] = (top > 0 ? bike.rpm * 64 / top : 0) * 2 + (sd.shiftRPM > 0 && bike.rpm >= sd.shiftRPM ? 1 : 0);
        key[4] = RiderStance::gameSendsIt() ? bike.riderStance : -2;
        key[5] = raceNum;
        break;
    }
    case Kind::Race: {
        const StandingsData* s = raceNum > 0 ? pd.getStanding(raceNum) : nullptr;
        key[0] = rankedPosition();
        key[1] = static_cast<int>(runningOrder().size());
        key[2] = s ? s->numLaps : -1;
        key[3] = sd.session * 64 + sd.eventType;
        key[4] = m_race.clockOutLaps;
        key[5] = sd.sessionLength > 0 ? pd.getSessionClockTime() / 1000 : pd.getSessionElapsedTime() / 1000;
        const RiderTrackState* t = pd.getPlayerTrackPosition();
        key[6] = t ? quantize(t->trackPos, 0.01f) : -1;
        key[7] = pd.getLeaderLapsToGo();
        key[8] = static_cast<int>(sd.sessionState);
        break;
    }
    case Kind::Timing: {
        const int64_t now = DigitRoll::nowUs();
        const int elapsed = pd.isLapTimerValid() ? pd.getElapsedLapTime() : -1;
        key[0] = held(now) ? -2 - m_timing.heldTime : (elapsed >= 0 ? elapsed / 100 : -1);
        const IdealLapData* ideal = pd.getIdealLapData();
        key[1] = ideal ? ideal->lastLapTime : -1;
        const LapLogEntry* best = pd.getBestLapEntry();
        key[2] = best ? best->lapTime : -1;
        int gap = 0;
        key[3] = m_freeze.shownGap(freezeRef(), &gap) ? gap / 10 : INT32_MIN;
        const CurrentLapData* cur = pd.getCurrentLapData();
        key[4] = cur ? cur->split1 : -1;
        key[5] = cur ? cur->split2 : -1;
        key[6] = cur ? cur->split3 : -1;
        const RiderTrackState* t = pd.getPlayerTrackPosition();
        key[7] = t ? quantize(t->trackPos, 0.01f) : -1;
        float splits[SplitLearner::SLOTS];
        const int n = pd.collectSplitPositions(splits);
        key[8] = n;
        for (int i = 0; i < n; ++i) key[8] = key[8] * 31 + quantize(splits[i], 0.001f);
        break;
    }
    case Kind::Wheelie:
        key[0] = quantize(bike.wheelieAngle, 1.0f);
        key[1] = quantize(m_wheelie.hold, 0.1f);
        key[2] = quantize(m_wheelie.best, 0.1f);
        key[3] = quantize(m_wheelie.zoneLo(), 1.0f);
        key[4] = quantize(m_wheelie.zoneHi(), 1.0f);
        key[5] = m_wheelie.active ? 1 : 0;
        key[6] = bike.isValid && raceNum == pd.getPlayerRaceNum() ? 1 : 0;
        break;
    case Kind::Slide:
        key[0] = quantize(bike.slideAngle, 1.0f);
        key[1] = quantize(m_spell.t, 0.1f);
        key[2] = quantize(m_spell.max, 0.1f);
        key[3] = m_spell.active ? 1 : 0;
        key[4] = bike.isValid && raceNum == pd.getPlayerRaceNum() ? 1 : 0;
        break;
    case Kind::Combo: {
#if GAME_HAS_FMX
        const FmxManager& fmx = FmxManager::getInstance();
        const Fmx::TrickInstance& t = fmx.getActiveTrick();
        const Fmx::FmxScore& sc = fmx.getScore();
        key[0] = static_cast<int>(t.type);
        key[1] = static_cast<int>(t.state);
        key[2] = quantize(t.progress, 0.02f);
        key[3] = sc.chainCount;
        const bool turning = t.state == Fmx::TrickState::ACTIVE || t.state == Fmx::TrickState::GRACE;
        key[4] = sc.chainScore + (turning ? sc.currentTrickScore : 0);
        key[5] = sc.sessionScore;
        key[6] = quantize(sc.chainElapsed, 0.05f);
        key[8] = m_combo.shownScore;
        // The chain running out blinks: each half-beat redraws it (the
        // last slot is the stamp's, so the blink shares the end's). Not
        // while a trick turns past its threshold: the chain's clock
        // waits and the meter stands full.
        const bool committed = Fmx::isTurning(t);
        const float period = fmx.getConfig().chainPeriod;
        const bool low = !committed && sc.chainCount > 0 && period > 0.0f &&
                         1.0f - sc.chainElapsed / period < CHAIN_LOW;
        const int blink = low ? (chainBlinkOn(DigitRoll::nowUs()) ? 1 : 2) : 0;
        key[7] = (m_combo.hold.held ? 1 : 0) + blink * 2;
        key[9] = quantize(m_combo.trickStats.duration, 0.1f);
        key[10] = quantize(m_combo.trickStats.distance, 1.0f);
        key[11] = quantize(m_combo.trickStats.height, 1.0f);
        key[12] = quantize(m_combo.trickStats.rotation, 1.0f);
#endif
        break;
    }
    case Kind::Notice: {
        // What is up, by the revisions of its two feeds, which of them the
        // deck shows, and how many wait
        key[0] = static_cast<int>(NoticeTracker::getInstance().revision());
        key[1] = static_cast<int>(ToastFeed::getInstance().revision());
        key[2] = ToastFeed::getInstance().waiting();
        key[3] = static_cast<int>(noticeUp());
        break;
    }
    case Kind::COUNT:
        break;
    default:
        computeMoreKey(key);
        break;
    }
    key[KEY_SIZE - 1] = static_cast<int>(m_stamp);
}

void DeckHud::rebuildRenderData() {
    if (ranks() && !m_rankFresh) refreshRank();   // a rebuild from outside update() reads a fresh place too
    const LayoutMemo& resolved = resolveLayout();
    // The layout this rebuild draws in, for its images and fonts (the Notice
    // deck fits its detail to the font in the gather)
    m_drawLayout = resolved.layout;
    m_drawTheme = resolved.themeIndex;
    const DeckLayout::Deck* deck = m_drawLayout ? &m_drawLayout->geometry.decks[static_cast<int>(m_kind)] : nullptr;
    const bool drawable = deck && deck->width > 0.0f && deck->height > 0.0f;

    clearRows();
    m_drawsNothing = false;
    switch (m_kind) {
    case Kind::Speed:   gatherSpeed(); break;
    case Kind::Race:    gatherRace(); break;
    case Kind::Timing:  gatherTiming(); break;
    case Kind::Wheelie: gatherWheelie(); break;
    case Kind::Slide:   gatherSlide(); break;
    case Kind::Combo:   gatherCombo(); break;
    case Kind::Gap:     gatherGap(); break;
    case Kind::Rival:   gatherRival(); break;
    case Kind::Spin:    gatherSpin(); break;
    case Kind::Inputs:  gatherInputs(); break;
    case Kind::Standings: gatherStandings(drawable ? deck->rows : 0); break;
    case Kind::LapLog:  gatherLapLog(drawable ? deck->rows : 0); break;
    case Kind::Session: gatherSession(); break;
    case Kind::Fuel:    gatherFuel(); break;
    case Kind::Media:   gatherMedia(drawable ? deck : nullptr); break;
    case Kind::Notice:
        // Nothing up: nothing drawn, and no bounds to take a click
        m_drawsNothing = !gatherNotice(drawable ? deck : nullptr);
        break;
    case Kind::COUNT:   break;
    }
    if (m_carousel) carouselTitle();
    // Title off: no name, so the box that hugs it goes too.
    if (!m_bShowTitle) m_rows.text[DeckLayout::TITLE][0] = '\0';
    gatherStamp();
    // Bars / Stamps off (setBars, setStamps): no bar, so its well goes too;
    // no stamp, so the box hugging it goes too
    if (!s_bars) {
        for (int m = 0; m < DeckLayout::METER_COUNT; ++m)
            if (DeckLayout::isBar(static_cast<DeckLayout::Meter>(m))) m_rows.meter[m].show = false;
    }
    if (!s_stamps) m_rows.text[DeckLayout::STAMP][0] = '\0';
    stepAnimations();
    drawGathered();
}

void DeckHud::rebuildLayout() {
    stepAnimations();
    drawGathered();
    finalizeThemedFill();
}

void DeckHud::stepAnimations() {
    if (m_kind == Kind::Standings) {
        const bool slide = UiConfig::getInstance().getMotion() != Motion::Level::OFF;
        const int64_t now = DigitRoll::nowUs();
        for (int r = 0; r < m_rows.rowCount; ++r)
            m_rows.rowShift[r] = slide ? m_placeSlide.offset(m_rows.rowNum[r], now) : 0.0f;
    }
    if (m_kind == Kind::Combo && m_rows.pillCount > 0) {
        // Each stands one pill above every newer one, as far as they have
        // risen in: the newest rises from a pill below its place to it
        const int64_t now = DigitRoll::nowUs();
        float above = 0.0f;
        for (int k = m_rows.pillCount - 1; k >= 0; --k) {
            const float rise = chipRise(m_rows.pillFirst + k, now);
            m_rows.pillLift[k] = above + rise - 1.0f;
            above += rise;
        }
    }
}

void DeckHud::drawGathered() {
    m_quads.clear();
    clearStrings();
    invalidatePanelRect();   // the layout draws its own panel (art = panel)
    const DeckLayout::Deck* deck = m_drawLayout ? &m_drawLayout->geometry.decks[static_cast<int>(m_kind)] : nullptr;
    const bool drawable = deck && deck->width > 0.0f && deck->height > 0.0f;
    if (m_drawsNothing) {
        setBounds(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }
    // No layout installed: a small empty panel, still movable, nothing drawn.
    setBounds(0.0f, 0.0f, lx(drawable ? deck->width : 64.0f), ly(drawable ? deck->height : 32.0f));
    if (!drawable) return;
    // Each title-art box is a card cover the panel's fill is cut around
    // (finalizeThemedFill); a pool of one strip overflows on the first, and the
    // fill stacks under it. A row box counts once a row.
    int covers = 0;
    for (int i = 0; i < deck->boxCount; ++i) {
        const DeckLayout::Box& b = deck->boxes[i];
        if (b.present && b.art == DeckLayout::ART_TITLE) covers += b.row ? m_rows.rowCount : 1;
    }
    m_fillReserve = covers > 0 ? 5 * (covers + 1) : 0;
    m_contentQuadCount = m_contentStringCount = 0;
    m_carTitleL = m_carTitleR = 0.0f;
    drawDeck(*deck);
    if (m_carousel) finishCarousel(*deck);
}

void DeckHud::clearRows() {
    for (int i = 0; i < DeckLayout::FIELD_COUNT; ++i) {
        m_rows.text[i][0] = '\0';
        m_rows.tone[i] = DeckLayout::TONE_NORMAL;
    }
    m_rows.pillCount = 0;
    for (DeckMeter::Data& m : m_rows.meter) m = DeckMeter::Data{};
    m_rows.rowCount = 0;
    m_rows.rowBreakAt = -1;
    for (float& shift : m_rows.rowShift) shift = 0.0f;
    for (unsigned long& ink : m_rows.ink) ink = 0;
    m_rows.icon = 0;
    m_rows.iconInk = 0;
    for (int& sprite : m_rows.controlSprite) sprite = 0;
    m_rows.glow = 0.0f;
    m_rows.glowColor = 0;
}

// Only the fields DeckLayout::kDeckFields gives this deck: the table a layout
// author reads, so a field set here and missing there shows nowhere.
void DeckHud::setText(DeckLayout::Field f, const char* s, uint8_t tone) {
    if (!DeckLayout::deckFills(static_cast<int>(m_kind), f) || !s) return;
    snprintf(m_rows.text[f], sizeof(m_rows.text[f]), "%s", s);
    m_rows.tone[f] = tone;
}
