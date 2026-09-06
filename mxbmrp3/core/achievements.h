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
// time are counted the same way as wins: some players find "Frequent Flyer"
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
    FmxAirtimeSec,
    FmxWheelieSec,
    FmxWheelieKm,
    FmxChainScore,
    FmxKinds,
    MaxSessionHours,
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
    // Read from ExplorationStats by the row's `signal` (exploration_signals.h):
    // the one metric many rows share, each naming its own signal.
    Exploration,
    // Tiers earned across every other listed row, as a percentage (the
    // Completionist row; computed, never stored).
    Completion,
    COUNT
};

// The page an entry sits on in the settings tab: one group per page (or per
// six-row chunk of a group), in this order, so related rows are read together.
enum class Group : uint8_t {
    Mileage = 0,
    Racing,
    Conduct,
    Moments,      // one race or session's feat, glorious or not, as against a lifetime tally
    Progress,
    Freestyle,
    Tricks,       // the named ones: flips, wheelies, burnouts
    Plugin,       // "mxbmrp3": exploring the plugin itself, and the community nods
    Tinkering,    // the plugin's own files, builds and switches
    Hidden,       // unlisted until earned, then shown on this page
    COUNT
};

inline const char* groupName(Group g) {
    switch (g) {
        case Group::Mileage:   return "Mileage";
        case Group::Racing:    return "Racing";
        case Group::Conduct:   return "Conduct";
        case Group::Moments:   return "Moments";
        case Group::Progress:  return "Progress";
        case Group::Freestyle: return "Freestyle";
        case Group::Tricks:    return "Tricks";
        case Group::Plugin:    return "mxbmrp3";
        case Group::Tinkering: return "Tinkering";
        case Group::Hidden:    return "Hidden";
        default:               return "";
    }
}

// How a value of that metric is written for a human: "1,234", "1,234 km",
// "12 h" / "3.5 h", "1h 05m" / "45s", and "2.7s" for the short durations a
// tenth matters in (airtime, a wheelie).
enum class Unit : uint8_t { Count, Km, Hours, Seconds, SecondsTenths };

