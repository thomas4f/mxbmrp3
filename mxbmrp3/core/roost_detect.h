// ============================================================================
// core/roost_detect.h
// Rider-to-rider proximity — the pure pairwise core behind Roost (sat in
// someone's spray) and Side by Side (genuinely wheel to wheel).
//
// WHY IT LIVES HERE AND NOT IN SpotterManager. The spotter already runs a
// proximity pass over the same batch, and reusing it was the obvious move and
// the wrong one: the spotter is a feature a player switches OFF, and an
// achievement that only counts while you have voice callouts enabled is not an
// achievement. Same reason FuelWidget did not get to own the fuel counting.
// It does not live in PluginData either, for blue_flag_detect.h's reason: the
// inputs are four numbers per rider, so pulling the loop out lets the unit
// suite drive it with a plain g++ and no game.
//
// WHAT IT MEASURES. World distance decides who is near - it is exact, and it
// needs no lap numbers, so a rider you are about to be lapped by counts the
// same as one on your lap. Centreline position (trackPos) decides only whether
// they are AHEAD of you or beside you, which is what separates sitting in
// someone's roost from racing them side by side. The two are mutually
// exclusive by construction: the along-track bands do not overlap.
//
// PERFORMANCE CONTRACT (this is on the ~30Hz RaceTrackPosition path with up to
// 50 riders):
//   - one linear pass over a CONTIGUOUS array the caller already owns. No map
//     lookups inside the loop - blue_flag_detect.h's header says what happened
//     the last time something on this path indexed a hash map per iteration.
//   - no allocation, no sqrt: the radii are compared squared.
//   - at most ONE rider is credited per tick. You cannot tailgate two people,
//     and summing over a pack would make a mid-field scrap worth more per
//     second than a battle for the lead.
// ============================================================================
#pragma once

#include <cmath>

namespace Roost {

// One rider's contribution, flattened from Unified::TrackPositionData. Kept
// game-type-free so the unit test needs no game headers.
struct Rider {
    float posX = 0.0f;      // metres, world
    float posZ = 0.0f;      // metres, world (Y is altitude; see the map convention)
    float trackPos = 0.0f;  // 0..1 along the centreline
};

// The bands, in metres. DELIBERATELY NOT in ProximityTuning beside the blue
// flag and hazard knobs, which the INI can move: those describe a track, this
// describes an achievement, and a threshold a player can edit is a threshold
// that means nothing. A struct rather than loose constants only so the unit
// test can sweep a band without rebuilding the caller.
struct Tuning {
    float sideRadiusM = 6.0f;    // world distance to count as alongside
    float sideAlongM = 3.0f;     // ...and how little of that may be fore/aft
    float roostNearM = 15.0f;    // world distance to be in the spray
    float roostMinAlongM = 3.0f; // closer along-track than this is alongside, not behind
    float roostMaxAlongM = 12.0f;// further ahead than this and the roost has fallen out
};

enum class Contact : unsigned char { None = 0, SideBySide, Roost };

// Signed along-track distance from `me` to `other` in metres: positive when the
// other rider is AHEAD. Wrapped to the shorter way round, so the start/finish
// line is not a cliff.
inline float alongTrackM(float myTrackPos, float otherTrackPos, float trackLengthM) {
    float delta = otherTrackPos - myTrackPos;
    if (delta > 0.5f) delta -= 1.0f;
    else if (delta < -0.5f) delta += 1.0f;
    return delta * trackLengthM;
}

// The closest qualifying rider's contact, or None. `others` may contain `me`
// itself - a rider is never their own contact, since a zero world distance with
// a zero along-track distance fails the side-by-side test's own lower bound
// only by luck, so the caller passes myIndex to skip it outright.
inline Contact classify(const Rider& me, const Rider* others, int count,
                        int myIndex, float trackLengthM, const Tuning& t) {
    if (!others || count <= 0 || !(trackLengthM > 0.0f)) return Contact::None;
    const float sideR2 = t.sideRadiusM * t.sideRadiusM;
    const float roostR2 = t.roostNearM * t.roostNearM;
    Contact best = Contact::None;
    float bestDist2 = 0.0f;
    for (int i = 0; i < count; ++i) {
        if (i == myIndex) continue;
        const float dx = others[i].posX - me.posX;
        const float dz = others[i].posZ - me.posZ;
        const float dist2 = dx * dx + dz * dz;
        if (dist2 > roostR2) continue;   // not near by any measure
        const float along = alongTrackM(me.trackPos, others[i].trackPos, trackLengthM);
        Contact contact = Contact::None;
        if (dist2 <= sideR2 && std::fabs(along) <= t.sideAlongM) {
            contact = Contact::SideBySide;
        } else if (along >= t.roostMinAlongM && along <= t.roostMaxAlongM) {
            contact = Contact::Roost;
        }
        if (contact == Contact::None) continue;
        if (best == Contact::None || dist2 < bestDist2) {
            best = contact;
            bestDist2 = dist2;
        }
    }
    return best;
}

}  // namespace Roost
