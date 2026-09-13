// ============================================================================
// core/exploration_signals.h
// The signals behind the "Plugin", "Hidden" and race-extra achievement rows:
// the things the plugin can notice about how it is being used (a tab opened,
// the overlay connecting, a build made at home) and the race facts no lifetime
// stat carries (a photo finish, a lap that reads the same backwards).
//
// ONE TABLE, ONE ROW EACH, ONE FEED CALL EACH. A signal is a name in the enum,
// a JSON key in kSignals, a catalogue row in achievements.h that reads it, and
// one call into ExplorationStats where the thing happens. Removing an
// achievement is deleting those four lines; test_achievements.cpp holds the
// enum and the table to the same length and every signal to exactly one row,
// so a half-removed one fails to build or fails the unit gate rather than
// lingering. Disabling one is its id in kDisabledIds (achievements.h), like
// any other row.
//
// Header-only and dependency-free so achievements.h (the pure catalogue) can
// name a signal per row.
// ============================================================================
#pragma once

#include <cstdint>

namespace Exploration {

enum class Signal : uint8_t {
    None = 0,
    // ---- Plugin: exploring what the plugin can do -------------------------
    HudsTried,            // max-ever: percent of the HUDs and widgets ever switched on
    TabsVisited,          // max-ever: percent of the settings tabs ever opened
    Customisations,       // max-ever: theme, colour and font choices in effect
    CustomPacks,          // max-ever: user packs installed, any type
    Stylist,              // flag: a custom.css with a rule in it
    OverlayConnections,   // count: web overlay SSE connections
    CompanionOpens,       // count: companion window opened
    DirectorCuts,         // count: auto-director camera cuts
    SpotterCallouts,      // count: spotter utterances
    RumbleOn,             // flag: rumble switched on in settings (a pad the plugin cannot see does not block it)
    HotkeysBound,         // max-ever: hotkeys with a binding
    ProfileSwitches,      // count: manual profile switches
    UpdatesInstalled,     // count: in-game updater installs
    Segments,             // count: custom timing segments completed
    SpectateHours,        // sum: hours spent spectating or in replays
    DaysUsed,             // count: distinct local days seen (a startup, or the clock read once a minute)
    Prerelease,           // flag: the update channel set to pre-release
    FramePerfect,         // max-ever: draws in one second (480 is the row)
    RidersTracked,        // count: riders put on the tracked list
    TestPilot,            // flag: an experimental setting switched on
    // ---- Racing extras ----------------------------------------------------
    PositionsGained,      // max-ever: places gained from lap one to the flag
    WireToWire,           // count: races led on every lap
    LappedField,          // count: wins with every rider a lap down
    Holeshots,            // count: first through the opening split after a gate drop
    LastLapPasses,        // count: races where a place was gained on the final lap
    Survivals,            // count: races finished where a quarter of the starters retired
    SoloRace,             // flag: finished a race that nobody else started
    PeakG,                // max-ever: hardest hit taken, in g (chassis-local, gravity included)
    CleanLapStreak,       // max-ever: consecutive laps with no crash and no penalty
    FuelBurnt,            // sum: litres of fuel burnt, every bike and session
    Fumes,                // flag: finished a race with the tank all but empty
    RanDry,               // flag: emptied the tank out on track
    DayRideHours,         // max-ever: hours ridden within one local day (Iron Butt)
    RoostSec,             // sum: seconds racing in another rider's roost
    Metronome,            // count: runs of five laps within a tenth
    AirtimeSec,           // sum: seconds airborne, EVERY flight - trick or not
    JumpHeightM,          // max-ever: metres above the altitude a jump took off at
    JumpDistanceM,        // max-ever: metres across the ground in one flight
    AirDistanceKm,        // sum: ground covered while airborne, over every flight
    ShredSec,             // sum: seconds in burnouts, donuts and drifts
    // ---- Hidden -----------------------------------------------------------
    Phoenix,              // flag: started after a crash-to-desktop dump
    CrashTally99,         // max-ever: the crash tally (99 is the row)
    SettingsEdited,       // flag: the settings file changed outside the game
    StatsEdited,          // flag: the stats file changed outside the game
    Homebrew,             // flag: a build without analytics keys
    Ungrateful,           // flag: achievement toasts turned off
    FactoryFresh,         // flag: full reset to defaults
    Developer,            // flag: developer mode on
    NightOwl,             // flag: riding between 02:00 and 05:00 local (session start, or the minute's clock)
    Anniversary,          // flag: a session on the first run's anniversary
    PhotoFinish,          // flag: a win by under a tenth
    SoClose,              // flag: a SECOND place by under a tenth - the same margin, lost
    LongestFlightSec,     // max-ever: the longest single landed flight (Hang Time)
    AirHeightKm,          // sum: height climbed over every flight, landed or not (To the Moon)
    Sandbagger,           // flag: a PB on the last lap of a race
    Palindrome,           // flag: a lap time that reads the same backwards
    DejaVu,               // flag: two identical lap times in one session
    RageQuits,            // count: races left before the finish
    BackMarker,           // max-ever: laps down at a race's end (three is the row)
    BakersDozen,          // max-ever: crashes in one session (thirteen is the row)
    SteadyHands,          // max-ever: seconds RIDDEN in one session without a crash (1800 = the row; pits and parking pause it)
    ChainCrash,           // flag: a chain of five tricks ended by a crash
    ChainLost,            // max-ever: points an unfinished chain was worth when it was lost
    DayStreak,            // max-ever: consecutive local days shown up on (Regular's days, in a row)
    Servers,              // count: distinct server names ridden on
    PerfectRace,          // flag: a win with the holeshot, every lap led and the fastest lap
    Choke,                // flag: led into the final lap and lost the race
    ConsolationPrize,     // flag: the fastest lap of a race finished off the podium
    PileUp,               // max-ever: riders down around you while you were down too (three is the row)
    PeaceOut,             // max-ever: riders down around you while you rode PAST, upright (three is the row)
    FavoriteSpot,         // max-ever: crashes in a row at the same place on track (three is the row)
    DiggingSec,           // max-ever: seconds of unbroken rear wheelspin standing still
    COUNT
};

// The JSON key each signal persists under, in enum order (None has none).
struct SignalInfo {
    Signal signal;
    const char* key;
};

constexpr SignalInfo kSignals[] = {
    { Signal::HudsTried,          "hudsTried" },
    { Signal::TabsVisited,        "tabsVisited" },
    { Signal::Customisations,     "customisations" },
    { Signal::CustomPacks,        "customPacks" },
    { Signal::Stylist,            "stylist" },
    { Signal::OverlayConnections, "overlayConnections" },
    { Signal::CompanionOpens,     "companionOpens" },
    { Signal::DirectorCuts,       "directorCuts" },
    { Signal::SpotterCallouts,    "spotterCallouts" },
    { Signal::RumbleOn,           "rumbleOn" },
    { Signal::HotkeysBound,       "hotkeysBound" },
    { Signal::ProfileSwitches,    "profileSwitches" },
    { Signal::UpdatesInstalled,   "updatesInstalled" },
    { Signal::Segments,           "segments" },
    { Signal::SpectateHours,      "spectateHours" },
    { Signal::DaysUsed,           "daysUsed" },
    { Signal::Prerelease,         "prerelease" },
    { Signal::FramePerfect,       "framePerfect" },
    { Signal::RidersTracked,      "ridersTracked" },
    { Signal::TestPilot,          "testPilot" },
    { Signal::PositionsGained,    "positionsGained" },
    { Signal::WireToWire,         "wireToWire" },
    { Signal::LappedField,        "lappedField" },
    { Signal::Holeshots,          "holeshots" },
    { Signal::LastLapPasses,      "lastLapPasses" },
    { Signal::Survivals,          "survivals" },
    { Signal::SoloRace,           "soloRace" },
    { Signal::PeakG,              "peakG" },
    { Signal::CleanLapStreak,     "cleanLapStreak" },
    { Signal::FuelBurnt,          "fuelBurnt" },
    { Signal::Fumes,              "fumes" },
    { Signal::RanDry,             "ranDry" },
    { Signal::DayRideHours,       "dayRideHours" },
    { Signal::RoostSec,           "roostSec" },
    { Signal::Metronome,          "metronome" },
    { Signal::AirtimeSec,         "airtimeSec" },
    { Signal::JumpHeightM,        "jumpHeightM" },
    { Signal::JumpDistanceM,      "jumpDistanceM" },
    { Signal::AirDistanceKm,      "airDistanceKm" },
    { Signal::ShredSec,           "shredSec" },
    { Signal::Phoenix,            "phoenix" },
    { Signal::CrashTally99,       "crashTally99" },
    { Signal::SettingsEdited,     "settingsEdited" },
    { Signal::StatsEdited,        "statsEdited" },
    { Signal::Homebrew,           "homebrew" },
    { Signal::Ungrateful,         "ungrateful" },
    { Signal::FactoryFresh,       "factoryFresh" },
    { Signal::Developer,          "developer" },
    { Signal::NightOwl,           "nightOwl" },
    { Signal::Anniversary,        "anniversary" },
    { Signal::PhotoFinish,        "photoFinish" },
    { Signal::SoClose,            "soClose" },
    { Signal::LongestFlightSec,   "longestFlightSec" },
    { Signal::AirHeightKm,        "airHeightKm" },
    { Signal::Sandbagger,         "sandbagger" },
    { Signal::Palindrome,         "palindrome" },
    { Signal::DejaVu,             "dejaVu" },
    { Signal::RageQuits,          "rageQuits" },
    { Signal::BackMarker,         "backMarker" },
    { Signal::BakersDozen,        "bakersDozen" },
    { Signal::SteadyHands,        "steadyHands" },
    { Signal::ChainCrash,         "chainCrash" },
    { Signal::ChainLost,          "chainLost" },
    { Signal::DayStreak,          "dayStreak" },
    { Signal::Servers,            "servers" },
    { Signal::PerfectRace,        "perfectRace" },
    { Signal::Choke,              "choke" },
    { Signal::ConsolationPrize,   "consolationPrize" },
    { Signal::PileUp,             "pileUp" },
    { Signal::PeaceOut,           "peaceOut" },
    { Signal::FavoriteSpot,       "favoriteSpot" },
    { Signal::DiggingSec,         "diggingSec" },
};

constexpr int SIGNAL_COUNT = static_cast<int>(Signal::COUNT);
static_assert(sizeof(kSignals) / sizeof(kSignals[0]) == SIGNAL_COUNT - 1,
              "every Signal but None needs a kSignals row, in enum order");

inline const char* signalKey(Signal s) {
    const int i = static_cast<int>(s) - 1;
    return (i >= 0 && i < SIGNAL_COUNT - 1) ? kSignals[i].key : "";
}

}  // namespace Exploration