constexpr int TIER_COUNT = 4;
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
      Metric::DistanceKm,     Unit::Km,      { 100, 1000, 10000, 50000 },        false, false },
    { "ride_time",      "Seat Time",        Group::Mileage, nullptr,
      "Spend %s on track",                     "clock",
      Metric::RideTimeHours,  Unit::Hours,   { 10, 100, 500, 1000 },             false, false },
    { "laps",           "Lap Counter",      Group::Mileage, nullptr,
      "Complete %s laps",                      "repeat",
      Metric::Laps,           Unit::Count,   { 100, 1000, 10000, 50000 },        false, false },
    { "gear_shifts",    "Shift Happens",    Group::Mileage, nullptr,
      "Shift %s times",                  "gear",
      Metric::GearShifts,     Unit::Count,   { 1000, 10000, 100000, 1000000 },   false, false },
    { "session_laps",   "Marathon",         Group::Mileage, nullptr,
      "Complete %s laps in one session",       "person-running",
      Metric::MaxSessionLaps, Unit::Count, { 25, 50, 75, 100 },             false, false },
    { "session_time",   "Iron Butt",        Group::Mileage, nullptr,
      "Ride %s in one session",                "motorcycle",
      Metric::MaxSessionHours, Unit::Hours,  { 1, 2, 4, 8 },                     false, false,
      Exploration::Signal::None, TIER_COUNT, false, "@soulberg3" },
    { "track_laps",     "Local Hero",       Group::Mileage, nullptr,
      "Complete %s laps at one track",         "location-dot",
      Metric::MaxTrackLaps,   Unit::Count,   { 50, 250, 1000, 5000 },            false, false },
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
      "Finish on the podium %s times",         "medal",
      Metric::Podiums,        Unit::Count,   { 1, 25, 100, 300 },                false, false },
    { "fastest_laps",   "Hot Lapper",       Group::Racing, "Set the fastest lap of a race",
      "Set the fastest race lap %s times",     "fire",
      Metric::FastestLaps,  Unit::Count,   { 1, 10, 50, 200 },                false, false },
    // Race-side extras the callbacks already carry: the weather at the finish
    // and the size of the grid.
    { "rain_races",     "Mudder",           Group::Racing, "Finish a race in the rain",
      "Finish %s races in the rain",           "cloud-showers-heavy",
      Metric::RainRaces,      Unit::Count,   { 1, 5, 25, 50 },                  false, false },
    { "big_grids",      "Crowd Surfer",     Group::Racing, "Finish a race with 20+ riders",
      "Finish %s races with 20+ riders",       "people-group",
      Metric::BigGridRaces, Unit::Count, { 1, 10, 50, 150 },              false, false },
    // ---- Conduct: clean and otherwise ---------------------------------------
    { "clean_races",    "Rubber Side Down", Group::Conduct, "Finish a race without crashing",
      "Finish %s races without crashing",      "shield",
      Metric::CleanRaces,    Unit::Count,   { 1, 10, 50, 250 },                 true, false },
    { "penalty_free",   "Clean Sheet",      Group::Conduct, nullptr,
      "Go %s races in a row penalty-free", "clipboard-check",
      Metric::PenaltyFreeStreak, Unit::Count, { 5, 10, 25, 100 },      false, false },
    { "crashes",        "Frequent Flyer",   Group::Conduct, "Crash once",
      "Crash %s times",                        "user-injured",
      Metric::Crashes,    Unit::Count,   { 1, 100, 1000, 10000 },            true, false },
    { "penalties",      "Rule Bender",      Group::Conduct, "Collect a penalty",
      "Collect %s penalties",                  "triangle-exclamation",
      Metric::Penalties,      Unit::Count,   { 1, 10, 100, 1000 },               false, false,
      Exploration::Signal::None, TIER_COUNT, false, "@Froxy" },
    { "penalty_time",   "Time Served",      Group::Conduct, nullptr,
      "Serve %s of penalty time",              "hourglass",
      Metric::PenaltyTimeSec, Unit::Seconds, { 10, 60, 600, 3600 },              false, false },
    // ---- Progress: variety and improvement ----------------------------------
    { "tracks",         "Globetrotter",     Group::Progress, nullptr,
      "Ride %s different tracks",              "globe",
      Metric::Tracks,         Unit::Count,   { 5, 10, 25, 50 },                  false, false },
    { "bikes",          "Collector",        Group::Progress, nullptr,
      "Ride %s different bikes",               "gem",
      Metric::Bikes,          Unit::Count,   { 3, 10, 25, 50 },                  false, false },
    { "personal_bests", "Personal Best",    Group::Progress, "Set a personal best",
      "Set %s personal bests",                 "star",
      Metric::PersonalBests,  Unit::Count,   { 1, 10, 100, 500 },                false, false },
    { "pb_leaps",       "Leap Forward",     Group::Progress, "Beat a personal best by a second or more",
      "Beat %s PBs by a second or more", "arrow-trend-up",
      Metric::PbLeaps, Unit::Count, { 1, 10, 50, 100 },                false, false },
    // ---- Freestyle: FMX totals. A trick counts when its CHAIN completes
    // (FmxManager::completeTrick), the same rule the FMX HUD's session totals
    // follow, so a chain that ends in a crash earns nothing here either.
    { "fmx_tricks",     "Show-off",         Group::Freestyle, nullptr,
      "Land %s tricks",                        "wand-magic-sparkles",
      Metric::FmxTricks,   Unit::Count,   { 10, 100, 1000, 10000 },           false, true },
    { "fmx_points",     "Style Points",     Group::Freestyle, nullptr,
      "Score %s FMX points",          "certificate",
      Metric::FmxScore,    Unit::Count,   { 1000, 10000, 100000, 500000 },   false, true },
    { "fmx_chain",      "Chain Reaction",   Group::Freestyle, nullptr,
      "Score %s points in one chain",          "link",
      Metric::FmxChainScore, Unit::Count, { 250, 1000, 2500, 5000 },        false, true },
    { "fmx_kinds",      "Trick Collector",  Group::Freestyle, nullptr,
      "Land %s different kinds of trick",      "list-check",
      Metric::FmxKinds, Unit::Count, { 4, 8, 12, 15 },                 false, true },
    { "fmx_airtime",    "Hang Time",        Group::Freestyle, nullptr,
      "Stay airborne for %s in one jump",      "rocket",
      Metric::FmxAirtimeSec, Unit::SecondsTenths, { 2, 3, 4, 5 },      false, true },
    // ---- Tricks: the named ones ---------------------------------------------
    { "fmx_backflips",  "Flipper",          Group::Tricks, "Land a backflip",
      "Land %s backflips",                     "arrow-rotate-left",
      Metric::FmxBackflips,   Unit::Count,   { 1, 10, 100, 500 },               false, true,
      Exploration::Signal::None, TIER_COUNT, false, "@Lynds" },
    { "fmx_wheelie",    "Wheelie King",     Group::Tricks, nullptr,
      "Hold a wheelie for %s",                 "wheelie",
      Metric::FmxWheelieSec,  Unit::SecondsTenths, { 5, 10, 20, 30 },            false, true },
    { "fmx_wheelie_km", "One-Wheel Tour",   Group::Tricks, nullptr,
      "Ride %s on the back wheel",             "route",
      Metric::FmxWheelieKm, Unit::Km,    { 1, 10, 50, 200 },               false, true },
    { "fmx_whips",      "Whip It",          Group::Tricks, "Land a whip",
      "Land %s whips",                         "whip-right",
      Metric::FmxWhips,       Unit::Count,   { 10, 100, 500, 2500 },             false, true },
    { "fmx_scrubs",     "Scrubber",         Group::Tricks, "Land a scrub",
      "Land %s scrubs",                        "arrow-right-arrow-left",
      Metric::FmxScrubs,      Unit::Count,   { 10, 100, 500, 2500 },             false, true },
    { "fmx_oppos",      "Oppo",             Group::Tricks, "Land an oppo",
      "Land %s oppos",                         "turn-up",
      Metric::FmxOppos,       Unit::Count,   { 1, 10, 100, 500 },                false, true },
    { "fmx_turn_downs", "Turn Down",        Group::Tricks, "Land a turn down",
      "Land %s turn downs",                    "turn-down",
      Metric::FmxTurnDowns,   Unit::Count,   { 1, 10, 100, 500 },                false, true },
    // ---- The one that fires on demand, and the file-and-build rows -----------
    { "breakout",       "Brick Breaker",    Group::Plugin, nullptr,
      "Breakout score %s",                  "cubes",
      Metric::Breakout,     Unit::Count,   { 100, 500, 2000, 5000 },           false, false,
      Exploration::Signal::None, TIER_COUNT, false, "@BennyP118" },
    // The one that fires on demand. RELOAD_CONFIG is a legitimate thing to
    // count -- tinkering is a real way to use this plugin -- and it is also the
    // one achievement a fresh install can earn on purpose in ten seconds, which
    // is what lets a player (and a developer) see that toasts work at all.
    { "config_reloads", "Tinkerer",         Group::Tinkering, "Reload the config",
      "Reload the config %s times",            "wrench",
      Metric::ConfigReloads, Unit::Count, { 1, 10, 100, 500 },            false, false },
    // ---- Racing extras the callbacks already carry --------------------------
    { "charger",        "Charger",          Group::Moments, nullptr,
      "Gain %s places in one race",            "caret-up",
      Metric::Exploration, Unit::Count, { 5, 10, 15, 20 }, false, false,
      Exploration::Signal::PositionsGained },
    { "wire_to_wire",   "Wire to Wire",     Group::Moments, "Lead every lap of a race",
      "Lead every lap of %s races",            "ranking-star",
      Metric::Exploration, Unit::Count, { 1, 10, 50, 250 }, false, false,
      Exploration::Signal::WireToWire },
    { "lapped_field",   "Lapped the Field", Group::Moments, "Win with every rider a lap down",
      "Win %s races lapping the field", "crown",
      Metric::Exploration, Unit::Count, { 1, 5, 25, 100 }, false, false,
      Exploration::Signal::LappedField },
    { "metronome",      "Metronome",        Group::Moments, "Five laps within a tenth of each other",
      nullptr,                                 "equals",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::Metronome, 1 },
    { "air_miles",      "Air Miles",        Group::Freestyle, nullptr,
      "Spend %s in the air",                   "plane-up",
      Metric::Exploration, Unit::Seconds, { 60, 600, 1800, 3600 }, false, true,
      Exploration::Signal::AirtimeSec },
    // The chain that got away: the points an unfinished chain was worth when
    // it was lost (Chain Reaction's ladder, so the two rows read together).
    { "chain_lost",     "Weakest Link",     Group::Freestyle, nullptr,
      "Lose a chain worth %s points",          "link-slash",
      Metric::Exploration, Unit::Count, { 250, 1000, 2500, 5000 }, false, true,
      Exploration::Signal::ChainLost },
    { "tyre_shredder",  "Tyre Shredder",    Group::Tricks, nullptr,
      "Shred tyres for %s", "tornado",
      Metric::Exploration, Unit::Seconds, { 60, 600, 1800, 3600 }, false, true,
      Exploration::Signal::ShredSec },
    // ---- Plugin: exploring what it can do, and the community nods ----------
    // Nods carry a placeholder title until the people they thank are named.
    { "tyre_kicker",    "Tyre Kicker",      Group::Plugin, nullptr,
      "Try %s% of the HUDs", "list-check",
      Metric::Exploration, Unit::Count, { 25, 50, 75, 100 }, false, false,
      Exploration::Signal::HudsTried, TIER_COUNT, false, "@BrinkleyPT" },
    { "grand_tour",     "Grand Tour",       Group::Plugin, nullptr,
      "Open %s settings tabs",                 "compass",
      Metric::Exploration, Unit::Count, { 5, 10, 15, 20 }, false, false,
      Exploration::Signal::TabsVisited },
    { "decorator",      "Interior Decorator", Group::Plugin, "Customise the look",
      "Customise the look %s ways",            "palette",
      Metric::Exploration, Unit::Count, { 1, 3, 6, 10 }, false, false,
      Exploration::Signal::Customisations },
    { "dress_up",       "Dress-up",         Group::Plugin, "Install a custom pack",
      "Install %s custom packs",               "shirt",
      Metric::Exploration, Unit::Count, { 1, 2, 3, 5 }, false, false,
      Exploration::Signal::CustomPacks },
    { "pad_painter",    "Pad Painter",      Group::Plugin, "Install a custom gamepad pack",
      nullptr,                                 "gamepad",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::CustomGamepad, 1 },
    { "voice_actor",    "Voice Actor",      Group::Plugin, "Install a custom spotter pack",
      nullptr,                                 "microphone-lines",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::CustomSpotter, 1 },
    { "sign_writer",    "Sign Writer",      Group::Plugin, "Install a pit board or gauges pack",
      nullptr,                                 "pen-ruler",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::CustomBoard, 1, false, "@bh5o" },
    { "skin_deep",      "Skin Deep",        Group::Plugin, "Install a custom panel theme",
      nullptr,                                 "swatchbook",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::CustomTheme, 1 },
    { "stylist",        "Stylist",          Group::Plugin, "Style the web overlay with custom.css",
      nullptr,                                 "paintbrush",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::Stylist, 1 },
    { "on_air",         "On Air",           Group::Plugin, "Connect the web overlay",
      "On air %s times",      "tower-broadcast",
      Metric::Exploration, Unit::Count, { 1, 10, 50, 250 }, false, false,
      Exploration::Signal::OverlayConnections, TIER_COUNT, false, "@Special Ed" },
    { "second_screen",  "Second Screen",    Group::Plugin, "Open the companion window",
      "Open a second screen %s times",    "display",
      Metric::Exploration, Unit::Count, { 1, 10, 50, 250 }, false, false,
      Exploration::Signal::CompanionOpens },
    { "directors_cut",  "Director's Cut",   Group::Plugin, nullptr,
      "Watch %s cuts",         "clapperboard",
      Metric::Exploration, Unit::Count, { 10, 100, 1000, 10000 }, false, false,
      Exploration::Signal::DirectorCuts, TIER_COUNT, false, "@Tallon214" },
    { "backseat",       "Backseat Driver",  Group::Plugin, nullptr,
      "Hear %s cues",              "headset",
      Metric::Exploration, Unit::Count, { 100, 1000, 10000, 100000 }, false, false,
      Exploration::Signal::SpotterCallouts, TIER_COUNT, false, "@Foreign" },
    { "good_vibrations", "Good Vibrations", Group::Plugin, nullptr,
      "Rumble on for %s",                "wave-square",
      Metric::Exploration, Unit::Hours, { 1, 10, 50, 100 }, false, false,
      Exploration::Signal::RumbleHours, TIER_COUNT, false, "@Debra" },
    { "keymaster",      "Keymaster",        Group::Plugin, nullptr,
      "Bind %s hotkeys",                       "keyboard",
      Metric::Exploration, Unit::Count, { 3, 6, 10, 15 }, false, false,
      Exploration::Signal::HotkeysBound, TIER_COUNT, false, "@Vegi" },
    { "profile_hopper", "Profile Hopper",   Group::Plugin, nullptr,
      "Switch profiles %s times",              "shuffle",
      Metric::Exploration, Unit::Count, { 10, 100, 1000, 10000 }, false, false,
      Exploration::Signal::ProfileSwitches },
    { "fresh_coat",     "Fresh Coat",       Group::Plugin, "Install an update in-game",
      "Install %s updates in-game",            "download",
      Metric::Exploration, Unit::Count, { 1, 5, 25, 100 }, false, false,
      Exploration::Signal::UpdatesInstalled },
    { "split_decision", "Split Decision",   Group::Plugin, "Time a segment",
      "%s splits",               "scissors",
      Metric::Exploration, Unit::Count, { 1, 10, 100, 500 }, false, false,
      Exploration::Signal::Segments, TIER_COUNT, false, "@ZealousidealWin823" },
    { "armchair",       "Armchair Marshal", Group::Plugin, nullptr,
      "Spectate or replay for %s",     "couch",
      Metric::Exploration, Unit::Hours, { 1, 10, 50, 100 }, false, false,
      Exploration::Signal::SpectateHours },
    { "regular",        "Regular",          Group::Progress, nullptr,
      "Show up on %s different days",             "calendar-days",
      Metric::Exploration, Unit::Count, { 7, 30, 100, 365 }, false, false,
      Exploration::Signal::DaysUsed },
    { "day_streak",     "On a Roll",        Group::Progress, nullptr,
      "Show up %s days in a row",              "chart-line",
      Metric::Exploration, Unit::Count, { 3, 7, 14, 30 }, false, false,
      Exploration::Signal::DayStreak },
    { "servers",        "Well Travelled",   Group::Progress, nullptr,
      "Ride on %s different servers",          "server",
      Metric::Exploration, Unit::Count, { 5, 10, 25, 50 }, false, false,
      Exploration::Signal::Servers },
    { "bike_classes",   "Class Act",        Group::Progress, nullptr,
      "Ride bikes from %s classes",            "layer-group",
      Metric::BikeClasses, Unit::Count, { 2, 3, 4, 5 }, false, false,
      Exploration::Signal::None, TIER_COUNT, false, "@Aiden" },
    { "version_hopper", "Version Hopper",   Group::Plugin, nullptr,
      "Run %s plugin versions",                "code-branch",
      Metric::Exploration, Unit::Count, { 3, 10, 20, 30 }, false, false,
      Exploration::Signal::VersionsRun, TIER_COUNT, false, "@Husk" },
    { "frame_perfect",  "Frame Perfect",    Group::Plugin, "Hit 480 FPS",
      nullptr,                                 "gauge-high",
      Metric::Exploration, Unit::Count, { 480, 0, 0, 0 }, false, false,
      Exploration::Signal::FramePerfect, 1, false, "@Mave" },
    { "stalker",        "Stalker",          Group::Plugin, "Track a rider",
      "Track %s riders",                       "eye",
      Metric::Exploration, Unit::Count, { 1, 5, 25, 100 }, false, false,
      Exploration::Signal::RidersTracked, TIER_COUNT, false, "@turkishmonk" },
    { "completionist",  "Completionist",    Group::Plugin, nullptr,
      "Earn %s percent of all tiers",         "bullseye",
      Metric::Completion, Unit::Count, { 25, 50, 75, 100 }, false, false },
    // ---- Hidden: unlisted until earned. Only the ones that happen TO a rider
    // -- the surprises. Anything a player could set out to do is on a page.
    { "phoenix",        "Phoenix",          Group::Hidden, "Come back after a crash to desktop",
      nullptr,                                 "fire-flame-curved",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::Phoenix, 1, true },
    { "ninety_nine",    "99 Problems",      Group::Conduct, "Crash tally to 99",
      nullptr,                                 "skull-crossbones",
      Metric::Exploration, Unit::Count, { 99, 0, 0, 0 }, true, false,
      Exploration::Signal::CrashTally99, 1, false, "@TheRealSliX" },
    { "test_pilot",     "Test Pilot",       Group::Tinkering, "Turn on an experimental setting",
      nullptr,                                 "paper-plane",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::TestPilot, 1 },
    { "under_hood",     "Under the Hood",   Group::Tinkering, "Edit the settings file by hand",
      nullptr,                                 "file-pen",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::SettingsEdited, 1 },
    { "backup_plan",    "Backup Plan",      Group::Hidden, "Edit the stats file by hand",
      nullptr,                                 "floppy-disk",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::StatsEdited, 1, true },
    { "homebrew",       "Homebrew",         Group::Tinkering, "Run a build you made yourself",
      nullptr,                                 "beer-mug-empty",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::Homebrew, 1 },
    { "signed_copy",    "Signed Copy",      Group::Tinkering, "Run a build with a different name",
      nullptr,                                 "signature",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::SignedCopy, 1 },
    { "ungrateful",     "Ungrateful",       Group::Tinkering, "Turn achievement toasts off",
      nullptr,                                 "bell-slash",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::Ungrateful, 1 },
    { "factory_fresh",  "Factory Fresh",    Group::Tinkering, "Reset every setting to defaults",
      nullptr,                                 "industry",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::FactoryFresh, 1 },
    { "developer",      "Developer",        Group::Tinkering, "Enable developer mode",
      nullptr,                                 "code",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::Developer, 1 },
    { "night_owl",      "Night Owl",        Group::Hidden, "Ride between two and five in the morning",
      nullptr,                                 "moon",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::NightOwl, 1, true },
    { "anniversary",    "Anniversary",      Group::Hidden, "Ride on the anniversary of your first run",
      nullptr,                                 "cake-candles",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::Anniversary, 1, true },
    { "photo_finish",   "Photo Finish",     Group::Racing, "Win a race by under a tenth",
      nullptr,                                 "camera",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::PhotoFinish, 1 },
    { "sandbagger",     "Sandbagger",       Group::Moments, "Set a personal best on the last lap of a race",
      nullptr,                                 "weight-hanging",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::Sandbagger, 1 },
    { "palindrome",     "Palindrome",       Group::Hidden, "Set a lap time that reads the same backwards",
      nullptr,                                 "arrows-left-right",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::Palindrome, 1, true },
    { "deja_vu",        "Deja Vu",          Group::Hidden, "Set two identical lap times in one session",
      nullptr,                                 "clone",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::DejaVu, 1, true },
    // A one-shot on purpose: the joke lands on the first one, and a ladder
    // would read as an invitation to the next nine hundred and ninety-nine.
    { "rage_quit",      "Rage Quit",        Group::Conduct, "Leave a race before the finish",
      nullptr,                                 "door-open",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, false,
      Exploration::Signal::RageQuits, 1 },
    { "back_marker",    "Back Marker",      Group::Conduct, "Get lapped three times in one race",
      nullptr,                                 "person-walking",
      Metric::Exploration, Unit::Count, { 3, 0, 0, 0 }, false, false,
      Exploration::Signal::BackMarker, 1 },
    { "bakers_dozen",   "Baker's Dozen",    Group::Moments, "Crash thirteen times in one session",
      nullptr,                                 "bread-slice",
      Metric::Exploration, Unit::Count, { 13, 0, 0, 0 }, true, false,
      Exploration::Signal::BakersDozen, 1 },
    { "steady_hands",   "Steady Hands",     Group::Moments, "Ride an hour without crashing",
      nullptr,                                 "hand",
      Metric::Exploration, Unit::Seconds, { 3600, 0, 0, 0 }, true, false,
      Exploration::Signal::SteadyHands, 1 },
    { "mondays",        "Case of the Mondays", Group::Hidden, "Crash out of a chain of five tricks",
      nullptr,                                 "face-tired",
      Metric::Exploration, Unit::Count, { 1, 0, 0, 0 }, false, true,
      Exploration::Signal::ChainCrash, 1, true },
    // The flip nobody means to do: off the Tricks page so it stays at eight
    // rows, found by whoever lands one.
    { "fmx_frontflips", "Somersault",       Group::Hidden, "Land a frontflip",
      "Land %s frontflips",                    "arrow-rotate-right",
      Metric::FmxFrontflips,  Unit::Count,   { 1, 10, 100, 500 },               false, true,
      Exploration::Signal::None, TIER_COUNT, true },
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

