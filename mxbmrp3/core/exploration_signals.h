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
    TabsVisited,          // count: distinct settings tabs opened
    Customisations,       // max-ever: theme, colour and font choices in effect
    CustomPacks,          // max-ever: user packs installed, any type
    CustomGamepad,        // flag: a user gamepad pack
    CustomSpotter,        // flag: a user spotter pack
    CustomBoard,          // flag: a user pit board or gauges pack
    CustomTheme,          // flag: a user panel theme
    Stylist,              // flag: a custom.css with a rule in it
    OverlayConnections,   // count: web overlay SSE connections
    CompanionOpens,       // count: companion window opened
    DirectorCuts,         // count: auto-director camera cuts
    SpotterCallouts,      // count: spotter utterances
    RumbleHours,          // sum: hours ridden with rumble on (enabled, a controller connected)
    HotkeysBound,         // max-ever: hotkeys with a binding
    ProfileSwitches,      // count: manual profile switches
    UpdatesInstalled,     // count: in-game updater installs
    Segments,             // count: custom timing segments completed
    SpectateHours,        // sum: hours spent spectating or in replays
    DaysUsed,             // count: distinct local days seen (a startup, or the clock read once a minute)
    VersionsRun,          // count: distinct plugin versions started
    FramePerfect,         // max-ever: draws in one second (480 is the row)
    RidersTracked,        // count: riders put on the tracked list
    TestPilot,            // flag: an experimental setting switched on
    // ---- Racing extras ----------------------------------------------------
    PositionsGained,      // max-ever: places gained from lap one to the flag
    WireToWire,           // count: races led on every lap
    LappedField,          // count: wins with every rider a lap down
    Metronome,            // count: runs of five laps within a tenth
    AirtimeSec,           // sum: seconds airborne in landed tricks
    ShredSec,             // sum: seconds in burnouts, donuts and drifts
    // ---- Hidden -----------------------------------------------------------
    Phoenix,              // flag: started after a crash-to-desktop dump
    CrashTally99,         // max-ever: the crash tally (99 is the row)
    SettingsEdited,       // flag: the settings file changed outside the game
    StatsEdited,          // flag: the stats file changed outside the game
    Homebrew,             // flag: a build without analytics keys
    SignedCopy,           // flag: a build whose title was changed
    Ungrateful,           // flag: achievement toasts turned off
    FactoryFresh,         // flag: full reset to defaults
    Developer,            // flag: developer mode on
    NightOwl,             // flag: riding between 02:00 and 05:00 local (session start, or the minute's clock)
    Anniversary,          // flag: a session on the first run's anniversary
    PhotoFinish,          // flag: a win by under a tenth
    Sandbagger,           // flag: a PB on the last lap of a race
    Palindrome,           // flag: a lap time that reads the same backwards
    DejaVu,               // flag: two identical lap times in one session
    RageQuits,            // count: races left before the finish
    BackMarker,           // max-ever: laps down at a race's end (three is the row)
    BakersDozen,          // max-ever: crashes in one session (thirteen is the row)
    SteadyHands,          // max-ever: seconds of riding in one session without a crash (an hour is the row; pits pause it)
    ChainCrash,           // flag: a chain of five tricks ended by a crash
    ChainLost,            // max-ever: points an unfinished chain was worth when it was lost
    DayStreak,            // max-ever: consecutive local days shown up on (Regular's days, in a row)
    Servers,              // count: distinct server names ridden on
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
    { Signal::CustomGamepad,      "customGamepad" },
    { Signal::CustomSpotter,      "customSpotter" },
    { Signal::CustomBoard,        "customBoard" },
    { Signal::CustomTheme,        "customTheme" },
    { Signal::Stylist,            "stylist" },
    { Signal::OverlayConnections, "overlayConnections" },
    { Signal::CompanionOpens,     "companionOpens" },
    { Signal::DirectorCuts,       "directorCuts" },
    { Signal::SpotterCallouts,    "spotterCallouts" },
    { Signal::RumbleHours,        "rumbleHours" },
    { Signal::HotkeysBound,       "hotkeysBound" },
    { Signal::ProfileSwitches,    "profileSwitches" },
    { Signal::UpdatesInstalled,   "updatesInstalled" },
    { Signal::Segments,           "segments" },
    { Signal::SpectateHours,      "spectateHours" },
    { Signal::DaysUsed,           "daysUsed" },
    { Signal::VersionsRun,        "versionsRun" },
    { Signal::FramePerfect,       "framePerfect" },
    { Signal::RidersTracked,      "ridersTracked" },
    { Signal::TestPilot,          "testPilot" },
    { Signal::PositionsGained,    "positionsGained" },
    { Signal::WireToWire,         "wireToWire" },
    { Signal::LappedField,        "lappedField" },
    { Signal::Metronome,          "metronome" },
    { Signal::AirtimeSec,         "airtimeSec" },
    { Signal::ShredSec,           "shredSec" },
    { Signal::Phoenix,            "phoenix" },
    { Signal::CrashTally99,       "crashTally99" },
    { Signal::SettingsEdited,     "settingsEdited" },
    { Signal::StatsEdited,        "statsEdited" },
    { Signal::Homebrew,           "homebrew" },
    { Signal::SignedCopy,         "signedCopy" },
    { Signal::Ungrateful,         "ungrateful" },
    { Signal::FactoryFresh,       "factoryFresh" },
    { Signal::Developer,          "developer" },
    { Signal::NightOwl,           "nightOwl" },
    { Signal::Anniversary,        "anniversary" },
    { Signal::PhotoFinish,        "photoFinish" },
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
};

constexpr int SIGNAL_COUNT = static_cast<int>(Signal::COUNT);
static_assert(sizeof(kSignals) / sizeof(kSignals[0]) == SIGNAL_COUNT - 1,
              "every Signal but None needs a kSignals row, in enum order");

inline const char* signalKey(Signal s) {
    const int i = static_cast<int>(s) - 1;
    return (i >= 0 && i < SIGNAL_COUNT - 1) ? kSignals[i].key : "";
}

}  // namespace Exploration
