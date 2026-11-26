// ============================================================================
// core/exploration_stats_feeds.cpp
// Counting for the November rows. Split from exploration_stats.cpp, which is
// near its line budget; same class, same private add()/mark()/changed().
// ============================================================================
#include "exploration_stats.h"

using Exploration::Signal;

void ExplorationStats::onRodePastDown(int riders) {
    if (riders <= 0) return;
    add(Signal::Slalom, riders);
    changed();
}

void ExplorationStats::onLeapfrog(int riders) {
    if (riders <= 0) return;
    add(Signal::Leapfrog, riders);
    changed();
}

void ExplorationStats::onFeatherTouch() {
    add(Signal::FeatherTouch, 1.0);
    changed();
}

void ExplorationStats::onPureLap(bool grounded, bool flatOut) {
    // add(), not mark(): one-shots, but the tally past one still reaches the
    // stats file for the usage survey (onRaceFinished has the why).
    if (grounded) add(Signal::Grounded, 1.0);
    if (flatOut) add(Signal::FlatOut, 1.0);
    if (grounded || flatOut) changed();
}

void ExplorationStats::onRaceExtras(const RaceExtras& race) {
    bool moved = false;
    if (race.lapsPutOn > 0) { add(Signal::RushHour, race.lapsPutOn); moved = true; }
    // Back From the Dead: the win, after going down twice in it. A field, so
    // a lobby of one is not a race to come back in.
    if (race.position == 1 && race.starters >= MIN_FIELD &&
        race.raceCrashes >= BACK_FROM_THE_DEAD_CRASHES) {
        add(Signal::BackFromTheDead, 1.0);
        moved = true;
    }
    // Fall From Grace: first to the opening split, last at the flag, with the
    // field the other race rows ask for (MIN_FIELD, unsaid in their sentences
    // too: it only keeps an empty server from handing it out).
    if (m_gotHoleshot && race.lastOfFinishers && race.starters >= MIN_FIELD) {
        add(Signal::FallFromGrace, 1.0);
        moved = true;
    }
    if (race.bestLapTwin) { add(Signal::Photocopy, 1.0); moved = true; }
    // Nemesis: the same rider beside you at the flag, three races this launch.
    const char* const sides[2] = { race.aheadName, race.behindName };
    for (const char* name : sides) {
        if (!name || !*name) continue;
        if (++m_nemesisRaces[name] == NEMESIS_RACES) { add(Signal::Nemesis, 1.0); moved = true; }
    }
    if (moved) changed();
}
