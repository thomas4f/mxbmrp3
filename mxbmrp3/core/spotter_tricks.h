// ============================================================================
// core/spotter_tricks.h
// What the spotter says about a freestyle trick, and WHICH tricks it says
// anything about. Pure (Fmx types in, words out) so the unit suite pins it
// (test_spotter_tricks.cpp); FmxManager reports the moments and
// SpotterManager::onTrick* turns them into the trick_* cues.
//
// THE FOUR MOMENTS, and where FmxManager has each one:
//   trick_started  the trick has a name worth saying AND is half done
//                  (kNameCallProgress). A GROUND trick has its name as soon
//                  as it classifies; an AIR trick does not: it starts
//                  nameless, is plain "Air" after 0.3 s, and only becomes a
//                  Backflip or a Spin once it has rotated far enough
//                  (fmx_types.h, FULL_ROTATION_MIN). So this is a mid-air call
//                  for those, which is what an announcer does anyway. It says
//                  "here it comes", not WHICH trick: that can still change.
//   trick_landed   the APPARENT landing: kLandingCallDelaySec into the landing
//                  grace (FmxManager's GRACE state), still upright. That is
//                  before the trick is confirmed, on purpose - a crash later
//                  in the grace window still follows the cheer with a boo.
//                  The one moment that NAMES the trick ({trick}).
//   trick_crashed  you went down with a trick or a chain live. No name either:
//                  a trick that never landed was never really that trick.
//   trick_combo    a chain of kComboMin or more banked.
//
// WHAT STAYS QUIET. Detection runs on every ride, races included, and on a
// motocross track every jump is an "Air" trick. So plain Air and Drift never
// speak (isCallable), and the cues never fire in a race session at all
// (SpotterManager::tricksAllowed): the category is for freeriding.
// ============================================================================
#pragma once

#include <cstring>
#include <string>

#include "fmx_levels.h"
#include "fmx_types.h"
#include "spotter_phrase.h"

namespace SpotterTricks {

// A chain this long is a combo worth calling out. Two is just two tricks.
constexpr int kComboMin = 3;

// How far into a trick (its progress, 0..1 of the HUD's bar) before its name
// is called. Not at first classification: a wheelie classifies the moment the
// front lifts, and every little pop became a "Wheelie!". Half the bar is a
// second held for a wheelie, and an air trick that classifies late - a
// backflip at 270 degrees - is already past it, so those are called at once.
constexpr float kNameCallProgress = 0.5f;

// How long after touching down before the landing is called. Touching down is
// not landing it: the hard crash comes a moment later as often as not, and a
// "stuck it!" before every one of those was the wrong joke. Still inside the
// 0.75 s grace, so the call comes before the trick is confirmed.
constexpr float kLandingCallDelaySec = 0.4f;

// The trick as said out loud: lowercase (the expander capitalises a line that
// opens with it), and WITHOUT the side. "Barrel roll" is the trick; "barrel
// roll left" is a detail a rider in the air neither needs nor has time for,
// and the HUD shows it anyway. nullptr for the tricks that are never said.
// The ENGLISH word, which is also the key a language's [Words] translates
// (SpotterPhrase::kWordKeys); spokenTrick is what says it.
inline const char* spokenName(Fmx::TrickType type) {
    using T = Fmx::TrickType;
    switch (type) {
        case T::WHEELIE:           return "wheelie";
        case T::COASTER_WHEELIE:   return "coaster wheelie";
        case T::ENDO:              return "endo";
        case T::STOPPIE:           return "stoppie";
        case T::BURNOUT:           return "burnout";
        case T::DONUT:             return "donut";
        case T::PIVOT_LEFT:
        case T::PIVOT_RIGHT:       return "pivot";
        case T::BACKFLIP:          return "backflip";
        case T::FRONTFLIP:         return "frontflip";
        case T::BARREL_ROLL_LEFT:
        case T::BARREL_ROLL_RIGHT: return "barrel roll";
        case T::SCRUB_LEFT:
        case T::SCRUB_RIGHT:       return "scrub";
        case T::WHIP_LEFT:
        case T::WHIP_RIGHT:        return "whip";
        case T::SPIN_LEFT:
        case T::SPIN_RIGHT:        return "spin";
        case T::OPPO_LEFT:
        case T::OPPO_RIGHT:        return "oppo";
        case T::TURN_DOWN_LEFT:
        case T::TURN_DOWN_RIGHT:   return "turn down";
        case T::FLAT_360_LEFT:
        case T::FLAT_360_RIGHT:    return "flat three sixty";
        // Plain Air and Drift are what riding a motocross track looks like
        // to the detector, and NONE is no trick at all.
        default:                   return nullptr;
    }
}

inline bool isCallable(Fmx::TrickType type) { return spokenName(type) != nullptr; }

// The level word as said out loud, lowercase like the name: "big" for level
// 1, the trick's own top word ("flat", "deep", "huge", "endless") for level 2,
// nullptr for none. Read off core/fmx_levels.h, so the spotter and the HUD
// can never disagree about how big a trick was; the English word is the
// [Words] key a language translates.
inline const char* spokenLevel(Fmx::TrickType type, int level) {
    const char* word = Fmx::getTrickLevelWord(type, level);
    if (!word) return nullptr;
    if (std::strcmp(word, "Big") == 0) return "big";
    if (std::strcmp(word, "Flat") == 0) return "flat";
    if (std::strcmp(word, "Deep") == 0) return "deep";
    if (std::strcmp(word, "Huge") == 0) return "huge";
    if (std::strcmp(word, "Endless") == 0) return "endless";
    return nullptr;   // a new top word stays unsaid until it is listed here
}

// The name with its multiplier or its level: 2 rotations is a "double
// backflip", a whip laid past 90 degrees a "flat whip". The two never meet on
// one trick (fmx_types.h, formatTrickName), and a level wins if they ever do,
// as it does on the HUD. Past four rotations the word stops mattering, and a
// rider who lands a quintuple anything has heard enough already. Empty for a
// trick that is never said, so a template's [bracketed group] around {trick}
// drops cleanly.
inline std::string spokenTrick(Fmx::TrickType type, int multiplier, int level = 0) {
    const char* name = spokenName(type);
    if (!name) return std::string();
    const char* prefix = spokenLevel(type, level);
    if (!prefix) {
        prefix = multiplier >= 4 ? "quad"
               : multiplier == 3 ? "triple"
               : multiplier == 2 ? "double"
               : nullptr;
    }
    const char* word = SpotterPhrase::say(name);
    if (!prefix) return word;
    return std::string(SpotterPhrase::say(prefix)) + " " + word;
}

}  // namespace SpotterTricks
