// ============================================================================
// core/fmx_levels.h
// Trick LEVELS: how big one landed trick was, said the way riders say it -
// "Whip", "Big Whip", "Flat Whip". Pure (a TrickInstance in, a word out) so the
// unit suite pins every threshold and word (tests/unit/test_fmx_levels.cpp).
//
// TWO LEVELS ABOVE PLAIN. Level 1 is "Big" for every trick; level 2 is the
// word riders use for that trick at its limit where one exists and measures
// what we measure (a FLAT whip is ~90 degrees of yaw, a DEEP scrub is the bike
// laid down, an ENDLESS wheelie is the hold), else "Huge". Style words
// (filthy, steezy, proper) were turned down: the detector sees angle, time and
// height, never how good it looked.
//
// WHICH TRICKS. The partial-rotation air tricks, plain Air, and the balance
// tricks. Full rotations already have a level - Double/Triple/Quad from
// `multiplier` - so the two never stack on one name. Coaster Wheelie has none
// because "Endless Coaster Wheelie" (23) is longer than any name the trick
// stack's large row shows today ("Double Barrel Roll L", 20); the shredding
// tricks (burnout, donut, drift, pivot) have none by design.
//
// DISPLAY ONLY. Scoring never reads a level (fmx_scoring.h already scales by
// time, distance and rotation), so the Chain Reaction / Style Points ladders
// keep their meaning. The words stay English like the trick names themselves
// and "Double" (every pack's glossary keeps trick names English).
//
// A level is read LIVE off the active trick, so the HUD's name grows while the
// rider holds it ("Wheelie" -> "Big Wheelie"), the same way "Backflip" becomes
// "Double Backflip" mid-air. Measurements only rise during a trick (peaks,
// hold time, airtime), so a level never drops back.
// ============================================================================
#pragma once

#include "fmx_types.h"

#include <algorithm>
#include <cmath>

namespace Fmx {

constexpr int TRICK_LEVEL_COUNT = 2;  // above plain

// The thresholds, in the unit the trick is measured in. A value AT a threshold
// earns it. First cut: re-cut from the FMX debug log once players report.
struct TrickLevelRule {
    float big;            // level 1
    float top;            // level 2
    const char* topWord;  // level 2's word ("Flat", "Deep", "Endless", "Huge")
};

// The rule for a trick, or nullptr when the trick has no levels.
inline const TrickLevelRule* getTrickLevelRule(TrickType type) {
    static constexpr TrickLevelRule kWhip     = { 45.0f, 90.0f, "Flat" };     // degrees of yaw
    static constexpr TrickLevelRule kScrub    = { 45.0f, 75.0f, "Deep" };     // degrees of roll
    static constexpr TrickLevelRule kOppo     = { 90.0f, 150.0f, "Huge" };    // degrees of yaw
    static constexpr TrickLevelRule kWheelie  = { 5.0f, 20.0f, "Endless" };   // seconds held
    static constexpr TrickLevelRule kEndo     = { 1.0f, 3.0f, "Endless" };    // seconds held
    static constexpr TrickLevelRule kAir      = { 1.5f, 3.5f, "Huge" };       // seconds airborne
    switch (type) {
        case TrickType::WHIP_LEFT:
        case TrickType::WHIP_RIGHT:        return &kWhip;
        case TrickType::SCRUB_LEFT:
        case TrickType::SCRUB_RIGHT:       return &kScrub;
        case TrickType::OPPO_LEFT:
        case TrickType::OPPO_RIGHT:
        case TrickType::TURN_DOWN_LEFT:
        case TrickType::TURN_DOWN_RIGHT:   return &kOppo;
        case TrickType::WHEELIE:           return &kWheelie;
        case TrickType::ENDO:
        case TrickType::STOPPIE:           return &kEndo;
        case TrickType::AIR:               return &kAir;
        default:                           return nullptr;
    }
}

// The measurement a trick's level is read from.
//   Whip, Oppo, Turn Down: peak yaw.
//   Scrub: the deepest WORLD lean, the lip lean plus the roll added since:
//          `peakRoll` is roll accumulated from take-off, not a lean (the Flat
//          360 test sums the two the same way). 40 at the lip and 40 more is
//          80; 40 at the lip rolled back 60 is 20, so the lip lean alone
//          (40) is the deeper of the two there.
//   Wheelie, Endo, Stoppie: `duration`, the time held past the pitch threshold
//          (what Wheelie King and the Endo row measure).
//   Air: `airTime` from the lip (what Hang Time measures), not `duration`.
inline float getTrickLevelValue(const TrickInstance& trick) {
    switch (trick.type) {
        case TrickType::WHIP_LEFT:
        case TrickType::WHIP_RIGHT:
        case TrickType::OPPO_LEFT:
        case TrickType::OPPO_RIGHT:
        case TrickType::TURN_DOWN_LEFT:
        case TrickType::TURN_DOWN_RIGHT:
            return std::abs(trick.peakYaw);
        case TrickType::SCRUB_LEFT:
        case TrickType::SCRUB_RIGHT:
            return std::max(std::abs(trick.startRoll),
                            std::abs(trick.startRoll + trick.peakRoll));
        case TrickType::WHEELIE:
        case TrickType::ENDO:
        case TrickType::STOPPIE:
            return trick.duration;
        case TrickType::AIR:
            return trick.airTime;
        default:
            return 0.0f;
    }
}

// 0 = plain, 1 = Big, 2 = the trick's top word.
inline int getTrickLevel(const TrickInstance& trick) {
    const TrickLevelRule* rule = getTrickLevelRule(trick.type);
    if (!rule) return 0;
    const float v = getTrickLevelValue(trick);
    if (!std::isfinite(v)) return 0;
    if (v >= rule->top) return 2;
    if (v >= rule->big) return 1;
    return 0;
}

// The word in front of the name for a level, or nullptr for none.
inline const char* getTrickLevelWord(TrickType type, int level) {
    const TrickLevelRule* rule = getTrickLevelRule(type);
    if (!rule || level <= 0) return nullptr;
    return (level >= TRICK_LEVEL_COUNT) ? rule->topWord : "Big";
}

inline const char* getTrickLevelWord(const TrickInstance& trick) {
    return getTrickLevelWord(trick.type, getTrickLevel(trick));
}

} // namespace Fmx
