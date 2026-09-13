// ============================================================================
// core/achievements.h
// The achievement CATALOGUE and the pure tier arithmetic over it. Header-only
// and dependency-free (no PluginData, no StatsManager, no game API), so the
// unit suite compiles the real table and pins every rule below
// (tests/unit/test_achievements.cpp) in about a second.
//
// WHAT AN ACHIEVEMENT IS HERE. One lifetime METRIC the plugin already tracks
// (distance, races, crashes, ...) with FOUR ascending thresholds. Reaching a
// threshold earns that TIER (Bronze, Silver, Gold, Platinum); the tab shows the
// metric as progress toward the next one, so the same row is both the lifetime
// stat it replaced and the achievement it feeds. A "flat" achievement is simply
// one whose four thresholds are the same number; none ships that way today,
// because a single bar that stays full forever says less than four steps.
//
// NEGATIVE metrics are achievements on purpose. Crashes, penalties and penalty
// time are counted the same way as wins: some players find "Skill Issue"
// funnier than "Winner", and a stat that is only ever shown as a shortfall is a
// stat nobody looks at.
//
// THE TABLE IS THE CONTRACT. `id` is what the stats file stores (by NAME, never
// by index -- an index reassigns everyone's progress when a row is inserted),
// and `metric` is what AchievementManager evaluates. Adding an achievement is
// one row here plus one case in AchievementManager::metricValue(); the unit
// test walks the table, so a row with a misordered threshold, a duplicate id,
// or a description with no %s fails there rather than in a player's toast.
//
// HOW A ROW IS WRITTEN for a human -- the unit formatter and the two sentence
// builders -- lives in achievement_text.h, split out when this file passed its
// budget. Only the settings tab and the toast need it.
//
// WHY THRESHOLDS ARE IN DISPLAY UNITS (km, hours, seconds) rather than the
// metres/ms the stats keep: the same number is read by the description ("Ride
// 1,000 km"), the progress text ("340 / 1,000 km") and the evaluation, and a
// conversion at each of those sites is three places for one slip. The manager
// converts ONCE, at the metric read.
// ============================================================================
#pragma once

#include "exploration_signals.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace Achievements {

// Which lifetime number feeds an entry. The manager maps each to a
// StatsManager read (or, for ConfigReloads, its own counter).
enum class Metric : uint8_t {
    DistanceKm = 0,
    RideTimeHours,
    Laps,
    Races,
    Wins,
    Podiums,
    FastestLaps,
    CleanRaces,
    Crashes,
    Penalties,
    PenaltyTimeSec,
    GearShifts,
    Tracks,
    Bikes,
    PersonalBests,
    Breakout,
    ConfigReloads,
    RainRaces,
    BigGridRaces,
    MaxSessionLaps,
    // FMX (GAME_HAS_FMX): lifetime trick stats StatsManager keeps for these.
    FmxTricks,
    FmxScore,
    FmxBackflips,
    FmxWheelieSec,
    FmxWheelieKm,
    FmxChainScore,
    FmxKinds,
    PenaltyFreeStreak,
    PbLeaps,
    FmxFrontflips,
    MaxTrackLaps,     // the most laps at any one track, every bike counted
    MaxBikeKm,        // the longest odometer of any one bike
    BikeClasses,      // distinct classes (categories) among the bikes ridden
    FmxWhips,         // the named air tricks, one lifetime count each
    FmxScrubs,
    FmxOppos,
    FmxTurnDowns,
    FmxEndoSec,       // the longest endo or stoppie HELD, not how many
    // Read from ExplorationStats by the row's `signal` (exploration_signals.h):
    // the one metric many rows share, each naming its own signal.
    Exploration,
    // The share of LISTED rows standing at each metal or better, 0-100, one
    // metric per metal (computed, never stored; see completion_floor.h). They
    // replace a single row that reported the lowest tier reached anywhere,
    // which is a floor - it sat at zero until the very last row moved, so it
    // could not say how far along anyone was.
    CompletionBronze,
    CompletionSilver,
    CompletionGold,
    CompletionPlatinum,
    COUNT
};

// The page an entry sits on in the settings tab: one group per page (or per
// six-row chunk of a group), so related rows are read together.
//
// THE ORDER HERE IS THE PAGE ORDER, in the tab and in the generated docs - both
// walk 0..COUNT. Riding first; then Completion, which reports on exactly the
// pages above it; then the pages outside those figures, Hidden last (an empty
// page until something is stumbled on).
enum class Group : uint8_t {
    Mileage = 0,
    Racing,
    // STREAKS, every one of them: races finished upright, races in a row
    // penalty-free, laps in a row clean, laps within a tenth of each other,
    // half an hour without a spill. It was called Conduct, which only ever fit
    // the penalty row -- staying upright is not conduct -- and "Skill" was
    // considered and dropped: Racing, Air and Tricks are skill too, so it named
    // nothing, and it sat oddly beside the Skill Issue row on Misfortune.
    Consistency,
    // BREADTH, not progress: five of its rows are "different X" counts -- tracks
    // ridden, bikes, classes, servers, days turned up on. It was called Progress,
    // which collided twice: the tab's summary block is a section headed Progress
    // (the Unlocked bar), and Completion is the page that measures progress.
    Variety,
    // JUMPING, which is not freestyle: none of these needs a trick, and the
    // flight detector measures them all whether you whipped it or rode it
    // straight. Three measurements (time, distance, height) read two ways
    // (a lifetime total, and the best single jump) - six rows, and the shape
    // is what decides the landing rule: a TOTAL takes the flight at touchdown,
    // a BEST has to be ridden away from. See ExplorationStats::onFlight/onFlightLanded.
    Air,
    Freestyle,
    Tricks,       // the named ones: flips, wheelies, burnouts
    // The Sweep rows: how much of the catalogue stands at each metal. ALWAYS
    // listed, unlike the pages below it - a dashboard is no use to a player
    // deciding whether to bother if it only appears once they have. Its own
    // page rather than a corner of another, and placed where it is because it
    // measures the pages above it and nothing below.
    Completion,
    // The rows about something going WRONG -- crashes, penalties, retirements,
    // running the tank dry, losing a chain. Unlisted until earned and out of the
    // total like Hidden, but a SEPARATE PAGE because the two are different
    // ideas: Hidden is for things a player did not know were there, this is for
    // things they would rather not have done. Filed together, the surprises page
    // reads as a rap sheet and the genuine secrets are buried in it. Out of the
    // total is the substantive half -- counted, the Platinum Sweep and so
    // Prestige could not be reached without crashing ninety-nine times and
    // running dry on track, and no ladder should ask anyone to ride badly.
    Misfortune,
    Plugin,       // "mxbmrp3": exploring the plugin itself, and the community nods
    Tinkering,    // the plugin's own files, builds and switches
    Hidden,       // unlisted until earned, then shown on this page
    COUNT
};

// Does this group's membership imply Entry::hidden -- unlisted until earned,
// and never part of the completion total. TWO groups do, for the two different
// reasons above; every other group is listed from the start and counted.
// The catalogue still states `hidden` per row, and the unit gate asserts the
// two agree, so a row cannot be filed here and stay in the total.
constexpr bool isUnlistedGroup(Group g) {
    return g == Group::Hidden || g == Group::Misfortune;
}