// The entry with this id, or nullptr. Linear over ~17 rows; called on load and
// from the settings tab, never per frame.
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

// Bounded append: `text` onto the C string already in `buffer`, never past
// bufferSize, always terminated. The formatters below are built from this
// rather than from snprintf("%s%s") chains, which the compiler cannot prove
// fit and which -Wformat-truncation therefore rejects under -Werror.
inline void appendText(char* buffer, size_t bufferSize, const char* text) {
    if (bufferSize == 0 || !text) return;
    size_t len = std::strlen(buffer);
    if (len >= bufferSize) len = bufferSize - 1;
    for (; *text && len + 1 < bufferSize; ++text) buffer[len++] = *text;
    buffer[len] = '\0';
}

// "1234567" -> "1,234,567". Integer part only; `value` is truncated. Small
// helper the unit formatter below builds on.
// TRUNCATES, never rounds: the tier is decided by `value >= threshold`, and a
// display that rounded 999.6 km up would read "1,000 / 1,000 km" beside a
// still-locked Bronze. The same rule holds for the tenths below.
inline void formatWithCommas(double value, char* buffer, size_t bufferSize) {
    if (bufferSize == 0) return;
    if (value < 0.0) value = 0.0;
    char raw[32];
    snprintf(raw, sizeof(raw), "%.0f", std::floor(value));
    const int len = static_cast<int>(std::strlen(raw));
    size_t out = 0;
    for (int i = 0; i < len && out + 1 < bufferSize; ++i) {
        if (i > 0 && (len - i) % 3 == 0) {
            if (out + 2 >= bufferSize) break;
            buffer[out++] = ',';
        }
        buffer[out++] = raw[i];
    }
    buffer[out] = '\0';
}

