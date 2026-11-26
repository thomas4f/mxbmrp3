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
// budget. Only the settings tab and the toast need it. The types a row is
// written in (Metric, Group, Unit, Entry) live in achievement_types.h, split
// out the second time it did: the table itself is one array by design.
//
// WHY THRESHOLDS ARE IN DISPLAY UNITS (km, hours, seconds) rather than the
// metres/ms the stats keep: the same number is read by the description ("Ride
// 1,000 km"), the progress text ("340 / 1,000 km") and the evaluation, and a
// conversion at each of those sites is three places for one slip. The manager
// converts ONCE, at the metric read.
// ============================================================================
#pragma once

#include "achievement_types.h"
#include "i18n_mark.h"

#include <cstring>

namespace Achievements {

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

// Marathon's tier words, one per threshold (Entry::tierPhrases).
inline constexpr const char* kMarathonPhrases[TIER_COUNT] = {
    N_("a 10K"), N_("a half marathon"), N_("a marathon"), N_("an ultramarathon"),
};

// THE CATALOGUE. Grouped, and in display order within a group (before the
// earned/locked sort). Keep ids lowercase_snake; they are file keys.
// docs/achievements.md is generated from this table by test_achievements.cpp.
// `inline`: one entity for every translation unit, so `entry - kCatalogue` is
// defined wherever findById()'s pointer came from. A plain constexpr array has
// internal linkage - a copy per TU - and that subtraction is then only defined
// by luck of which findById the linker kept.
inline constexpr Entry kCatalogue[] = {
    // ---- Mileage: how much, how long ----------------------------------------
    { "distance",       N_("Long Hauler"),      Group::Mileage, nullptr,
      N_("Ride %s in total"),                      "road",
      Metric::DistanceKm,     Unit::Km,      { 100, 1000, 5000, 25000 },        false, false },
    { "ride_time",      N_("Seat Time"),        Group::Mileage, nullptr,
      N_("Spend %s on track"),                     "clock",
      Metric::RideTimeHours,  Unit::Hours,   { 10, 100, 500, 1000 },             false, false },
    { "laps",           N_("Lap Counter"),      Group::Mileage, nullptr,
      N_("Complete %s laps"),                      "repeat",
      Metric::Laps,           Unit::Count,   { 100, 500, 2000, 5000 },        false, false },
    // HIDDEN, and one-shot. Fuel burnt only started being counted this
    // release, so every install begins at zero - a listed row climbing towards
    // 2,000 L is a bar that visibly cannot be finished, which is worse than no
    // row. Unlisted it is a surprise when it lands instead.
    { "fuel_burnt",     N_("Carbon Footprint"), Group::Hidden, nullptr,
      N_("Burn %s of fuel"),                       "seedling",
      Metric::Exploration,    Unit::Litres,  { 100, 0, 0, 0 },                  false, false,
      Exploration::Signal::FuelBurnt, 1, true },
    { "gear_shifts",    N_("Shift Happens"),    Group::Mileage, nullptr,
      N_("Shift %s times"),                  "gear",
      Metric::GearShifts,     Unit::Count,   { 1000, 10000, 100000, 1000000 },   false, false },
    // MARATHON IS A DISTANCE, as the name says: laps were twice the riding on
    // an MX track that they were on a supercross one. The tiers are the running
    // distances, named in the sentence (Entry::tierPhrases). The id stays, so a
    // tier earned under the old laps rule is kept.
    { "session_laps",   N_("Marathon"),         Group::Mileage, nullptr,
      N_("Ride %s in a session"),                  "person-running",
      Metric::MaxSessionKm,   Unit::Km,      { 10, 21, 42, 100 },             false, false,
      Exploration::Signal::None, TIER_COUNT, false, nullptr, kMarathonPhrases },
    { "session_time",   N_("Iron Butt"),        Group::Mileage, nullptr,
      N_("Ride %s in one day"),                    "motorcycle",
      Metric::Exploration,    Unit::Hours,   { 1, 2, 4, 8 },                     false, false,
      Exploration::Signal::DayRideHours, TIER_COUNT, false, "@soulberg3" },
    { "track_laps",     N_("Local Hero"),       Group::Mileage, nullptr,
      N_("Complete %s laps at one track"),         "location-dot",
      Metric::MaxTrackLaps,   Unit::Count,   { 50, 250, 500, 1000 },            false, false },
    { "bike_km",        N_("Loyal"),            Group::Mileage, nullptr,
      N_("One bike, %s"),                   "heart",
      Metric::MaxBikeKm,      Unit::Km,      { 100, 500, 2500, 5000 },          false, false,
      Exploration::Signal::None, TIER_COUNT, false, "@BLAND525" },
    // ---- Racing: results ----------------------------------------------------
    { "races",          N_("Racer"),            Group::Racing, N_("Finish a race"),
      N_("Finish %s races"),                       "flag-checkered",
      Metric::Races,          Unit::Count,   { 1, 10, 100, 500 },                false, false },
    { "wins",           N_("Winner"),           Group::Racing, N_("Win a race"),
      N_("Win %s races"),                          "trophy",
      Metric::Wins,           Unit::Count,   { 1, 10, 50, 150 },                 false, false },
    { "podiums",        N_("Podium Regular"),   Group::Racing, N_("Finish on the podium"),
      N_("Finish on the podium %s times"),         "ranking-star",
      Metric::Podiums,        Unit::Count,   { 1, 25, 100, 300 },                false, false },
    // A ONE-SHOT. Lapping a whole field needs a grid that will let you - it is
    // not something you can go and do on purpose - so as a career tally it asked
    // for a hundred of them, which is not a target, it is a wish. Done once, it
    // is a story. Listed, on Racing: it is a race result, not a secret.
    { "lapped_field",   N_("Lapped the Field"), Group::Racing, N_("Win with every rider at least a lap down"),
      nullptr,                                 "crown",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::LappedField, 1, false },
    { "fastest_laps",   N_("Hot Lapper"),       Group::Racing, N_("Set the fastest lap in a race"),
      N_("Set the fastest lap in %s races"),       "fire",
      Metric::FastestLaps,  Unit::Count,   { 1, 10, 50, 200 },                false, false },
    // Race-side extras the callbacks already carry: the weather at the finish
    // and the size of the grid.
    { "rain_races",     N_("Mudder"),           Group::Racing, N_("Finish a race in the rain"),
      N_("Finish %s races in the rain"),           "cloud-showers-heavy",
      Metric::RainRaces,      Unit::Count,   { 1, 5, 15, 25 },                  false, false },
    { "big_grids",      N_("Crowd Surfer"),     Group::Racing, N_("Finish a race on a 20+ rider grid"),
      N_("Finish %s races on a 20+ rider grid"),    "people-group",
      Metric::BigGridRaces, Unit::Count, { 1, 10, 50, 150 },              false, false },
    // The first SPLIT off the gate, whichever index that is, and both the title
    // and the sentence say so: the game never reports its holeshot marker
    // (handlers/opening_line.h), so this is what the row can honestly measure,
    // and the "-ish" stays until it does. Charger's row counts "from the first
    // split" - the same line, same words.
    { "holeshots",      N_("Holeshot-ish"),     Group::Racing, N_("Lead at the first split"),
      N_("Lead at the first split in %s races"),  "right-from-bracket",
      Metric::Exploration,  Unit::Count,   { 1, 10, 50, 150 },                false, false,
      Exploration::Signal::Holeshots },
    { "charger",        N_("Charger"),          Group::Racing, nullptr,
      // "up ON the first split" read as a place on track rather than the
      // position held there; "from" says which end the count starts at, and
      // "climb" is the shorter verb that keeps the row inside its budget.
      N_("Climb %s places from the first split"), "caret-up",
      Metric::Exploration, Unit::Count, { 5, 10, 15, 20 }, false, false,
      Exploration::Signal::PositionsGained },
    { "last_lap_pass",  N_("Last Gasp"),        Group::Racing, N_("Pass on the last lap"),
      N_("Pass on the last lap in %s races"),       "circle-arrow-up",
      Metric::Exploration,  Unit::Count,   { 1, 10, 50, 150 },                false, false,
      Exploration::Signal::LastLapPasses },
    // "races of attrition" only meant anything to someone who had read the
    // bronze line first, and the bronze line was missing a word. One shape for
    // all four. The 6-starter floor below is still not in the sentence - it
    // does not fit - so a four-rider lobby losing one still looks like a miss.
    { "survivor",       N_("Survivor"),         Group::Racing, N_("Finish a race where a quarter quit"),
      N_("Finish %s races where a quarter quit"),  "star-of-life",
      Metric::Exploration,  Unit::Count,   { 1, 5, 15, 40 },                  false, false,
      Exploration::Signal::Survivals },
    { "roost",          N_("Roost"),            Group::Racing, nullptr,
      N_("Ride %s in a rider's wake"),             "wind",
      Metric::Exploration,  Unit::Seconds, { 300, 1800, 7200, 18000 },        false, false,
      Exploration::Signal::RoostSec },
    // ---- Consistency: the streaks ------------------------------------------
    { "clean_races",    N_("Rubber Side Down"), Group::Consistency, N_("Finish a race without crashing"),
      N_("Finish %s races without crashing"),      "shield",
      Metric::CleanRaces,    Unit::Count,   { 1, 5, 15, 25 },                 true, false },
    { "penalty_free",   N_("Clean Sheet"),      Group::Consistency, nullptr,
      N_("Go %s races in a row penalty-free"), "clipboard-check",
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
    { "wire_to_wire",   N_("Wire to Wire"),     Group::Consistency, N_("Win a race leading every lap"),
      N_("Win %s races leading every lap"),        "arrow-right",
      Metric::Exploration, Unit::Count, { 1, 5, 10, 25 }, false, false,
      Exploration::Signal::WireToWire },
    // The four things a race can be won with, all in the same race: the
    // holeshot, the lead on every lap, the fastest lap of it, and the flag.
    // Each is already its own row (Holeshot, Wire to Wire, Hot Lapper,
    // Winner); this is the one race they happened together. LISTED and counted,
    // unlike its Hidden neighbours, because every part of it is something a
    // rider can set out to do - which is the line this page draws.
    { "perfect_race",   N_("Perfect Race"),     Group::Consistency,
      N_("Holeshot, every lap led, fastest lap, win"),
      nullptr,                                 "hat-wizard",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::PerfectRace, 1 },
    { "clean_laps",     N_("Spotless"),         Group::Consistency, nullptr,
      // "clean" meant nothing on its own, and the obvious guess - no crashes -
      // is wrong: onLapCompleted takes crashes AND penalties. The slash is what
      // keeps the row inside its budget, since "crash- and penalty-free" is 52
      // on its own; dropping "in a row" would make a STREAK read as a running
      // total. The ladder ends at FIFTY because the streak breaks on a single
      // spill: a hundred and fifty consecutive clean laps is several flawless
      // hours, which is a bar nobody was going to clear twice by accident.
      N_("%s laps in a row, crash/penalty free"),   "hand-sparkles",
      Metric::Exploration,  Unit::Count,   { 5, 15, 30, 50 },                 true, false,
      Exploration::Signal::CleanLapStreak },
    { "crashes",        N_("Skill Issue"),      Group::Misfortune, nullptr,
      N_("Crash %s times in total"),               "user-injured",
      Metric::Crashes,    Unit::Count,   { 1000, 0, 0, 0 },                  true, false,
      Exploration::Signal::None, 1, true },
    { "penalties",      N_("Rule Bender"),      Group::Misfortune, nullptr,
      N_("Collect %s penalties"),                  "triangle-exclamation",
      Metric::Penalties,      Unit::Count,   { 100, 0, 0, 0 },                   false, false,
      Exploration::Signal::None, 1, true, "@Froxy" },
    { "penalty_time",   N_("Time Served"),      Group::Misfortune, nullptr,
      N_("Serve %s of penalty time"),              "hourglass",
      Metric::PenaltyTimeSec, Unit::Seconds, { 600, 0, 0, 0 },                   false, false,
      Exploration::Signal::None, 1, true },
    // ---- Progress: variety and improvement ----------------------------------
    { "tracks",         N_("Globetrotter"),     Group::Variety, nullptr,
      N_("Ride %s different tracks"),              "globe",
      Metric::Tracks,         Unit::Count,   { 5, 25, 50, 100 },                  false, false },
    { "bikes",          N_("Collector"),        Group::Variety, nullptr,
      N_("Ride %s different bikes"),               "gem",
      Metric::Bikes,          Unit::Count,   { 3, 10, 25, 50 },                  false, false },
    { "personal_bests", N_("Personal Best"),    Group::Variety, N_("Set a personal best"),
      N_("Set %s personal bests"),                 "star",
      Metric::PersonalBests,  Unit::Count,   { 1, 10, 100, 500 },                false, false },
    { "pb_leaps",       N_("Leap Forward"),     Group::Variety, N_("Beat a PB by a second or more"),
      N_("Beat %s PBs by a second or more"), "arrow-trend-up",
      Metric::PbLeaps, Unit::Count, { 1, 10, 50, 100 },                false, false },
    // ---- Freestyle: FMX totals. A trick counts when its CHAIN completes
    // (FmxManager::completeTrick), the same rule the FMX HUD's session totals
    // follow, so a chain that ends in a crash earns nothing here either.
    { "fmx_tricks",     N_("Show-off"),         Group::Freestyle, nullptr,
      N_("Land %s freestyle tricks"),              "wand-magic-sparkles",
      Metric::FmxTricks,   Unit::Count,   { 10, 100, 1000, 10000 },           false, true },
    { "fmx_points",     N_("Style Points"),     Group::Freestyle, nullptr,
      N_("Score %s FMX points"),          "certificate",
      Metric::FmxScore,    Unit::Count,   { 1000, 10000, 100000, 500000 },   false, true },
    { "fmx_chain",      N_("Chain Reaction"),   Group::Freestyle, nullptr,
      N_("Score %s points in one chain"),          "link",
      Metric::FmxChainScore, Unit::Count, { 250, 750, 1500, 2500 },        false, true },
    { "fmx_kinds",      N_("Trick Collector"),  Group::Freestyle, nullptr,
      N_("Land %s kinds of freestyle trick"),      "list-check",
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
    { "air_miles",      N_("Frequent Flyer"),   Group::Air, nullptr,
      N_("Spend %s in the air"),                   "cloud",
      Metric::Exploration, Unit::Seconds, { 7200, 54000, 180000, 360000 }, false, true,
      Exploration::Signal::AirtimeSec },
    { "air_distance",   N_("Air Miles"),        Group::Air, nullptr,
      N_("Cover %s through the air"),              "suitcase",
      Metric::Exploration, Unit::Km,    { 1, 10, 50, 150 },                    false, true,
      Exploration::Signal::AirDistanceKm },
    // The third measurement, summed: every metre of climb, banked at touchdown
    // like the two above it. PLACEHOLDER THRESHOLDS: nobody has reported what a
    // season's worth of climb actually comes to, so these are a first cut, to be
    // re-cut once real telemetry says what the numbers look like.
    { "air_height",     N_("To the Moon"),      Group::Air, nullptr,
      N_("Climb %s in jumps"),                     "rocket",
      Metric::Exploration, Unit::Km,    { 1, 5, 20, 50 },                      false, true,
      Exploration::Signal::AirHeightKm },
    // WAS Metric::FmxAirtimeSec, the longest airborne TRICK - so a five-second
    // step-down you simply rode out earned nothing, while the row said "in one
    // jump". It reads the flight detector now, like the five rows around it,
    // which is what the sentence always claimed. The stored value restarts
    // (it is a different measurement); earned tiers are kept, as tiers only rise.
    { "fmx_airtime",    N_("Hang Time"),        Group::Air, nullptr,
      N_("Stay airborne %s and land it"),          "parachute-box",
      Metric::Exploration, Unit::SecondsTenths, { 2, 3, 4, 5 },        false, true,
      Exploration::Signal::LongestFlightSec },
    // RE-CUT: a big track jump clears the 60 m this used to top out at.
    { "jump_distance",  N_("Gap Jumper"),       Group::Air, nullptr,
      N_("Land a %s jump"),                        "ruler-horizontal",
      Metric::Exploration, Unit::Metres, { 10, 30, 60, 100 },                  false, true,
      Exploration::Signal::JumpDistanceM },
    // The flight itself, measured at every landing you RIDE AWAY FROM
    // (FmxManager::updateFlight): how high above where it left the ground, how
    // far across it, and how far in total. Casing one out credits nothing. The
    // HEIGHT is still a guess: nothing has reported what a big one comes to.
    { "jump_height",    N_("Sent It"),          Group::Air, nullptr,
      N_("Reach %s above takeoff and land it"),    "ruler-vertical",
      Metric::Exploration, Unit::Metres, { 5, 10, 18, 25 },                    false, true,
      Exploration::Signal::JumpHeightM },
    // ---- Tricks: the named ones ---------------------------------------------
    { "fmx_backflips",  N_("Flipper"),          Group::Tricks, N_("Land a backflip"),
      N_("Land %s backflips"),                     "arrow-rotate-left",
      Metric::FmxBackflips,   Unit::Count,   { 1, 10, 50, 100 },               false, true,
      Exploration::Signal::None, TIER_COUNT, false, "@Lynds" },
    // "Hold a wheelie for 30s" never said the part that decides it: the duration
    // only counts once the chain BANKS, so a wheelie ridden into a crash is
    // worth nothing. Same verb as its endo counterpart, so the pair reads as one
    // idea - hold it, then put it down.
    { "fmx_wheelie",    N_("Wheelie King"),     Group::Tricks, nullptr,
      N_("Land a %s wheelie"),                     "wheelie",
      Metric::FmxWheelieSec,  Unit::SecondsTenths, { 5, 10, 20, 30 },            false, true },
    { "fmx_wheelie_km", N_("One-Wheel Tour"),   Group::Freestyle, nullptr,
      N_("Ride %s on the back wheel"),             "route",
      Metric::FmxWheelieKm, Unit::Km,    { 1, 10, 50, 100 },               false, true },
    { "fmx_whips",      N_("Whip It"),          Group::Tricks, nullptr,
      N_("Land %s whips"),                         "whip-right",
      Metric::FmxWhips,       Unit::Count,   { 10, 100, 500, 2500 },             false, true },
    { "fmx_scrubs",     N_("Scrubber"),         Group::Tricks, nullptr,
      N_("Land %s scrubs"),                        "arrow-right-arrow-left",
      Metric::FmxScrubs,      Unit::Count,   { 10, 100, 500, 2500 },             false, true },
    { "fmx_oppos",      N_("Oppo"),             Group::Tricks, N_("Land an oppo"),
      N_("Land %s oppos"),                         "turn-up",
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
    // CHEESABLE ON PURPOSE -- it measures the front wheel carrying the bike, so a
    // rear wheel parked on a wall holds it forever, and that is fine: a HUD's
    // list is not a ranked competition. Police at classification if ever needed.
    { "fmx_endos",      N_("Endo"),             Group::Tricks, nullptr,
      N_("Land a %s endo or stoppie"),             "endo",
      Metric::FmxEndoSec, Unit::SecondsTenths, { 3, 0, 0, 0 },                   false, true,
      Exploration::Signal::None, 1 },
    { "fmx_turn_downs", N_("Turn Down"),        Group::Tricks, N_("Land a turn down"),
      N_("Land %s turn downs"),                    "turn-down",
      Metric::FmxTurnDowns,   Unit::Count,   { 1, 10, 100, 500 },                false, true },
    // ---- The one that fires on demand, and the file-and-build rows -----------
    { "breakout",       N_("Brick Breaker"),    Group::Plugin, nullptr,
      // A NOUN PHRASE where its neighbours are instructions, and it has to be:
      // the credit is part of the rendered sentence, so "Breakout score 5,000
      // (@BennyP118)" already lands on exactly 47 beside "4,999 / 5,000". Every
      // verb-led form ("Score %s at Breakout") is three over. Leave it.
      N_("Breakout score %s"),                  "cubes",
      Metric::Breakout,     Unit::Count,   { 5000, 0, 0, 0 },                  false, false,
      Exploration::Signal::None, 1, false, "@BennyP118" },
    // The one that fires on demand. RELOAD_CONFIG is a legitimate thing to
    // count -- tinkering is a real way to use this plugin -- and it is also the
    // one achievement a fresh install can earn on purpose in ten seconds, which
    // is what lets a player (and a developer) see that toasts work at all.
    { "config_reloads", N_("Tinkerer"),         Group::Tinkering, N_("Reload the config"),
      N_("Reload the config %s times"),            "wrench",
      Metric::ConfigReloads, Unit::Count, { 1, 0, 0, 0 },                 false, false,
      Exploration::Signal::None, 1 },
    // ---- Racing extras the callbacks already carry --------------------------
    { "photo_finish",   N_("Photo Finish"),     Group::Racing, N_("Win a race by under a tenth"),
      nullptr,                                 "camera",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::PhotoFinish, 1 },
    // Photo Finish from the other side of the line. Same margin, same source
    // (FinishMargin), and deliberately NOT the same row: winning by a tenth and
    // losing by one are not the same thing to the person it happened to.
    { "so_close",       N_("So Close"),         Group::Misfortune, N_("Lose a race by under a tenth"),
      nullptr,                                 "poo",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::SoClose, 1, true },
    { "back_marker",    N_("Back Marker"),      Group::Misfortune, N_("Finish a race at least a lap down"),
      nullptr,                                 "road-barrier",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::BackMarker, 1, true },
    { "metronome",      N_("Metronome"),        Group::Consistency, N_("Five laps in a row, all within a tenth"),
      nullptr,                                 "equals",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::Metronome, 1 },
    // The chain that got away: the points an unfinished chain was worth when
    // it was lost (Chain Reaction's ladder, so the two rows read together).
    // 2,500 is Chain Reaction's Platinum: the chain you lost has to have been
    // worth the best one anybody banks.
    { "chain_lost",     N_("Weakest Link"),     Group::Misfortune, nullptr,
      N_("Crash out of a %s-point chain"),         "link-slash",
      Metric::Exploration, Unit::Count, { 2500, 0, 0, 0 }, false, true,
      Exploration::Signal::ChainLost, 1, true },
    { "tyre_shredder",  N_("Tyre Shredder"),    Group::Freestyle, nullptr,
      // Spelling out what shredding IS - burnouts, donuts and drifts, which is
      // exactly what ShredSec counts - has never fitted the 47-character row
      // once the seconds column takes its 17, and "min" took two more. Two of
      // the three name it well enough; the title carries the rest.
      N_("%s in burnouts and drifts"),      "tornado",
      Metric::Exploration, Unit::Seconds, { 60, 600, 1800, 3600 }, false, true,
      Exploration::Signal::ShredSec },
    // ---- Plugin: exploring what it can do, and the community nods ----------
    // Nods carry a placeholder title until the people they thank are named.
    { "tyre_kicker",    N_("Tyre Kicker"),      Group::Plugin, N_("Try each HUD and widget"),
      nullptr,                                 "magnifying-glass",
      Metric::Exploration, Unit::Count, { 100, 0, 0, 0 }, false, false,
      Exploration::Signal::HudsTried, 1, false, "@BrinkleyPT" },
    { "grand_tour",     N_("Grand Tour"),       Group::Plugin, N_("Open every settings tab"),
      nullptr,                                 "compass",
      Metric::Exploration, Unit::Count, { 100, 0, 0, 0 }, false, false,
      Exploration::Signal::TabsVisited, 1 },
    // ONE-SHOTS, both: overriding a colour and installing a pack are things you
    // either did or did not, and a ladder of them only asked a player who had
    // already made the plugin theirs to keep going for the sake of the bar.
    { "decorator",      N_("Interior Decorator"), Group::Tinkering, N_("Override a colour, font or theme"),
      nullptr,                                 "palette",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::Customisations, 1 },
    { "dress_up",       N_("Made It Yours"),    Group::Tinkering, N_("Install a pack of any type"),
      nullptr,                                 "shirt",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::CustomPacks, 1, false, "@bh5o" },
    { "on_air",         N_("On Air"),           Group::Plugin, N_("Connect the web overlay"),
      nullptr,                     "tower-broadcast",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::OverlayConnections, 1, false, "@Special Ed" },
    { "second_screen",  N_("Second Screen"),    Group::Plugin, N_("Show the HUD on a second screen"),
      nullptr,                                 "display",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::CompanionOpens, 1 },
    { "directors_cut",  N_("Director's Cut"),   Group::Plugin, nullptr,
      // Verbless, like Loyal: naming the feature costs more than the verb does.
      N_("Watch %s director cuts"),                "clapperboard",
      Metric::Exploration, Unit::Count, { 500, 0, 0, 0 }, false, false,
      Exploration::Signal::DirectorCuts, 1, false, "@Tallon214" },
    { "backseat",       N_("Backseat Driver"),  Group::Plugin, nullptr,
      N_("Hear %s spotter calls"),                 "headset",
      Metric::Exploration, Unit::Count, { 500, 0, 0, 0 }, false, false,
      Exploration::Signal::SpotterCallouts, 1, false, "@Foreign" },
    // The SETTING, not hours behind it. The hours version could not be earned
    // at all by a player whose pad the plugin does not see - which is how it
    // reached a stream and stayed locked all night. That is also why it was
    // hidden, and why it no longer is: asking only for the switch makes it a
    // thing a player can go and do, which is what the listed pages are for.
    { "good_vibrations", N_("Good Vibrations"), Group::Plugin, N_("Turn rumble on"),
      nullptr,                                 "wave-square",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::RumbleOn, 1, false, "@Debra" },
    { "keymaster",      N_("Keymaster"),        Group::Plugin, N_("Bind a hotkey"),
      nullptr,                                 "keyboard",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::HotkeysBound, 1, false, "@Vegi" },
    { "profile_hopper", N_("Profile Hopper"),   Group::Plugin, nullptr,
      N_("Switch profiles %s times"),              "shuffle",
      Metric::Exploration, Unit::Count, { 100, 0, 0, 0 }, false, false,
      Exploration::Signal::ProfileSwitches, 1 },
    { "fresh_coat",     N_("Fresh Coat"),       Group::Plugin, N_("Install an update in-game"),
      nullptr,                                 "download",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::UpdatesInstalled, 1 },
    { "split_decision", N_("Split Decision"),   Group::Plugin, N_("Time a segment"),
      nullptr,                                 "scissors",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::Segments, 1, false, "@ZealousidealWin823" },
    // Six hours, not a hundred: this counts time NOT riding, and a hundred
    // hours of it asked players to leave the game running to earn a badge -
    // which is the shape of grind every other row here avoids.
    { "armchair",       N_("Armchair Marshal"), Group::Plugin, nullptr,
      N_("Spectate or replay for %s"),     "couch",
      Metric::Exploration, Unit::Hours, { 6, 0, 0, 0 }, false, false,
      Exploration::Signal::SpectateHours, 1 },
    { "regular",        N_("Regular"),          Group::Variety, nullptr,
      N_("Show up on %s different days"),             "calendar-days",
      Metric::Exploration, Unit::Count, { 7, 30, 100, 365 }, false, false,
      Exploration::Signal::DaysUsed },
    { "day_streak",     N_("On a Roll"),        Group::Variety, nullptr,
      N_("Show up %s days in a row"),              "chart-line",
      Metric::Exploration, Unit::Count, { 3, 4, 5, 7 }, false, false,
      Exploration::Signal::DayStreak },
    { "servers",        N_("Well Travelled"),   Group::Variety, nullptr,
      N_("Join %s different servers"),             "server",
      Metric::Exploration, Unit::Count, { 5, 10, 25, 50 }, false, false,
      Exploration::Signal::Servers },
    { "bike_classes",   N_("Class Act"),        Group::Variety, nullptr,
      N_("Ride bikes from %s classes"),            "layer-group",
      Metric::BikeClasses, Unit::Count, { 2, 3, 4, 5 }, false, false,
      Exploration::Signal::None, TIER_COUNT, false, "@Aiden" },
    // Replaces Version Hopper, which counted releases run and so measured our
    // cadence rather than anything the player chose. Opting into pre-releases is
    // a choice, and it is made once. Credit kept: same spirit, running the
    // bleeding edge.
    { "prerelease",     N_("Early Access"),     Group::Hidden, N_("Switch to the pre-release channel"),
      nullptr,                                 "code-branch",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::Prerelease, 1, true, "@Husk" },
    // 480 is the plugin's own performance target - the number run_perf.sh gates
    // against and BenchmarkWidget colours against - so it is the row, and the
    // steps below it were never the interesting part. The signal stays a
    // max-ever of frames in one second, which is what earns it.
    { "frame_perfect",  N_("Frame Perfect"),    Group::Hidden, nullptr,
      N_("Hit %sFPS"),                             "gauge-high",
      Metric::Exploration, Unit::Count, { 480, 0, 0, 0 }, false, false,
      Exploration::Signal::FramePerfect, 1, true, "@Mave" },
    { "stalker",        N_("Stalker"),          Group::Plugin, nullptr,
      N_("Track %s riders"),                       "eye",
      Metric::Exploration, Unit::Count, { 10, 0, 0, 0 }, false, false,
      Exploration::Signal::RidersTracked, 1, false, "@turkishmonk" },
    // ALWAYS LISTED, unlike Hidden and Misfortune: these are a dashboard, and a
    // dashboard that appears once you are already on it is no use to the player
    // deciding whether to bother. Four bars at 0% on a fresh install is the
    // honest picture. (An earlier cut hid them until their value left zero;
    // nothing does that now, and this comment used to claim it still did.)
    { "all_bronze",     N_("Bronze Sweep"),     Group::Completion, N_("Counted achievements earned"),
      nullptr,                                 "medal",
      Metric::CompletionBronze,   Unit::Count, { 100, 0, 0, 0 },              false, false,
      Exploration::Signal::None, 1, false },
    { "all_silver",     N_("Silver Sweep"),     Group::Completion, N_("Counted achievements at Silver or max"),
      nullptr,                                 "medal",
      Metric::CompletionSilver,   Unit::Count, { 100, 0, 0, 0 },              false, false,
      Exploration::Signal::None, 1, false },
    { "all_gold",       N_("Gold Sweep"),       Group::Completion, N_("Counted achievements at Gold or max"),
      nullptr,                                 "medal",
      Metric::CompletionGold,     Unit::Count, { 100, 0, 0, 0 },              false, false,
      Exploration::Signal::None, 1, false },
    { "all_platinum",   N_("Platinum Sweep"),   Group::Completion, N_("Counted achievements maxed out"),
      nullptr,                                 "medal",
      Metric::CompletionPlatinum, Unit::Count, { 100, 0, 0, 0 },              false, false,
      Exploration::Signal::None, 1, false },
    { "phoenix",        N_("Phoenix"),          Group::Hidden, N_("Come back after a crash to desktop"),
      nullptr,                                 "fire-flame-curved",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::Phoenix, 1, true },
    // NOT a second Skill Issue, though the two sit on the same page. Skill
    // Issue reads the lifetime total (Metric::Crashes, the Stats tab's
    // "Crashes"); this reads the CrashWidget's tally, which a streamer can
    // zero mid-run - so the sentence has to say counter, not total. Held as a
    // max-ever by raise(), so a reset keeps the progress already made but
    // never adds across one: 50, reset, 50 more stands at 50, not 100.
    { "ninety_nine",    N_("99 Problems"),      Group::Misfortune, N_("Crash counter hits 99"),
      nullptr,                                 "skull-crossbones",
      Metric::Exploration, Unit::Count, { 99, 0, 0, 0 }, true, false,
      Exploration::Signal::CrashTally99, 1, true, "@TheRealSliX" },
    { "test_pilot",     N_("Test Pilot"),       Group::Hidden, N_("Turn on an experimental setting"),
      nullptr,                                 "paper-plane",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::TestPilot, 1, true },
    { "under_hood",     N_("Under the Hood"),   Group::Hidden, N_("Edit the settings file by hand"),
      nullptr,                                 "file-pen",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::SettingsEdited, 1, true },
    { "backup_plan",    N_("Backup Plan"),      Group::Hidden, N_("Edit the stats file by hand"),
      nullptr,                                 "floppy-disk",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::StatsEdited, 1, true },
    { "stylist",        N_("Stylist"),          Group::Tinkering, N_("Style the web overlay with custom.css"),
      nullptr,                                 "paintbrush",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::Stylist, 1, false },
    { "homebrew",       N_("Homebrew"),         Group::Hidden, N_("Run a build you made yourself"),
      nullptr,                                 "beer-mug-empty",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::Homebrew, 1, true },
    { "ungrateful",     N_("Ungrateful"),       Group::Tinkering, N_("Turn achievement toasts off"),
      nullptr,                                 "bell-slash",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::Ungrateful, 1 },
    { "factory_fresh",  N_("Factory Fresh"),    Group::Tinkering, N_("Reset every setting to defaults"),
      nullptr,                                 "industry",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::FactoryFresh, 1 },
    { "developer",      N_("Developer"),        Group::Hidden, N_("Enable developer mode"),
      nullptr,                                 "code",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::Developer, 1, true },
    { "night_owl",      N_("Night Owl"),        Group::Hidden, N_("Ride between two and five in the morning"),
      nullptr,                                 "moon",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::NightOwl, 1, true },
    { "anniversary",    N_("Anniversary"),      Group::Hidden, N_("Ride on the anniversary of your first run"),
      nullptr,                                 "cake-candles",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::Anniversary, 1, true },
    { "sandbagger",     N_("Sandbagger"),       Group::Racing, N_("Set a PB on the last lap of a race"),
      nullptr,                                 "weight-hanging",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::Sandbagger, 1 },
    { "palindrome",     N_("Palindrome"),       Group::Hidden, N_("Set a lap time the same backwards"),
      nullptr,                                 "arrows-left-right",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::Palindrome, 1, true },
    { "deja_vu",        N_("Deja Vu"),          Group::Hidden, N_("Set two identical lap times in a session"),
      nullptr,                                 "clone",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::DejaVu, 1, true },
    // A one-shot on purpose: the joke lands on the first one, and a ladder
    // would read as an invitation to the next nine hundred and ninety-nine.
    { "rage_quit",      N_("Rage Quit"),        Group::Misfortune, N_("Retire from a race before the finish"),
      nullptr,                                 "door-open",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::RageQuits, 1, true },
    { "bakers_dozen",   N_("Baker's Dozen"),    Group::Misfortune, N_("Crash 13 times in one session"),
      nullptr,                                 "bread-slice",
      Metric::Exploration, Unit::Count, { 13, 0, 0, 0 }, true, false,
      Exploration::Signal::BakersDozen, 1, true },
    // Thirty minutes is what the threshold below has always awarded, though the
    // row and the signal comment both used to claim an hour. It does not say "in
    // a session" because it cannot span one: the run is per-session scratch, so
    // an unbroken half hour is inside a session by construction. It does not say
    // "clean" either -- that is the undefined word Spotless was rewritten to
    // stop using, and this row means the same thing Rubber Side Down means.
    { "steady_hands",   N_("Steady Hands"),     Group::Consistency, nullptr,
      N_("Ride %s without crashing"),              "hand",
      Metric::Exploration, Unit::Seconds, { 1800, 0, 0, 0 }, true, false,
      Exploration::Signal::SteadyHands, 1 },
    { "fumes",          N_("Running on Fumes"), Group::Racing, N_("Finish a race on 1% of a tank or less"),
      nullptr,                                 "water",
      Metric::Exploration, Unit::Count,    { 1, 0, 0, 0 },                     false, false,
      Exploration::Signal::Fumes, 1 },
    { "ran_dry",        N_("Long Walk Home"),   Group::Misfortune, N_("Run out of fuel out on track"),
      nullptr,                                 "person-hiking",
      Metric::Exploration, Unit::Count,    { 1, 0, 0, 0 },                     false, false,
      Exploration::Signal::RanDry, 1, true },
    { "peak_g",         N_("Big Hit"),          Group::Misfortune, nullptr,
      N_("Take a %sG hit"),                        "explosion",
      Metric::Exploration, Unit::Count,    { 35, 0, 0, 0 },                    false, false,
      Exploration::Signal::PeakG, 1, true },
    { "solo_race",      N_("Just Me Then"),     Group::Hidden, N_("Finish a race nobody else started"),
      nullptr,                                 "heart-crack",
      Metric::Exploration, Unit::Count,    { 1, 0, 0, 0 },                     false, false,
      Exploration::Signal::SoloRace, 1, true },
    { "mondays",        N_("Case of the Mondays"), Group::Misfortune, N_("Crash out of a chain of five tricks"),
      nullptr,                                 "face-tired",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, true,
      Exploration::Signal::ChainCrash, 1, true },
    // ---- The ones that read a race, or a rider, sideways --------------------
    // Perfect Race's opposite number, and Last Gasp's: the same final lap,
    // read from the rider it was taken FROM. Deferred with Last Gasp for the
    // same reason (the pair can still be a lap behind at the flag), so the two
    // are credited by one helper rather than two that could disagree.
    { "choke",          N_("Choke"),            Group::Misfortune,
      N_("Lead into the final lap and lose the race"),
      nullptr,                                 "face-sad-cry",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::Choke, 1, true },
    // The pace without the result. OFF THE PODIUM, not merely beaten: a
    // fastest lap in second is a race lost by a little, and this row is for
    // the one where the lap was the only thing that went right.
    { "consolation",    N_("Consolation Prize"), Group::Misfortune,
      N_("Fastest lap, and off the podium with it"),
      nullptr,                                 "thumbs-up",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::ConsolationPrize, 1, true },
    // Company on the floor. Counts whoever is down AROUND you at any point
    // while you are down too - the rider who lands on you a second later is as
    // much of a heap as the one you landed on - so it needs a crash state to
    // have any meaning. The sentence says "with", not "already": the row is
    // about the heap, not about who fell into it first.
    { "pile_up",        N_("Pile-Up"),          Group::Misfortune,
      N_("Go down with three others"),
      nullptr,                                 "truck-medical",
      Metric::Exploration, Unit::Count, { 3, 0, 0, 0 }, true, false,
      Exploration::Signal::PileUp, 1, true },
    // Pile-Up from the outside: the same heap, ridden past instead of joined.
    // A separate row rather than a second tier of one, because they are not
    // degrees of the same thing - one is a crash and the other is avoiding it -
    // and hidden rather than listed because it needs three strangers to fall
    // over near you, which is nothing to aim at. RIDE means moving here too.
    { "peace_out",      N_("Peace Out"),        Group::Hidden,
      N_("Ride through three riders down"),
      nullptr,                                 "hand-peace",
      Metric::Exploration, Unit::Count, { 3, 0, 0, 0 }, true, false,
      Exploration::Signal::PeaceOut, 1, true },
    // The same corner, three times running. A RUN, not a tally: three crashes
    // spread over an afternoon at one corner is a hard corner, three in a row
    // is a grudge.
    { "favorite_spot",  N_("Favorite Spot"),    Group::Misfortune,
      N_("Crash three times in a row in one place"),
      nullptr,                                 "campground",
      Metric::Exploration, Unit::Count, { 3, 0, 0, 0 }, true, false,
      Exploration::Signal::FavoriteSpot, 1, true },
    // Measured from the telemetry rather than off the Burnout trick, which
    // has its own INI switch: a row that vanished when someone turned a trick
    // off would be a bug nobody could see. Ten seconds of it, unbroken.
    { "digging",        N_("Digging a Hole"),   Group::Hidden, nullptr,
      N_("Spin the rear wheel in place for %s"),    "person-digging",
      Metric::Exploration, Unit::Seconds, { 10, 0, 0, 0 }, false, false,
      Exploration::Signal::DiggingSec, 1, true },
    // The bike rides on without you: off it, and it stays upright on both
    // wheels and keeps going (core/ghost_ride.h has the rule and the tape it
    // was cut from). Endo's three seconds, and as rare as that sounds.
    { "ghost_ride",     N_("Ghost Ride"),       Group::Hidden, nullptr,
      N_("Let the bike ride alone for %s"),       "ghost",
      Metric::Exploration, Unit::SecondsTenths, { 3, 0, 0, 0 }, true, false,
      Exploration::Signal::GhostRideSec, 1, true },
    // A QUARTER OF FLIPPER'S LADDER at every step (1 / 10 / 50 / 100 -> 1 / 3 /
    // 13 / 25, halves rounded up), because a frontflip is the harder way round
    // and nobody lands them at backflip rates. Tied to that row rather than
    // picked freely, so the two move together if Flipper is ever re-cut.
    { "fmx_frontflips", N_("Somersault"),       Group::Tricks, N_("Land a frontflip"),
      N_("Land %s frontflips"),                    "arrow-rotate-right",
      Metric::FmxFrontflips,  Unit::Count,   { 1, 3, 13, 25 },              false, true },
    // ---- Ten rows added together, each on the page it reads with -----------
    // Calibrated against the usage survey's tier shares for each row's nearest
    // sibling (Roost / Hot Lapper / Charger land at about 25 / 10 / 2 / <0.5 %
    // of installs): see the PR that added them for the per-row reasoning.
    //
    // Peace Out's rule, counted: every rider down within its ten metres, ridden
    // past upright at speed and not at your own crash, once per rider per
    // crash (core/slalom.h). Peace Out's three-at-once is held by a quarter of
    // installs, so riding past one is common; the ladder starts at 25.
    { "slalom",         N_("Slalom"),           Group::Consistency, nullptr,
      N_("Ride past %s fallen riders"),            "person-skiing",
      Metric::Exploration, Unit::Count, { 25, 100, 500, 1500 }, true, false,
      Exploration::Signal::Slalom },
    // The laps every other classified finisher is down on you, summed at the
    // flag: lapping one rider twice is two. Lapped the Field's lifetime ladder.
    { "rush_hour",      N_("Rush Hour"),        Group::Racing, nullptr,
      N_("Lap other riders %s times"),             "traffic-light",
      Metric::Exploration, Unit::Count, { 5, 25, 100, 500 }, false, false,
      Exploration::Signal::RushHour },
    // In the air, over a rider: within reach sideways and well above them
    // (core/leapfrog.h). Once per rider per flight.
    { "leapfrog",       N_("Leapfrog"),         Group::Air, N_("Jump over a rider"),
      N_("Jump over %s riders"),                   "frog",
      Metric::Exploration, Unit::Count, { 1, 5, 25, 100 }, false, true,
      Exploration::Signal::Leapfrog },
    // A clean lap with no flight in it (core/lap_purity.h). A flat track
    // earns it too; the sentence is the whole rule.
    { "grounded",       N_("Grounded"),         Group::Hidden, N_("A clean lap without leaving the ground"),
      nullptr,                                 "arrows-down-to-line",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, true,
      Exploration::Signal::Grounded, 1, true },
    // A clean lap without touching either brake.
    { "flat_out",       N_("Flat Out"),         Group::Hidden, N_("A clean lap without touching the brakes"),
      nullptr,                                 "gauge",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::FlatOut, 1, true },
    // A win with two crashes in the race behind it. Two, not three: win plus
    // three crashes would sit under Photo Finish's 0.4%.
    { "back_from_dead", N_("Back From the Dead"), Group::Racing, N_("Win a race after two or more crashes"),
      nullptr,                                 "heart-pulse",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, true, false,
      Exploration::Signal::BackFromTheDead, 1 },
    // First to the opening split, last at the flag, in a real field.
    { "fall_from_grace", N_("Fall From Grace"), Group::Misfortune, N_("Lead at the first split, then finish last"),
      nullptr,                                 "arrow-trend-down",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::FallFromGrace, 1, true },
    // BEST laps, not any laps: any of your laps matching any of theirs to the
    // millisecond turns up in about a third of full-grid races.
    { "photocopy",      N_("Photocopy"),        Group::Hidden, N_("Your best lap equals a rival's, to the ms"),
      nullptr,                                 "copy",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::Photocopy, 1, true },
    // A 25 m jump landed under 10 g and ridden away from: the sentence is the
    // whole rule (core/feather_touch.h has the recording it was cut from).
    { "feather_touch",  N_("Feather Touch"),    Group::Air, nullptr,
      N_("Land %s jumps of 25m under 10G"),              "feather",
      Metric::Exploration, Unit::Count, { 10, 100, 500, 2500 }, false, true,
      Exploration::Signal::FeatherTouch },
    // Next to the same rider at the flag, three times in one game launch. The
    // names are held in memory only and never written to the stats file.
    { "nemesis",        N_("Nemesis"),          Group::Racing, N_("Finish beside a rival thrice in a sitting"),
      nullptr,                                 "user-ninja",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::Nemesis, 1 },
};

constexpr int COUNT = static_cast<int>(sizeof(kCatalogue) / sizeof(kCatalogue[0]));

inline bool isOneShot(const Entry& e) { return e.tierCount <= 1; }

// Tier 0 is "not yet"; 1..TIER_COUNT are the metals.
inline const char* tierName(int tier) {
    switch (tier) {
        case 1: return N_("Bronze");
        case 2: return N_("Silver");
        case 3: return N_("Gold");
        case 4: return N_("Platinum");
        default: return N_("Locked");
    }
}

// The entry with this id, or nullptr: linear, on load and in Settings, never per frame.
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
    if (isOneShot(e)) return tier > 0 ? N_("Earned") : N_("Locked");
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