// Do this group's rows count toward the COMPLETION figures -- the Unlocked
// percentage and the four Sweeps?
//
// Five groups do not, for two reasons. Hidden and Misfortune are unlisted
// (above): a row nobody can aim at, and a row nobody should aim at. mxbmrp3 and
// Tinkering are listed and perfectly aimable, and still do not belong in a
// figure about riding -- counting them lets a player who never leaves the pits
// read as half finished, and makes a completionist install a pack to finish a
// racing ladder. And the Completion rows cannot be in their own denominator
// without All Bronze needing All Bronze at Bronze.
//
// The pages say so: a group this returns false for gets "(does not count towards
// progress)" on its heading, in the tab and in the generated docs.
constexpr bool countsTowardCompletion(Group g) {
    return !(isUnlistedGroup(g) || g == Group::Plugin ||
             g == Group::Tinkering || g == Group::Completion);
}

inline const char* groupName(Group g) {
    switch (g) {
        case Group::Mileage:   return "Mileage";
        case Group::Racing:    return "Racing";
        case Group::Consistency: return "Consistency";
        case Group::Variety:   return "Variety";
        case Group::Freestyle: return "Freestyle";
        case Group::Air:       return "Air";
        case Group::Tricks:    return "Tricks";
        case Group::Plugin:    return "mxbmrp3";
        case Group::Tinkering: return "Tinkering";
        case Group::Completion: return "Completion";
        case Group::Misfortune: return "Misfortune";
        case Group::Hidden:    return "Hidden";
        default:               return "";
    }
}

// ONE SPACING RULE, AND IT COVERS EVERY UNIT: a number and its unit are a
// single token, never separated. "150km", "60m", "100L", "1h 30min", "45s",
// "2.7s" -- and, in the row sentences that carry their own unit, "50%", "35G",
// "480FPS". Distances and volumes used to take a space while durations did
// not, which is how "Ride 30m in another rider's roost" came to differ from
// "Land a 60 m jump" by one space and got read as a distance.
//
// MINUTES ARE "min" UNDER EITHER RULE, and that is worth knowing before anyone
// reverses this: "30 m" collides with "60 m" exactly as badly as "30m" does
// with "60m", so spacing was never what disambiguated them.
//
// FIGURES IN A SENTENCE: words below ten, digits at ten and above. "Five laps
// in a row", "a chain of five tricks", "between two and five in the morning" --
// but "Crash 13 times", "Crash 99 times in total". It is the ordinary
// typographic rule, and it is written down because a one-shot spells its own
// number (a tiered row substitutes it, so the question never arises there).
//
// UNSPACED RATHER THAN SPACED because the row is 47 characters (TAB_ROW_CHARS,
// measured) and a space per unit is not free: spacing every unit puts eleven
// row/tier combinations over that budget, unspacing puts none.
//
// How a value of that metric is written for a human: "1,234", "1,234 km",
// "12 h" / "3.5 h", "1h 05m" / "45s", and "2.7s" for the short durations a
// tenth matters in (airtime, a wheelie).
enum class Unit : uint8_t { Count, Km, Hours, Seconds, SecondsTenths, Litres, Metres };

constexpr int TIER_COUNT = 4;
// Rows per page in the settings tab, and so the number that decides how a group
// paginates: a page is a group, or an ENTRIES_PER_PAGE chunk of one. It lives
// here rather than in settings_tab_achievements.cpp (its only reader in the
// plugin) so the generated docs/achievements.md can number rows by the page
// they actually land on without hardcoding a second copy of it.
// Raising it makes the tab taller, which settings_fit_test / theme_geometry_test
// gate against the shared panel height.
constexpr int ENTRIES_PER_PAGE = 8;
// The Achievements tab's task row: the next tier's sentence and the progress
// numbers share one row beside a 1.4-row icon, and this is the characters that
// row holds (measured at the shipped layout). test_achievements.cpp holds every
// row's sentence plus its widest in-progress numbers under it; the tab cuts a
// sentence only for a count grown far past Platinum.
constexpr int TAB_ROW_CHARS = 47;

struct Entry {
    const char* id;          // stable key in the stats file; never renamed
    const char* title;       // "Long Hauler"
    Group group;             // the settings page it is listed on
    // Description of reaching a tier, as a printf template with ONE %s for the
    // formatted threshold. descOne is used INSTEAD when the threshold is exactly
    // 1 ("Finish a race", not "Finish 1 races"); nullptr = descMany always.
    const char* descOne;
    const char* descMany;
    const char* icon;        // marker icon name under mxbmrp3_data/icons/ (outlined set); nullptr = none yet
    Metric metric;
    Unit unit;
    double thresholds[TIER_COUNT];   // ascending, in the unit above
    // Only meaningful where the game reports a crash state (GAME_HAS_CRASH_STATE):
    // on the others every race is "clean" and the crash tally never moves, so the
    // manager hides these rows instead of showing two permanently frozen bars.
    bool needsCrashState;
    // Fed by FmxManager, which only the motorbike games run (GAME_HAS_FMX); a
    // kart would show a row that can never move.
    bool needsFmx;
    // ---- the three below default, so the older rows need not name them ----
    // For Metric::Exploration: which signal the row reads.
    Exploration::Signal signal = Exploration::Signal::None;
    // How many of `thresholds` are real (1..TIER_COUNT). A one-shot ("Land a
    // backflip once and that is that") has one tier, threshold 1, and no
    // metal: its tag reads "Earned" and its toast is the title alone.
    int tierCount = TIER_COUNT;
    // Unlisted in the settings tab until earned, and never part of the Progress
    // total: an earned hidden tier counts ON TOP, so ten listed rows and one
    // hidden read 100% at ten and 110% at eleven. Toasts as normal. Kept to the
    // genuine surprises -- a row a player could aim at belongs on a page.
    bool hidden = false;
    // A dedication, "@Name": the person who asked for it, appended to every
    // tier's sentence by formatDescription ("Bind 15 hotkeys (@Vegi)"). A
    // credited sentence is kept short enough for the row budget with it on.
    const char* credit = nullptr;
};

// ADDING, DISABLING AND REMOVING ROWS -- one rule for every row, old or new.
//   Add:     one row below, feeding an existing metric or a new signal
//            (exploration_signals.h: an enum line, a key, ONE feed call).
//   Disable: its id in kDisabledIds. The row is skipped as if game-gated; the
//            tier already in a player's file is kept (and honoured again if the
//            id comes back), never counted or shown meanwhile.
//   Remove:  delete the row. test_achievements.cpp holds every Metric and every
//            Signal to EXACTLY one row, so the metric case or the signal line
//            (and with it the feed call) must go too, or the gate says so.
// (The trailing nullptr keeps the array legal while nothing is disabled.)
inline constexpr const char* kDisabledIds[] = {
    nullptr,
};

inline bool isDisabledId(const char* id) {
    for (const char* d : kDisabledIds) {
        if (d && id && std::strcmp(d, id) == 0) return true;
    }
    return false;
}