// A metric value in its unit, for the description and the progress text.
// The epsilon absorbs the binary artifact of the x10 (2.3 * 10 is 22.999...),
// which would otherwise print a stored 2.3 as "2.2".
inline double truncTenths(double value) {
    return std::floor(value * 10.0 + 1e-6) / 10.0;
}

inline void formatValue(Unit unit, double value, char* buffer, size_t bufferSize) {
    if (bufferSize == 0) return;
    if (value < 0.0) value = 0.0;
    switch (unit) {
        case Unit::Count:
            formatWithCommas(value, buffer, bufferSize);
            return;
        case Unit::Km:
            if (value < 10.0) {
                snprintf(buffer, bufferSize, "%.1f", truncTenths(value));
            } else {
                formatWithCommas(value, buffer, bufferSize);
            }
            appendText(buffer, bufferSize, " km");
            return;
        case Unit::Hours:
            if (value < 10.0 && truncTenths(value) != static_cast<double>(static_cast<int>(value))) {
                snprintf(buffer, bufferSize, "%.1f", truncTenths(value));
            } else {
                formatWithCommas(value, buffer, bufferSize);
            }
            appendText(buffer, bufferSize, " h");
            return;
        case Unit::SecondsTenths:
            snprintf(buffer, bufferSize, "%.1fs", truncTenths(value));
            return;
        case Unit::Seconds: {
            const long long s = static_cast<long long>(value);
            if (s >= 3600) {
                snprintf(buffer, bufferSize, "%lldh %02lldm", s / 3600, (s % 3600) / 60);
            } else if (s >= 60) {
                snprintf(buffer, bufferSize, "%lldm %02llds", s / 60, s % 60);
            } else {
                snprintf(buffer, bufferSize, "%llds", s);
            }
            return;
        }
    }
    buffer[0] = '\0';
}

