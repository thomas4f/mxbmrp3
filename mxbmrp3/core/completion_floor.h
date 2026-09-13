// ============================================================================
// core/completion_floor.h
// How much of the catalogue a player has taken to each metal, as a percentage
// per metal. Pure, header-only and free of AchievementManager so it can be
// unit-tested against the real catalogue with synthetic tiers - the same shape
// as finish_margin.h and roost_detect.h.
//
// FOUR NUMBERS, NOT ONE. This was a single row reporting the LOWEST tier
// reached across the catalogue, which is a floor: one row left at Bronze held
// the whole thing at Bronze, so the number sat at zero indefinitely and then
// jumped. It could not answer the only question worth asking of it - how far
// along am I - because a floor has no middle. A percentage per metal does: a
// quarter of the way to all-Bronze reads as 25, and the four rows move
// independently, which is what they are for.
//
// The completion rows are excluded from their own denominator. Counting
// themselves would mean All Bronze could not reach 100% until All Bronze was
// at Bronze, which is the tail-chase the old row avoided by being hidden (and
// so excluded for free). These are listed, so it has to be said out loud.
// ============================================================================
#pragma once

#include "achievements.h"

namespace Achievements {

// A row whose value is drawn from the others, and so cannot be one of them.
inline bool isCompletionRow(const Entry& e) {
    return e.metric == Metric::CompletionBronze || e.metric == Metric::CompletionSilver ||
           e.metric == Metric::CompletionGold   || e.metric == Metric::CompletionPlatinum;
}

// Which metal a Sweep row measures, 1-4, or 0 for every other row. The tab
// paints all four with one glyph and tells them apart by this, so the page
// reads as a single instrument with four coloured bars rather than four
// unrelated icons that happen to be near each other.
inline int completionMetal(const Entry& e) {
    switch (e.metric) {
        case Metric::CompletionBronze:   return 1;
        case Metric::CompletionSilver:   return 2;
        case Metric::CompletionGold:     return 3;
        case Metric::CompletionPlatinum: return TIER_COUNT;
        default:                         return 0;
    }
}

// rows: indices into kCatalogue, in tab order (the manager's own row list).
// storedTier(idx): the tier recorded for kCatalogue[idx], 0 = none.
// Returns 0..100: the share of LISTED rows standing at `metal` or better.
template <typename TierFn>
inline int completionPercentAt(const int* rows, int rowCount, int metal, TierFn storedTier) {
    int counted = 0, reached = 0;
    for (int r = 0; r < rowCount; ++r) {
        const int idx = rows[r];
        const Entry& e = kCatalogue[idx];
        // Hidden rows are not on the board a player can see, so they are not
        // part of "the catalogue" this measures - and a row nobody can aim at
        // would hold every one of these at 99% forever.
        if (!countsTowardCompletion(e.group)) continue;
        ++counted;
        // A row that has run out of tiers is FULL, whatever metal is asked of
        // it: a one-shot has no Silver to withhold, and counting it short would
        // peg All Silver below 100% for every player permanently.
        const int tier = storedTier(idx);
        if (tier >= metal || tier >= e.tierCount) ++reached;
    }
    return counted > 0 ? (reached * 100) / counted : 0;
}

}  // namespace Achievements