// THE CATALOGUE. Grouped, and in display order within a group (before the
// earned/locked sort). Keep ids lowercase_snake; they are file keys.
// docs/achievements.md is generated from this table by test_achievements.cpp.
// `inline`: one entity for every translation unit, so `entry - kCatalogue` is
// defined wherever findById()'s pointer came from. A plain constexpr array has
// internal linkage - a copy per TU - and that subtraction is then only defined
// by luck of which findById the linker kept.
inline constexpr Entry kCatalogue[] = {
    // ---- Mileage: how much, how long ----------------------------------------
    { "distance",       "Long Hauler",      Group::Mileage, nullptr,
      "Ride %s in total",                      "road",
      Metric::DistanceKm,     Unit::Km,      { 100, 1000, 5000, 25000 },        false, false },
    { "ride_time",      "Seat Time",        Group::Mileage, nullptr,
      "Spend %s on track",                     "clock",
      Metric::RideTimeHours,  Unit::Hours,   { 10, 100, 500, 1000 },             false, false },
    { "laps",           "Lap Counter",      Group::Mileage, nullptr,
      "Complete %s laps",                      "repeat",
      Metric::Laps,           Unit::Count,   { 100, 500, 2000, 5000 },        false, false },
    // HIDDEN, and one-shot. Fuel burnt only started being counted this
    // release, so every install begins at zero - a listed row climbing towards
    // 2,000 L is a bar that visibly cannot be finished, which is worse than no
    // row. Unlisted it is a surprise when it lands instead.
    { "fuel_burnt",     "Carbon Footprint", Group::Hidden, nullptr,
      "Burn %s of fuel",                       "seedling",
      Metric::Exploration,    Unit::Litres,  { 100, 0, 0, 0 },                  false, false,
      Exploration::Signal::FuelBurnt, 1, true },
    { "gear_shifts",    "Shift Happens",    Group::Mileage, nullptr,
      "Shift %s times",                  "gear",
      Metric::GearShifts,     Unit::Count,   { 1000, 10000, 100000, 1000000 },   false, false },
    { "session_laps",   "Marathon",         Group::Mileage, nullptr,
      "Complete %s laps in one session",       "person-running",
      Metric::MaxSessionLaps, Unit::Count, { 25, 50, 75, 100 },             false, false },
    { "session_time",   "Iron Butt",        Group::Mileage, nullptr,
      "Ride %s in one day",                    "motorcycle",
      Metric::Exploration,    Unit::Hours,   { 1, 2, 4, 8 },                     false, false,
      Exploration::Signal::DayRideHours, TIER_COUNT, false, "@soulberg3" },
    { "track_laps",     "Local Hero",       Group::Mileage, nullptr,
      "Complete %s laps at one track",         "location-dot",
      Metric::MaxTrackLaps,   Unit::Count,   { 50, 250, 500, 1000 },            false, false },
    { "bike_km",        "Loyal",            Group::Mileage, nullptr,
      "One bike, %s",                   "heart",
      Metric::MaxBikeKm,      Unit::Km,      { 100, 500, 2500, 5000 },          false, false,
      Exploration::Signal::None, TIER_COUNT, false, "@BLAND525" },
    // ---- Racing: results ----------------------------------------------------
    { "races",          "Racer",            Group::Racing, "Finish a race",
      "Finish %s races",                       "flag-checkered",
      Metric::Races,          Unit::Count,   { 1, 10, 100, 500 },                false, false },
    { "wins",           "Winner",           Group::Racing, "Win a race",
      "Win %s races",                          "trophy",
      Metric::Wins,           Unit::Count,   { 1, 10, 50, 150 },                 false, false },
    { "podiums",        "Podium Regular",   Group::Racing, "Finish on the podium",
      "Finish on the podium %s times",         "ranking-star",
      Metric::Podiums,        Unit::Count,   { 1, 25, 100, 300 },                false, false },
    // A ONE-SHOT. Lapping a whole field needs a grid that will let you - it is
    // not something you can go and do on purpose - so as a career tally it asked
    // for a hundred of them, which is not a target, it is a wish. Done once, it
    // is a story. Listed, on Racing: it is a race result, not a secret.
    { "lapped_field",   "Lapped the Field", Group::Racing, "Win with every rider at least a lap down",
      nullptr,                                 "crown",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::LappedField, 1, false },
    { "fastest_laps",   "Hot Lapper",       Group::Racing, "Set the fastest lap in a race",
      "Set the fastest lap in %s races",       "fire",
      Metric::FastestLaps,  Unit::Count,   { 1, 10, 50, 200 },                false, false },
    // Race-side extras the callbacks already carry: the weather at the finish
    // and the size of the grid.
    { "rain_races",     "Mudder",           Group::Racing, "Finish a race in the rain",
      "Finish %s races in the rain",           "cloud-showers-heavy",
      Metric::RainRaces,      Unit::Count,   { 1, 5, 15, 25 },                  false, false },
    { "big_grids",      "Crowd Surfer",     Group::Racing, "Finish a race on a 20+ rider grid",
      "Finish %s races on a 20+ rider grid",    "people-group",
      Metric::BigGridRaces, Unit::Count, { 1, 10, 50, 150 },              false, false },
    // The first SPLIT off the gate, whichever index that is, and both the title
    // and the sentence say so: the game never reports its holeshot marker
    // (handlers/opening_line.h), so this is what the row can honestly measure,
    // and the "-ish" stays until it does. Charger's row counts "from the first
    // split" - the same line, same words.
    { "holeshots",      "Holeshot-ish",     Group::Racing, "Lead at the first split",
      "Lead at the first split in %s races",  "right-from-bracket",
      Metric::Exploration,  Unit::Count,   { 1, 10, 50, 150 },                false, false,
      Exploration::Signal::Holeshots },
    { "charger",        "Charger",          Group::Racing, nullptr,
      // "up ON the first split" read as a place on track rather than the
      // position held there; "from" says which end the count starts at, and
      // "climb" is the shorter verb that keeps the row inside its budget.
      "Climb %s places from the first split", "caret-up",
      Metric::Exploration, Unit::Count, { 5, 10, 15, 20 }, false, false,
      Exploration::Signal::PositionsGained },
    { "last_lap_pass",  "Last Gasp",        Group::Racing, "Pass on the last lap",
      "Pass on the last lap in %s races",       "circle-arrow-up",
      Metric::Exploration,  Unit::Count,   { 1, 10, 50, 150 },                false, false,
      Exploration::Signal::LastLapPasses },
    // "races of attrition" only meant anything to someone who had read the
    // bronze line first, and the bronze line was missing a word. One shape for
    // all four. The 6-starter floor below is still not in the sentence - it
    // does not fit - so a four-rider lobby losing one still looks like a miss.
    { "survivor",       "Survivor",         Group::Racing, "Finish a race where a quarter quit",
      "Finish %s races where a quarter quit",  "star-of-life",
      Metric::Exploration,  Unit::Count,   { 1, 5, 15, 40 },                  false, false,
      Exploration::Signal::Survivals },
    { "roost",          "Roost",            Group::Racing, nullptr,
      "Ride %s in a rider's wake",             "wind",
      Metric::Exploration,  Unit::Seconds, { 300, 1800, 7200, 18000 },        false, false,
      Exploration::Signal::RoostSec },
    // ---- Consistency: the streaks ------------------------------------------
    { "clean_races",    "Rubber Side Down", Group::Consistency, "Finish a race without crashing",
      "Finish %s races without crashing",      "shield",
      Metric::CleanRaces,    Unit::Count,   { 1, 5, 15, 25 },                 true, false },
    { "penalty_free",   "Clean Sheet",      Group::Consistency, nullptr,
      "Go %s races in a row penalty-free", "clipboard-check",
      Metric::PenaltyFreeStreak, Unit::Count, { 5, 10, 15, 25 },      false, false },
    // A WHOLE RACE without letting it slip, which is the page's idea carried
    // from a lap to a race. Tiered rather than one-shot: leading one race start
    // to finish is a good afternoon, twenty-five of them is the habit.
    // SAYS THE WIN, because it has always required one (the evaluation sits
    // inside the position == 1 branch) and the sentence did not. Leading every
    // lap and losing it at the flag is not wire to wire in any usage of the
    // phrase - it is Choke, which is its own row, and a race must not earn
    // both. "Leading every lap" rather than "start to finish": positions are
    // read at each lap's end, so a pass taken back before the line still counts.
    { "wire_to_wire",   "Wire to Wire",     Group::Consistency, "Win a race leading every lap",
      "Win %s races leading every lap",        "arrow-right",
      Metric::Exploration, Unit::Count, { 1, 5, 10, 25 }, false, false,
      Exploration::Signal::WireToWire },
    // The four things a race can be won with, all in the same race: the
    // holeshot, the lead on every lap, the fastest lap of it, and the flag.
    // Each is already its own row (Holeshot, Wire to Wire, Hot Lapper,
    // Winner); this is the one race they happened together. LISTED and counted,
    // unlike its Hidden neighbours, because every part of it is something a
    // rider can set out to do - which is the line this page draws.
    { "perfect_race",   "Perfect Race",     Group::Consistency,
      "Holeshot, every lap led, fastest lap, win",
      nullptr,                                 "hat-wizard",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::PerfectRace, 1 },
    { "clean_laps",     "Spotless",         Group::Consistency, nullptr,
      // "clean" meant nothing on its own, and the obvious guess - no crashes -
      // is wrong: onLapCompleted takes crashes AND penalties. The slash is what
      // keeps the row inside its budget, since "crash- and penalty-free" is 52
      // on its own; dropping "in a row" would make a STREAK read as a running
      // total. The ladder ends at FIFTY because the streak breaks on a single
      // spill: a hundred and fifty consecutive clean laps is several flawless
      // hours, which is a bar nobody was going to clear twice by accident.
      "%s laps in a row, crash/penalty free",   "hand-sparkles",
      Metric::Exploration,  Unit::Count,   { 5, 15, 30, 50 },                 true, false,
      Exploration::Signal::CleanLapStreak },
    { "crashes",        "Skill Issue",      Group::Misfortune, nullptr,
      "Crash %s times",                        "user-injured",
      Metric::Crashes,    Unit::Count,   { 1000, 0, 0, 0 },                  true, false,
      Exploration::Signal::None, 1, true },
    { "penalties",      "Rule Bender",      Group::Misfortune, nullptr,
      "Collect %s penalties",                  "triangle-exclamation",
      Metric::Penalties,      Unit::Count,   { 100, 0, 0, 0 },                   false, false,
      Exploration::Signal::None, 1, true, "@Froxy" },
    { "penalty_time",   "Time Served",      Group::Misfortune, nullptr,
      "Serve %s of penalty time",              "hourglass",
      Metric::PenaltyTimeSec, Unit::Seconds, { 600, 0, 0, 0 },                   false, false,
      Exploration::Signal::None, 1, true },
    // ---- Progress: variety and improvement ----------------------------------
    { "tracks",         "Globetrotter",     Group::Variety, nullptr,
      "Ride %s different tracks",              "globe",
      Metric::Tracks,         Unit::Count,   { 5, 25, 50, 100 },                  false, false },
    { "bikes",          "Collector",        Group::Variety, nullptr,
      "Ride %s different bikes",               "gem",
      Metric::Bikes,          Unit::Count,   { 3, 10, 25, 50 },                  false, false },
    { "personal_bests", "Personal Best",    Group::Variety, "Set a personal best",
      "Set %s personal bests",                 "star",
      Metric::PersonalBests,  Unit::Count,   { 1, 10, 100, 500 },                false, false },
    { "pb_leaps",       "Leap Forward",     Group::Variety, "Beat a PB by a second or more",
      "Beat %s PBs by a second or more", "arrow-trend-up",
      Metric::PbLeaps, Unit::Count, { 1, 10, 50, 100 },                false, false },
    // ---- Freestyle: FMX totals. A trick counts when its CHAIN completes
    // (FmxManager::completeTrick), the same rule the FMX HUD's session totals
    // follow, so a chain that ends in a crash earns nothing here either.
    { "fmx_tricks",     "Show-off",         Group::Freestyle, nullptr,
      "Land %s freestyle tricks",              "wand-magic-sparkles",
      Metric::FmxTricks,   Unit::Count,   { 10, 100, 1000, 10000 },           false, true },
    { "fmx_points",     "Style Points",     Group::Freestyle, nullptr,
      "Score %s FMX points",          "certificate",
      Metric::FmxScore,    Unit::Count,   { 1000, 10000, 100000, 500000 },   false, true },
    { "fmx_chain",      "Chain Reaction",   Group::Freestyle, nullptr,
      "Score %s points in one chain",          "link",
      Metric::FmxChainScore, Unit::Count, { 250, 750, 1500, 2500 },        false, true },
    { "fmx_kinds",      "Trick Collector",  Group::Freestyle, nullptr,
      "Land %s kinds of freestyle trick",      "list-check",
      Metric::FmxKinds, Unit::Count, { 4, 8, 12, 15 },                 false, true },
    // ---- Air: jumping, which is not freestyle --------------------------------
    // THE MATRIX, and the page reads in this order: three measurements (time,
    // distance, height) as lifetime TOTALS, then the same three as the best
    // SINGLE jump. The order is the explanation - a player scanning the page
    // meets each measurement twice, summed then peaked, and the landing rule
    // falls out of which half they are in.
    // TITLES AND IDS CROSS HERE, deliberately: `air_miles` is titled Frequent
    // Flyer (seconds aloft) and `air_distance` is titled Air Miles (kilometres
    // aloft). Air miles are a DISTANCE, so the kilometres row took the name and
    // the seconds row took the one the crash row used to carry. The ids are
    // stats-file keys and never move; grep by id and read the title here.
    { "air_miles",      "Frequent Flyer",   Group::Air, nullptr,
      "Spend %s in the air",                   "cloud",
      Metric::Exploration, Unit::Seconds, { 7200, 54000, 180000, 360000 }, false, true,
      Exploration::Signal::AirtimeSec },
    { "air_distance",   "Air Miles",        Group::Air, nullptr,
      "Cover %s through the air",              "suitcase",
      Metric::Exploration, Unit::Km,    { 1, 10, 50, 150 },                    false, true,
      Exploration::Signal::AirDistanceKm },
    // The third measurement, summed: every metre of climb, banked at touchdown
    // like the two above it. PLACEHOLDER THRESHOLDS: nobody has reported what a
    // season's worth of climb actually comes to, so these are a first cut, to be
    // re-cut once real telemetry says what the numbers look like.
    { "air_height",     "To the Moon",      Group::Air, nullptr,
      "Climb %s in jumps",                     "rocket",
      Metric::Exploration, Unit::Km,    { 1, 5, 20, 50 },                      false, true,
      Exploration::Signal::AirHeightKm },
    // WAS Metric::FmxAirtimeSec, the longest airborne TRICK - so a five-second
    // step-down you simply rode out earned nothing, while the row said "in one
    // jump". It reads the flight detector now, like the five rows around it,
    // which is what the sentence always claimed. The stored value restarts
    // (it is a different measurement); earned tiers are kept, as tiers only rise.
    { "fmx_airtime",    "Hang Time",        Group::Air, nullptr,
      "Stay airborne %s and land it",          "parachute-box",
      Metric::Exploration, Unit::SecondsTenths, { 2, 3, 4, 5 },        false, true,
      Exploration::Signal::LongestFlightSec },
    // RE-CUT: a big track jump clears the 60 m this used to top out at.
    { "jump_distance",  "Gap Jumper",       Group::Air, nullptr,
      "Land a %s jump",                        "ruler-horizontal",
      Metric::Exploration, Unit::Metres, { 10, 30, 60, 100 },                  false, true,
      Exploration::Signal::JumpDistanceM },
    // The flight itself, measured at every landing you RIDE AWAY FROM
    // (FmxManager::updateFlight): how high above where it left the ground, how
    // far across it, and how far in total. Casing one out credits nothing. The
    // HEIGHT is still a guess: nothing has reported what a big one comes to.
    { "jump_height",    "Sent It",          Group::Air, nullptr,
      "Reach %s above takeoff and land it",    "ruler-vertical",
      Metric::Exploration, Unit::Metres, { 5, 10, 18, 25 },                    false, true,
      Exploration::Signal::JumpHeightM },
    // ---- Tricks: the named ones ---------------------------------------------
    { "fmx_backflips",  "Flipper",          Group::Tricks, "Land a backflip",
      "Land %s backflips",                     "arrow-rotate-left",
      Metric::FmxBackflips,   Unit::Count,   { 1, 10, 50, 100 },               false, true,
      Exploration::Signal::None, TIER_COUNT, false, "@Lynds" },
    // "Hold a wheelie for 30s" never said the part that decides it: the duration
    // only counts once the chain BANKS, so a wheelie ridden into a crash is
    // worth nothing. Same verb as its endo counterpart, so the pair reads as one
    // idea - hold it, then put it down.
    { "fmx_wheelie",    "Wheelie King",     Group::Tricks, nullptr,
      "Land a %s wheelie",                     "wheelie",
      Metric::FmxWheelieSec,  Unit::SecondsTenths, { 5, 10, 20, 30 },            false, true },
    { "fmx_wheelie_km", "One-Wheel Tour",   Group::Freestyle, nullptr,
      "Ride %s on the back wheel",             "route",
      Metric::FmxWheelieKm, Unit::Km,    { 1, 10, 50, 100 },               false, true },
    { "fmx_whips",      "Whip It",          Group::Tricks, nullptr,
      "Land %s whips",                         "whip-right",
      Metric::FmxWhips,       Unit::Count,   { 10, 100, 500, 2500 },             false, true },
    { "fmx_scrubs",     "Scrubber",         Group::Tricks, nullptr,
      "Land %s scrubs",                        "arrow-right-arrow-left",
      Metric::FmxScrubs,      Unit::Count,   { 10, 100, 500, 2500 },             false, true },
    { "fmx_oppos",      "Oppo",             Group::Tricks, "Land an oppo",
      "Land %s oppos",                         "turn-up",
      Metric::FmxOppos,       Unit::Count,   { 1, 10, 100, 500 },                false, true },
    // THE HOLD, not the count: tapping the front wheel down is nothing, holding
    // it there is the trick. A ONE-SHOT still - three seconds either happened or
    // it did not, and a ladder would ask a rider to keep going on the one wheel
    // that steers. THREE SECONDS is a tenth of Wheelie King's thirty, which is
    // the right ratio for the harder end of the same machine: the two rows are
    // one bike tilted opposite ways, and their glyphs say so (assets/icons/
    // endo.svg is wheelie.svg with the rotation negated).
    //
    // IT SAYS STOPPIE because the count always included them - a stoppie is an
    // endo held to a standstill, and fmx_manager_scoring.cpp credits both to
    // the same tally - and it says LAND because that was always true and never
    // stated: recordFmxTrick is only reached from a BANKED chain, so a trick
    // crashed out of has never counted, here or on any other trick row.
    { "fmx_endos",      "Endo",             Group::Tricks, nullptr,
      "Land a %s endo or stoppie",             "endo",
      Metric::FmxEndoSec, Unit::SecondsTenths, { 3, 0, 0, 0 },                   false, true,
      Exploration::Signal::None, 1 },
    { "fmx_turn_downs", "Turn Down",        Group::Tricks, "Land a turn down",
      "Land %s turn downs",                    "turn-down",
      Metric::FmxTurnDowns,   Unit::Count,   { 1, 10, 100, 500 },                false, true },
    // ---- The one that fires on demand, and the file-and-build rows -----------
    { "breakout",       "Brick Breaker",    Group::Plugin, nullptr,
      // A NOUN PHRASE where its neighbours are instructions, and it has to be:
      // the credit is part of the rendered sentence, so "Breakout score 5,000
      // (@BennyP118)" already lands on exactly 47 beside "4,999 / 5,000". Every
      // verb-led form ("Score %s at Breakout") is three over. Leave it.
      "Breakout score %s",                  "cubes",
      Metric::Breakout,     Unit::Count,   { 5000, 0, 0, 0 },                  false, false,
      Exploration::Signal::None, 1, false, "@BennyP118" },
    // The one that fires on demand. RELOAD_CONFIG is a legitimate thing to
    // count -- tinkering is a real way to use this plugin -- and it is also the
    // one achievement a fresh install can earn on purpose in ten seconds, which
    // is what lets a player (and a developer) see that toasts work at all.
    { "config_reloads", "Tinkerer",         Group::Tinkering, "Reload the config",
      "Reload the config %s times",            "wrench",
      Metric::ConfigReloads, Unit::Count, { 1, 0, 0, 0 },                 false, false,
      Exploration::Signal::None, 1 },
    // ---- Racing extras the callbacks already carry --------------------------
    { "photo_finish",   "Photo Finish",     Group::Racing, "Win a race by under a tenth",
      nullptr,                                 "camera",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::PhotoFinish, 1 },
    // Photo Finish from the other side of the line. Same margin, same source
    // (FinishMargin), and deliberately NOT the same row: winning by a tenth and
    // losing by one are not the same thing to the person it happened to.
    { "so_close",       "So Close",         Group::Misfortune, "Lose a race by under a tenth",
      nullptr,                                 "poo",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::SoClose, 1, true },
    { "back_marker",    "Back Marker",      Group::Misfortune, "Finish a race three or more laps down",
      nullptr,                                 "road-barrier",
      Metric::Exploration, Unit::Count, { 3, 0, 0, 0 }, false, false,
      Exploration::Signal::BackMarker, 1, true },
    { "metronome",      "Metronome",        Group::Consistency, "Five laps in a row, all within a tenth",
      nullptr,                                 "equals",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::Metronome, 1 },
    // The chain that got away: the points an unfinished chain was worth when
    // it was lost (Chain Reaction's ladder, so the two rows read together).
    // 2,500 is Chain Reaction's Platinum: the chain you lost has to have been
    // worth the best one anybody banks.
    { "chain_lost",     "Weakest Link",     Group::Misfortune, nullptr,
      "Crash out of a %s-point chain",         "link-slash",
      Metric::Exploration, Unit::Count, { 2500, 0, 0, 0 }, false, true,
      Exploration::Signal::ChainLost, 1, true },
    { "tyre_shredder",  "Tyre Shredder",    Group::Freestyle, nullptr,
      // Spelling out what shredding IS - burnouts, donuts and drifts, which is
      // exactly what ShredSec counts - has never fitted the 47-character row
      // once the seconds column takes its 17, and "min" took two more. Two of
      // the three name it well enough; the title carries the rest.
      "%s in burnouts and drifts",      "tornado",
      Metric::Exploration, Unit::Seconds, { 60, 600, 1800, 3600 }, false, true,
      Exploration::Signal::ShredSec },
    // ---- Plugin: exploring what it can do, and the community nods ----------
    // Nods carry a placeholder title until the people they thank are named.
    { "tyre_kicker",    "Tyre Kicker",      Group::Plugin, "Try each HUD and widget",
      nullptr,                                 "magnifying-glass",
      Metric::Exploration, Unit::Count, { 100, 0, 0, 0 }, false, false,
      Exploration::Signal::HudsTried, 1, false, "@BrinkleyPT" },
    { "grand_tour",     "Grand Tour",       Group::Plugin, "Open every settings tab",
      nullptr,                                 "compass",
      Metric::Exploration, Unit::Count, { 100, 0, 0, 0 }, false, false,
      Exploration::Signal::TabsVisited, 1 },
    // ONE-SHOTS, both: overriding a colour and installing a pack are things you
    // either did or did not, and a ladder of them only asked a player who had
    // already made the plugin theirs to keep going for the sake of the bar.
    { "decorator",      "Interior Decorator", Group::Tinkering, "Override a colour, font or theme",
      nullptr,                                 "palette",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::Customisations, 1 },
    { "dress_up",       "Made It Yours",    Group::Tinkering, "Install a pack of any type",
      nullptr,                                 "shirt",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::CustomPacks, 1, false, "@bh5o" },
    { "on_air",         "On Air",           Group::Plugin, "Connect the web overlay",
      nullptr,                     "tower-broadcast",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::OverlayConnections, 1, false, "@Special Ed" },
    { "second_screen",  "Second Screen",    Group::Plugin, "Show the HUD on a second screen",
      nullptr,                                 "display",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::CompanionOpens, 1 },
    { "directors_cut",  "Director's Cut",   Group::Plugin, nullptr,
      // Verbless, like Loyal: naming the feature costs more than the verb does.
      "Watch %s director cuts",                "clapperboard",
      Metric::Exploration, Unit::Count, { 500, 0, 0, 0 }, false, false,
      Exploration::Signal::DirectorCuts, 1, false, "@Tallon214" },
    { "backseat",       "Backseat Driver",  Group::Plugin, nullptr,
      "Hear %s spotter calls",                 "headset",
      Metric::Exploration, Unit::Count, { 500, 0, 0, 0 }, false, false,
      Exploration::Signal::SpotterCallouts, 1, false, "@Foreign" },
    // The SETTING, not hours behind it. The hours version could not be earned
    // at all by a player whose pad the plugin does not see - which is how it
    // reached a stream and stayed locked all night. That is also why it was
    // hidden, and why it no longer is: asking only for the switch makes it a
    // thing a player can go and do, which is what the listed pages are for.
    { "good_vibrations", "Good Vibrations", Group::Plugin, "Turn rumble on",
      nullptr,                                 "wave-square",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::RumbleOn, 1, false, "@Debra" },
    { "keymaster",      "Keymaster",        Group::Plugin, "Bind a hotkey",
      nullptr,                                 "keyboard",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::HotkeysBound, 1, false, "@Vegi" },
    { "profile_hopper", "Profile Hopper",   Group::Plugin, nullptr,
      "Switch profiles %s times",              "shuffle",
      Metric::Exploration, Unit::Count, { 100, 0, 0, 0 }, false, false,
      Exploration::Signal::ProfileSwitches, 1 },
    { "fresh_coat",     "Fresh Coat",       Group::Plugin, "Install an update in-game",
      nullptr,                                 "download",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::UpdatesInstalled, 1 },
    { "split_decision", "Split Decision",   Group::Plugin, "Time a segment",
      nullptr,                                 "scissors",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::Segments, 1, false, "@ZealousidealWin823" },
    // Six hours, not a hundred: this counts time NOT riding, and a hundred
    // hours of it asked players to leave the game running to earn a badge -
    // which is the shape of grind every other row here avoids.
    { "armchair",       "Armchair Marshal", Group::Plugin, nullptr,
      "Spectate or replay for %s",     "couch",
      Metric::Exploration, Unit::Hours, { 6, 0, 0, 0 }, false, false,
      Exploration::Signal::SpectateHours, 1 },
    { "regular",        "Regular",          Group::Variety, nullptr,
      "Show up on %s different days",             "calendar-days",
      Metric::Exploration, Unit::Count, { 7, 30, 100, 365 }, false, false,
      Exploration::Signal::DaysUsed },
    { "day_streak",     "On a Roll",        Group::Variety, nullptr,
      "Show up %s days in a row",              "chart-line",
      Metric::Exploration, Unit::Count, { 3, 4, 5, 7 }, false, false,
      Exploration::Signal::DayStreak },
    { "servers",        "Well Travelled",   Group::Variety, nullptr,
      "Join %s different servers",             "server",
      Metric::Exploration, Unit::Count, { 5, 10, 25, 50 }, false, false,
      Exploration::Signal::Servers },
    { "bike_classes",   "Class Act",        Group::Variety, nullptr,
      "Ride bikes from %s classes",            "layer-group",
      Metric::BikeClasses, Unit::Count, { 2, 3, 4, 5 }, false, false,
      Exploration::Signal::None, TIER_COUNT, false, "@Aiden" },
    // Replaces Version Hopper, which counted releases run and so measured our
    // cadence rather than anything the player chose. Opting into pre-releases is
    // a choice, and it is made once. Credit kept: same spirit, running the
    // bleeding edge.
    { "prerelease",     "Early Access",     Group::Hidden, "Switch to the pre-release channel",
      nullptr,                                 "code-branch",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::Prerelease, 1, true, "@Husk" },
    // 480 is the plugin's own performance target - the number run_perf.sh gates
    // against and BenchmarkWidget colours against - so it is the row, and the
    // steps below it were never the interesting part. The signal stays a
    // max-ever of frames in one second, which is what earns it.
    { "frame_perfect",  "Frame Perfect",    Group::Hidden, nullptr,
      "Hit %sFPS",                             "gauge-high",
      Metric::Exploration, Unit::Count, { 480, 0, 0, 0 }, false, false,
      Exploration::Signal::FramePerfect, 1, true, "@Mave" },
    { "stalker",        "Stalker",          Group::Plugin, nullptr,
      "Track %s riders",                       "eye",
      Metric::Exploration, Unit::Count, { 10, 0, 0, 0 }, false, false,
      Exploration::Signal::RidersTracked, 1, false, "@turkishmonk" },
    // ALWAYS LISTED, unlike Hidden and Misfortune: these are a dashboard, and a
    // dashboard that appears once you are already on it is no use to the player
    // deciding whether to bother. Four bars at 0% on a fresh install is the
    // honest picture. (An earlier cut hid them until their value left zero;
    // nothing does that now, and this comment used to claim it still did.)
    { "all_bronze",     "Bronze Sweep",     Group::Completion, "Counted achievements earned",
      nullptr,                                 "medal",
      Metric::CompletionBronze,   Unit::Count, { 100, 0, 0, 0 },              false, false,
      Exploration::Signal::None, 1, false },
    { "all_silver",     "Silver Sweep",     Group::Completion, "Counted achievements at Silver or max",
      nullptr,                                 "medal",
      Metric::CompletionSilver,   Unit::Count, { 100, 0, 0, 0 },              false, false,
      Exploration::Signal::None, 1, false },
    { "all_gold",       "Gold Sweep",       Group::Completion, "Counted achievements at Gold or max",
      nullptr,                                 "medal",
      Metric::CompletionGold,     Unit::Count, { 100, 0, 0, 0 },              false, false,
      Exploration::Signal::None, 1, false },
    { "all_platinum",   "Platinum Sweep",   Group::Completion, "Counted achievements maxed out",
      nullptr,                                 "medal",
      Metric::CompletionPlatinum, Unit::Count, { 100, 0, 0, 0 },              false, false,
      Exploration::Signal::None, 1, false },
    { "phoenix",        "Phoenix",          Group::Hidden, "Come back after a crash to desktop",
      nullptr,                                 "fire-flame-curved",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::Phoenix, 1, true },
    { "ninety_nine",    "99 Problems",      Group::Misfortune, "Crash 99 times in total",
      nullptr,                                 "skull-crossbones",
      Metric::Exploration, Unit::Count, { 99, 0, 0, 0 }, true, false,
      Exploration::Signal::CrashTally99, 1, true, "@TheRealSliX" },
    { "test_pilot",     "Test Pilot",       Group::Hidden, "Turn on an experimental setting",
      nullptr,                                 "paper-plane",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::TestPilot, 1, true },
    { "under_hood",     "Under the Hood",   Group::Hidden, "Edit the settings file by hand",
      nullptr,                                 "file-pen",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::SettingsEdited, 1, true },
    { "backup_plan",    "Backup Plan",      Group::Hidden, "Edit the stats file by hand",
      nullptr,                                 "floppy-disk",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::StatsEdited, 1, true },
    { "stylist",        "Stylist",          Group::Tinkering, "Style the web overlay with custom.css",
      nullptr,                                 "paintbrush",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::Stylist, 1, false },
    { "homebrew",       "Homebrew",         Group::Hidden, "Run a build you made yourself",
      nullptr,                                 "beer-mug-empty",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::Homebrew, 1, true },
    { "ungrateful",     "Ungrateful",       Group::Tinkering, "Turn achievement toasts off",
      nullptr,                                 "bell-slash",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::Ungrateful, 1 },
    { "factory_fresh",  "Factory Fresh",    Group::Tinkering, "Reset every setting to defaults",
      nullptr,                                 "industry",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::FactoryFresh, 1 },
    { "developer",      "Developer",        Group::Hidden, "Enable developer mode",
      nullptr,                                 "code",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::Developer, 1, true },
    { "night_owl",      "Night Owl",        Group::Hidden, "Ride between two and five in the morning",
      nullptr,                                 "moon",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::NightOwl, 1, true },
    { "anniversary",    "Anniversary",      Group::Hidden, "Ride on the anniversary of your first run",
      nullptr,                                 "cake-candles",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::Anniversary, 1, true },
    { "sandbagger",     "Sandbagger",       Group::Racing, "Set a PB on the last lap of a race",
      nullptr,                                 "weight-hanging",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::Sandbagger, 1 },
    { "palindrome",     "Palindrome",       Group::Hidden, "Set a lap time the same backwards",
      nullptr,                                 "arrows-left-right",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::Palindrome, 1, true },
    { "deja_vu",        "Deja Vu",          Group::Hidden, "Set two identical lap times in a session",
      nullptr,                                 "clone",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::DejaVu, 1, true },
    // A one-shot on purpose: the joke lands on the first one, and a ladder
    // would read as an invitation to the next nine hundred and ninety-nine.
    { "rage_quit",      "Rage Quit",        Group::Misfortune, "Retire from a race before the finish",
      nullptr,                                 "door-open",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::RageQuits, 1, true },
    { "bakers_dozen",   "Baker's Dozen",    Group::Misfortune, "Crash 13 times in one session",
      nullptr,                                 "bread-slice",
      Metric::Exploration, Unit::Count, { 13, 0, 0, 0 }, true, false,
      Exploration::Signal::BakersDozen, 1, true },
    // Thirty minutes is what the threshold below has always awarded, though the
    // row and the signal comment both used to claim an hour. It does not say "in
    // a session" because it cannot span one: the run is per-session scratch, so
    // an unbroken half hour is inside a session by construction. It does not say
    // "clean" either -- that is the undefined word Spotless was rewritten to
    // stop using, and this row means the same thing Rubber Side Down means.
    { "steady_hands",   "Steady Hands",     Group::Consistency, nullptr,
      "Ride %s without crashing",              "hand",
      Metric::Exploration, Unit::Seconds, { 1800, 0, 0, 0 }, true, false,
      Exploration::Signal::SteadyHands, 1 },
    { "fumes",          "Running on Fumes", Group::Racing, "Finish a race on 1% of a tank or less",
      nullptr,                                 "water",
      Metric::Exploration, Unit::Count,    { 1, 0, 0, 0 },                     false, false,
      Exploration::Signal::Fumes, 1 },
    { "ran_dry",        "Long Walk Home",   Group::Misfortune, "Run out of fuel out on track",
      nullptr,                                 "person-hiking",
      Metric::Exploration, Unit::Count,    { 1, 0, 0, 0 },                     false, false,
      Exploration::Signal::RanDry, 1, true },
    { "peak_g",         "Big Hit",          Group::Misfortune, nullptr,
      "Take a %sG hit",                        "explosion",
      Metric::Exploration, Unit::Count,    { 35, 0, 0, 0 },                    false, false,
      Exploration::Signal::PeakG, 1, true },
    { "solo_race",      "Just Me Then",     Group::Hidden, "Finish a race nobody else started",
      nullptr,                                 "heart-crack",
      Metric::Exploration, Unit::Count,    { 1, 0, 0, 0 },                     false, false,
      Exploration::Signal::SoloRace, 1, true },
    { "mondays",        "Case of the Mondays", Group::Misfortune, "Crash out of a chain of five tricks",
      nullptr,                                 "face-tired",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, true,
      Exploration::Signal::ChainCrash, 1, true },
    // ---- The ones that read a race, or a rider, sideways --------------------
    // Perfect Race's opposite number, and Last Gasp's: the same final lap,
    // read from the rider it was taken FROM. Deferred with Last Gasp for the
    // same reason (the pair can still be a lap behind at the flag), so the two
    // are credited by one helper rather than two that could disagree.
    { "choke",          "Choke",            Group::Misfortune,
      "Lead into the final lap and lose the race",
      nullptr,                                 "face-sad-cry",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::Choke, 1, true },
    // The pace without the result. OFF THE PODIUM, not merely beaten: a
    // fastest lap in second is a race lost by a little, and this row is for
    // the one where the lap was the only thing that went right.
    { "consolation",    "Consolation Prize", Group::Misfortune,
      "Fastest lap, and off the podium with it",
      nullptr,                                 "thumbs-up",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::ConsolationPrize, 1, true },
    // Company on the floor. Counts whoever is down AROUND you at any point
    // while you are down too - the rider who lands on you a second later is as
    // much of a heap as the one you landed on - so it needs a crash state to
    // have any meaning. The sentence says "with", not "already": the row is
    // about the heap, not about who fell into it first.
    { "pile_up",        "Pile-Up",          Group::Misfortune,
      "Go down with three others",
      nullptr,                                 "truck-medical",
      Metric::Exploration, Unit::Count, { 3, 0, 0, 0 }, true, false,
      Exploration::Signal::PileUp, 1, true },
    // Pile-Up from the outside: the same heap, ridden past instead of joined.
    // A separate row rather than a second tier of one, because they are not
    // degrees of the same thing - one is a crash and the other is avoiding it -
    // and hidden rather than listed because it needs three strangers to fall
    // over near you, which is nothing to aim at. RIDE means moving here too.
    { "peace_out",      "Peace Out",        Group::Hidden,
      "Ride through three riders down",
      nullptr,                                 "hand-peace",
      Metric::Exploration, Unit::Count, { 3, 0, 0, 0 }, true, false,
      Exploration::Signal::PeaceOut, 1, true },
    // The same corner, three times running. A RUN, not a tally: three crashes
    // spread over an afternoon at one corner is a hard corner, three in a row
    // is a grudge.
    { "favorite_spot",  "Favorite Spot",    Group::Misfortune,
      "Crash three times in a row in one place",
      nullptr,                                 "campground",
      Metric::Exploration, Unit::Count, { 3, 0, 0, 0 }, true, false,
      Exploration::Signal::FavoriteSpot, 1, true },
    // Measured from the telemetry rather than off the Burnout trick, which
    // has its own INI switch: a row that vanished when someone turned a trick
    // off would be a bug nobody could see. Ten seconds of it, unbroken.
    { "digging",        "Digging a Hole",   Group::Hidden, nullptr,
      "Spin the rear wheel in place for %s",    "person-digging",
      Metric::Exploration, Unit::Seconds, { 10, 0, 0, 0 }, false, false,
      Exploration::Signal::DiggingSec, 1, true },
    // A QUARTER OF FLIPPER'S LADDER at every step (1 / 10 / 50 / 100 -> 1 / 3 /
    // 13 / 25, halves rounded up), because a frontflip is the harder way round
    // and nobody lands them at backflip rates. Tied to that row rather than
    // picked freely, so the two move together if Flipper is ever re-cut.
    { "fmx_frontflips", "Somersault",       Group::Tricks, "Land a frontflip",
      "Land %s frontflips",                    "arrow-rotate-right",
      Metric::FmxFrontflips,  Unit::Count,   { 1, 3, 13, 25 },              false, true },
};