// "Finish 10 races" -- what reaching `tier` (1..TIER_COUNT) means, in words.
inline void formatDescription(const Entry& e, int tier, char* buffer, size_t bufferSize) {
    if (bufferSize == 0) return;
    if (tier < 1) tier = 1;
    if (tier > e.tierCount) tier = e.tierCount;
    const double threshold = e.thresholds[tier - 1];
    // The dedication rides at the end of the sentence, "(@Name)": one string,
    // on the tab's task row, the toast and the doc alike. The row budget
    // (TAB_ROW_CHARS) is what keeps the credited sentences short.
    auto credit = [&]() {
        if (!e.credit) return;
        appendText(buffer, bufferSize, " (");
        appendText(buffer, bufferSize, e.credit);
        appendText(buffer, bufferSize, ")");
    };
    if (e.descOne && (threshold == 1.0 || !e.descMany)) {
        snprintf(buffer, bufferSize, "%s", e.descOne);
        credit();
        return;
    }
    char value[32];
    formatValue(e.unit, threshold, value, sizeof(value));
    // The template's one %s, substituted by hand: a non-literal format string
    // is a warning on every compiler, and the bounded copy cannot overrun.
    const char* marker = std::strstr(e.descMany, "%s");
    buffer[0] = '\0';
    if (!marker) {
        appendText(buffer, bufferSize, e.descMany);
        credit();
        return;
    }
    const size_t prefixLen = static_cast<size_t>(marker - e.descMany);
    size_t out = 0;
    for (size_t i = 0; i < prefixLen && out + 1 < bufferSize; ++i) buffer[out++] = e.descMany[i];
    buffer[out] = '\0';
    appendText(buffer, bufferSize, value);
    appendText(buffer, bufferSize, marker + 2);
    credit();
}

