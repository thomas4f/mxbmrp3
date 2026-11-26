// ============================================================================
// core/achievement_types.h
// The VOCABULARY the achievement catalogue is written in: which number feeds a
// row (Metric), the page it sits on (Group), how its value reads (Unit), and
// the row itself (Entry). Split out of achievements.h, which keeps the table
// and the tier arithmetic over it, when the table outgrew its file budget -
// the table is one array by design, so the types moved instead.
//
// Header-only and dependency-free for the same reason as the catalogue: the
// unit suite compiles the real types (tests/unit/test_achievements.cpp).
// ============================================================================
#pragma once

#include "exploration_signals.h"

#include <cstdint>

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
    MaxSessionKm,      // the longest distance ridden in one session (Marathon)
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
        case Group::Plugin:    return "mxbmrp3";  // name-literal: group label, player-visible text
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
// Entries the settings tab's scrolling list shows at once: it sets the list's
// height (settings_tab_achievements.cpp, its only reader).
// Raising it makes the tab taller, which settings_fit_test / theme_geometry_test
// gate against the shared panel height.
constexpr int ENTRIES_PER_PAGE = 6;
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
    // Words for each tier's threshold, used IN PLACE of the formatted number
    // in the sentence: Marathon's "Ride a half marathon in one session" rather
    // than "Ride 21km ...". One N_() phrase per tier (tierCount of them); the
    // progress text still shows the number, so the words only have to name it.
    const char* const* tierPhrases = nullptr;
};

}  // namespace Achievements