constexpr int COUNT = static_cast<int>(sizeof(kCatalogue) / sizeof(kCatalogue[0]));

inline bool isOneShot(const Entry& e) { return e.tierCount <= 1; }

// Tier 0 is "not yet"; 1..TIER_COUNT are the metals.
inline const char* tierName(int tier) {
    switch (tier) {
        case 1: return "Bronze";
        case 2: return "Silver";
        case 3: return "Gold";
        case 4: return "Platinum";
        default: return "Locked";
    }
}

// The entry with this id, or nullptr. Linear over the catalogue; called on load
// and from the settings tab, never per frame.
inline const Entry* findById(const char* id) {
    if (!id) return nullptr;
    for (const Entry& e : kCatalogue) {
        if (std::strcmp(e.id, id) == 0) return &e;
    }
    return nullptr;
}

// Highest tier whose threshold `value` has reached (0 = none).
inline int tierFor(const Entry& e, double value) {
    int tier = 0;
    for (int i = 0; i < e.tierCount; ++i) {
        if (value >= e.thresholds[i]) tier = i + 1;
    }
    return tier;
}

// The tag beside a row: the metal, or for a one-shot "Earned" / "Locked".
inline const char* tierLabel(const Entry& e, int tier) {
    if (isOneShot(e)) return tier > 0 ? "Earned" : "Locked";
    return tierName(tier);
}

// The threshold the bar is filling toward: the NEXT tier's, or the last one
// once everything is earned (so a complete row reads "50,000 / 50,000 km").
inline double targetThreshold(const Entry& e, int tier) {
    if (tier >= e.tierCount) return e.thresholds[e.tierCount - 1];
    if (tier < 0) tier = 0;
    return e.thresholds[tier];
}

// Fill fraction of the bar toward the next tier, 0..1. The bar measures the
// CURRENT STEP -- from the previous threshold to the next -- not the whole
// ladder, so a Silver-to-Gold rider at 4,000 of 10,000 km sees a bar a third
// full rather than one that barely moved since Bronze. Complete rows are full.
inline float progressFraction(const Entry& e, double value) {
    const int tier = tierFor(e, value);
    if (tier >= e.tierCount) return 1.0f;
    const double lo = tier > 0 ? e.thresholds[tier - 1] : 0.0;
    const double hi = e.thresholds[tier];
    if (hi <= lo) return 1.0f;
    double f = (value - lo) / (hi - lo);
    if (f < 0.0) f = 0.0;
    if (f > 1.0) f = 1.0;
    return static_cast<float>(f);
}

}  // namespace Achievements