// "340 / 1,000 km" -- the progress text beside the bar. Both halves share the
// unit, so it is written once on the right; a complete row shows the last
// threshold twice, which reads as "done" rather than as a bar with no goal.
inline void formatProgress(const Entry& e, double value, char* buffer, size_t bufferSize) {
    if (bufferSize == 0) return;
    // A one-shot has no count to show: the tag says Earned or Locked.
    if (isOneShot(e) && e.thresholds[0] == 1.0) { buffer[0] = '\0'; return; }
    // The current value is NEVER clamped to the target: past Platinum the row
    // keeps counting ("13,212 / 10,000 km"), which is the point of a lifetime
    // number. Only the bar saturates (progressFraction).
    const double target = targetThreshold(e, tierFor(e, value));
    char have[32];
    char want[32];
    formatValue(Unit::Count, value, have, sizeof(have));
    // Only the target carries the unit suffix; the current value borrows it --
    // at the unit's own resolution, so a row is seen to move: the first hour
    // in minutes ("24m / 1 h"), the first ten in tenths ("1.4 / 10 h"), a
    // kilometre in tenths ("0.4 / 1.0 km"). Whole hours used to print as a
    // count, which read as "0 / 1 h" for fifty-nine minutes.
    switch (e.unit) {
        case Unit::Count:
            formatValue(Unit::Count, target, want, sizeof(want));
            break;
        case Unit::Km:
            if (value < 10.0) snprintf(have, sizeof(have), "%.1f", truncTenths(value));
            formatValue(e.unit, target, want, sizeof(want));
            break;
        case Unit::Hours:
            if (value < 1.0) {
                snprintf(have, sizeof(have), "%dm", static_cast<int>(value * 60.0));
            } else if (value < 10.0) {
                snprintf(have, sizeof(have), "%.1f", truncTenths(value));
            }
            formatValue(e.unit, target, want, sizeof(want));
            break;
        case Unit::Seconds:
        case Unit::SecondsTenths:
            formatValue(e.unit, value, have, sizeof(have));
            formatValue(e.unit, target, want, sizeof(want));
            break;
    }
    buffer[0] = '\0';
    appendText(buffer, bufferSize, have);
    appendText(buffer, bufferSize, " / ");
    appendText(buffer, bufferSize, want);
}

}  // namespace Achievements
