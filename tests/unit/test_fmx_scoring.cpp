// ============================================================================
// tests/unit/test_fmx_scoring.cpp
// Pure-logic unit tests for the FMX scoring math (core/fmx_scoring.h), which was
// extracted out of FmxManager so it could be exercised headless. Pins the two
// scoring calculations against the behavior documented in the source:
//   - calculateTrickScore: rotation scaling (floored at 1x), the air vs ground
//     duration/distance bonus, and the clutch-scaled coaster bonus,
//   - calculateChainMultiplier: the free first trick, the +chainBonus per extra
//     trick, the diminishing repetition penalty, L/R variants counting as one
//     type, and the optional in-progress "extra" trick.
//
// Doctest impl lives in test_plugin_utils.cpp; this TU only adds cases.
// ============================================================================
#include "doctest.h"

#include "core/fmx_scoring.h"
#include <cmath>
#include <cstdio>

using namespace Fmx;

// A TrickInstance carrying just the fields the scoring reads.
static TrickInstance makeTrick(TrickType type, int baseScore, float duration,
                               float distance, float peakPitch = 0.0f,
                               float clutchHeldTime = 0.0f) {
    TrickInstance t;
    t.type = type;
    t.baseScore = baseScore;
    t.duration = duration;
    t.distance = distance;
    t.peakPitch = peakPitch;
    t.clutchHeldTime = clutchHeldTime;
    return t;
}

TEST_CASE("fmx scoring: rotation scale floors at 1x and scales past a full turn") {
    FmxConfig cfg;  // defaults

    // BACKFLIP is an air trick on the PITCH axis. Zero duration/distance keeps
    // the air bonus at exactly 1.0 so only the rotation scale is under test.
    // 720 deg = 2.0x; base 100 -> 200.
    CHECK(calculateTrickScore(makeTrick(TrickType::BACKFLIP, 100, 0.0f, 0.0f, 720.0f), cfg) == 200);

    // Under a full rotation the scale is floored at 1.0x (max(1, deg/360)),
    // so 350 deg is NOT 0.97x — it is 1.0x. Pins the floor, not the comment.
    CHECK(calculateTrickScore(makeTrick(TrickType::BACKFLIP, 100, 0.0f, 0.0f, 350.0f), cfg) == 100);
}

TEST_CASE("fmx scoring: air tricks scale with duration and distance") {
    FmxConfig cfg;  // durationBonusRate 0.25, distanceBonusRate 0.01
    // AIR has no rotation axis (scale 1.0). airBonus = 1 + 2*0.25 + 50*0.01 = 2.0.
    // base 50 -> 100.
    CHECK(calculateTrickScore(makeTrick(TrickType::AIR, 50, 2.0f, 50.0f), cfg) == 100);
}

TEST_CASE("fmx scoring: ground tricks scale with duration (floored) and distance") {
    FmxConfig cfg;
    // WHEELIE full duration = BALANCE_TRICK_FULL_DURATION (2s). duration 6s -> 3.0,
    // plus distance 100m * 0.01 = 1.0 -> groundBonus 4.0. base 10 -> 40.
    CHECK(calculateTrickScore(makeTrick(TrickType::WHEELIE, 10, 6.0f, 100.0f), cfg) == 40);
}

TEST_CASE("fmx scoring: coaster bonus is clutch-scaled and doubles a plain wheelie") {
    FmxConfig cfg;
    // Plain 2s wheelie: base 10, groundBonus max(1, 2/2)=1.0 -> 10.
    int wheelie = calculateTrickScore(makeTrick(TrickType::WHEELIE, 10, 2.0f, 0.0f), cfg);
    CHECK(wheelie == 10);

    // Coaster wheelie, clutch held the whole 2s (ratio 1.0): +COASTER_SCORE_BONUS
    // before the (1.0x) ground bonus -> (10 + 10) = 20, i.e. ~2x the plain wheelie.
    int coaster = calculateTrickScore(
        makeTrick(TrickType::COASTER_WHEELIE, 10, 2.0f, 0.0f, /*pitch*/0.0f, /*clutch*/2.0f), cfg);
    CHECK(coaster == 20);
    CHECK(coaster == 2 * wheelie);

    // Half the clutch time -> half the bonus: 10 + 5 = 15.
    int halfClutch = calculateTrickScore(
        makeTrick(TrickType::COASTER_WHEELIE, 10, 2.0f, 0.0f, 0.0f, /*clutch*/1.0f), cfg);
    CHECK(halfClutch == 15);
}

TEST_CASE("fmx chain: first trick is free, each extra adds a diminishing bonus") {
    FmxConfig cfg;  // chainBonusPerTrick 0.5, repetitionPenalty 0.5

    // No tricks / a single trick: multiplier stays 1.0 (the first is free).
    CHECK(calculateChainMultiplier({}, TrickType::NONE, cfg) == doctest::Approx(1.0f));
    CHECK(calculateChainMultiplier({makeTrick(TrickType::WHEELIE, 10, 1.0f, 0.0f)},
                                   TrickType::NONE, cfg) == doctest::Approx(1.0f));

    // Two DIFFERENT tricks -> full +0.5 -> 1.5.
    std::vector<TrickInstance> twoDifferent = {
        makeTrick(TrickType::WHEELIE, 10, 1.0f, 0.0f),
        makeTrick(TrickType::BACKFLIP, 100, 1.0f, 0.0f),
    };
    CHECK(calculateChainMultiplier(twoDifferent, TrickType::NONE, cfg) == doctest::Approx(1.5f));

    // Two of the SAME type -> repetition penalty halves the bonus -> 1.25.
    std::vector<TrickInstance> twoSame = {
        makeTrick(TrickType::WHEELIE, 10, 1.0f, 0.0f),
        makeTrick(TrickType::WHEELIE, 10, 1.0f, 0.0f),
    };
    CHECK(calculateChainMultiplier(twoSame, TrickType::NONE, cfg) == doctest::Approx(1.25f));
}

TEST_CASE("fmx chain: L/R variants are one type, and the extra active trick counts") {
    FmxConfig cfg;

    // DRIFT_LEFT + DRIFT_RIGHT share a base type -> counts as a repeat -> 1.25.
    std::vector<TrickInstance> lr = {
        makeTrick(TrickType::DRIFT_LEFT, 10, 1.0f, 0.0f),
        makeTrick(TrickType::DRIFT_RIGHT, 10, 1.0f, 0.0f),
    };
    CHECK(calculateChainMultiplier(lr, TrickType::NONE, cfg) == doctest::Approx(1.25f));

    // One banked trick + a different in-progress (extra) trick -> +0.5 -> 1.5.
    std::vector<TrickInstance> one = {makeTrick(TrickType::WHEELIE, 10, 1.0f, 0.0f)};
    CHECK(calculateChainMultiplier(one, TrickType::BACKFLIP, cfg) == doctest::Approx(1.5f));
}

// ============================================================================
// docs/tricks.md is GENERATED from fmx_types.h (the trick table, its base
// scores, axes, INI keys and thresholds), FmxConfig's defaults and the scoring
// in fmx_scoring.h, then diffed against the committed copy, the way the
// achievements overview and the spotter reference are. A trick added, renamed
// or re-scored changes the generated text, this fails, and committing the
// regenerated file is the fix. The rule column is worded here and NUMBERED
// from the code: the words can only go stale if the detector's shape changes,
// which is a review matter; the thresholds cannot.
// ============================================================================
#include <fstream>
#include <sstream>
#include <string>

namespace {

std::string num(float v) {
    char buf[32];
    if (v == static_cast<int>(v)) snprintf(buf, sizeof(buf), "%d", static_cast<int>(v));
    else snprintf(buf, sizeof(buf), "%g", v);
    return buf;
}

std::string deg(float v)  { return num(v) + " deg"; }
std::string secs(float v) { return num(v) + " s"; }
// Speeds are m/s in the code and km/h to a rider.
std::string kmh(float v)  { return num(std::round(v * 36.0f) / 10.0f) + " km/h"; }

// The stationary gate lives in GroundContactState as a comparison, not a
// named constant: read it off the struct so the number here is the real one.
float stationaryBelow() {
    GroundContactState g;
    for (int tenths = 0; tenths < 200; ++tenths) {
        g.vehicleSpeed = tenths / 10.0f;
        if (!g.isStationary()) return g.vehicleSpeed;
    }
    return 0.0f;
}

const char* axisName(RotationAxis a) {
    switch (a) {
        case RotationAxis::PITCH: return "pitch";
        case RotationAxis::YAW:   return "yaw";
        case RotationAxis::ROLL:  return "roll";
        default:                  return "-";
    }
}

// What fills the progress bar to 100%, from FmxManager::calculateProgress.
std::string fullProgress(TrickType t, const FmxConfig& c) {
    switch (t) {
        case TrickType::BACKFLIP:
        case TrickType::FRONTFLIP:         return deg(c.flipCompletionAngle) + " of pitch";
        case TrickType::BARREL_ROLL_LEFT:  return deg(c.barrelRollCompletionAngle) + " of roll";
        case TrickType::SPIN_LEFT:         return deg(c.spinCompletionAngle) + " of yaw";
        case TrickType::SCRUB_LEFT:        return deg(c.scrubMaxAngle) + " of roll";
        case TrickType::WHIP_LEFT:
        case TrickType::OPPO_LEFT:
        case TrickType::TURN_DOWN_LEFT:    return deg(c.whipMaxAngle) + " of yaw";
        case TrickType::AIR:               return secs(AIR_TRICK_FULL_DURATION) + " airborne";
        case TrickType::WHEELIE:
        case TrickType::COASTER_WHEELIE:
        case TrickType::ENDO:
        case TrickType::STOPPIE:           return secs(BALANCE_TRICK_FULL_DURATION) + " held";
        case TrickType::PIVOT_LEFT:        return deg(c.pivotCompletionAngle) + " of yaw";
        case TrickType::BURNOUT:
        case TrickType::DONUT:
        case TrickType::DRIFT_LEFT:        return secs(GROUND_TRICK_FULL_DURATION) + " held";
        case TrickType::FLAT_360_LEFT:     return deg(c.flipCompletionAngle) + " of pitch or roll";
        default:                           return "-";
    }
}

// How the detector recognises each trick (FmxManager::classifyCurrentTrick),
// with the gates it reads from FmxConfig and the fmx_types.h thresholds.
std::string recognisedBy(TrickType t, const FmxConfig& c) {
    const std::string still = "under " + kmh(stationaryBelow());
    switch (t) {
        case TrickType::WHEELIE:
            return "Rear wheel only, nose up past " + deg(c.wheelieAngleThreshold) +
                   " (ends below half that)";
        case TrickType::COASTER_WHEELIE:
            return "A wheelie with the clutch held in; letting it out drops it back to a wheelie";
        case TrickType::ENDO:
            return "Front wheel only, nose down past " + deg(-c.endoAngleThreshold) + ", moving";
        case TrickType::STOPPIE:
            return "An endo " + still;
        case TrickType::BURNOUT:
            return "Rear wheel spinning " + kmh(c.burnoutSlipThreshold) + " faster than the bike, " + still;
        case TrickType::DONUT:
            return "A burnout turned through " + deg(c.donutYawThreshold);
        case TrickType::DRIFT_LEFT:
            return "Sliding at a slip angle past " + deg(c.driftSlipAngleThreshold) + ", moving";
        case TrickType::PIVOT_LEFT:
            return "A wheelie or endo turned through " + deg(c.pivotMinYaw) + " under " + kmh(c.pivotMaxSpeed);
        case TrickType::AIR:
            return "Airborne " + secs(c.airCommitTime) + " with no rotation past a threshold";
        case TrickType::BACKFLIP:
            return "Pitched backward through " + deg(FULL_ROTATION_MIN);
        case TrickType::FRONTFLIP:
            return "Pitched forward through " + deg(FULL_ROTATION_MIN);
        case TrickType::BARREL_ROLL_LEFT:
            return "Rolled through " + deg(FULL_ROTATION_MIN);
        case TrickType::SCRUB_LEFT:
            return "Rolled, or took off leaned, past " + deg(PARTIAL_ROTATION_MIN) + ", short of a barrel roll";
        case TrickType::WHIP_LEFT:
            return "Yawed past " + deg(PARTIAL_ROTATION_MIN) + " with the nose level, short of a spin";
        case TrickType::SPIN_LEFT:
            return "Yawed through " + deg(FULL_ROTATION_MIN);
        case TrickType::OPPO_LEFT:
            return "Yawed past " + deg(TURN_YAW_THRESHOLD) + " with the nose up past " + deg(TURN_PITCH_THRESHOLD);
        case TrickType::TURN_DOWN_LEFT:
            return "Yawed past " + deg(TURN_YAW_THRESHOLD) + " with the nose down past " + deg(TURN_PITCH_THRESHOLD);
        case TrickType::FLAT_360_LEFT:
            return "A flip rolled between " + deg(c.flat360MinRoll) + " and 180 deg";
        default:
            return "-";
    }
}

void trickTable(std::ostringstream& out, bool air, const FmxConfig& c) {
    out << "| Trick | INI key | Base | Scored on | Full progress | Recognised by |\n";
    out << "|---|---|---|---|---|---|\n";
    for (int i = 1; i < static_cast<int>(TrickType::COUNT); ++i) {
        const auto t = static_cast<TrickType>(i);
        if (isAirTrick(t) != air) continue;
        if (getTrickDirection(t) == TrickDirection::RIGHT) continue;   // one row per pair
        std::string name = getTrickName(t);
        const bool paired = getTrickDirection(t) == TrickDirection::LEFT;
        if (paired && name.size() > 2 && name.compare(name.size() - 2, 2, " L") == 0) name.resize(name.size() - 2);
        out << "| " << name << (paired ? " (L/R)" : "")
            << " | `" << getTrickIniKey(t) << "`"
            << " | " << getTrickBaseScore(t)
            << " | " << axisName(getPrimaryAxis(t))
            << " | " << fullProgress(t, c)
            << " | " << recognisedBy(t, c) << " |\n";
    }
    out << "\n";
}

std::string generateTricksDoc() {
    const FmxConfig c;
    std::ostringstream out;
    out << "# FMX tricks\n\n";
    out << "GENERATED from `mxbmrp3/core/fmx_types.h` and `mxbmrp3/core/fmx_scoring.h` by "
           "`tests/unit/test_fmx_scoring.cpp` - do not edit. When a trick or a number changes, run the "
           "unit gate and copy `/tmp/tricks.new.md` over this file.\n\n";
    out << "What the FMX HUD recognises, what each trick is worth and how a score is built, at the "
           "defaults. Left and right variants are one trick here, as in the settings: `trickEnabled_<INI key>=0` "
           "under `[FmxHud]` in the settings file turns one off, both directions at once. *Base* is the score before the "
           "multipliers below; *Scored on* is the axis whose rotation scales it; *Full progress* is what "
           "fills the HUD's bar.\n\n";
    out << "## Ground tricks\n\n";
    out << "A ground trick counts once it reaches " << num(MIN_GROUND_TRICK_PROGRESS * 100.0f)
        << "% of its full progress, so a momentary blip never scores.\n\n";
    trickTable(out, false, c);
    out << "## Air tricks\n\n";
    out << "An air trick classifies once the bike has been off the ground " << secs(c.airCommitTime)
        << "; before that a bump is not a trick. Full rotations (" << deg(FULL_ROTATION_MIN)
        << ") are read first, then the turns, then whip and scrub, then plain air.\n\n";
    trickTable(out, true, c);
    out << "## Scoring\n\n";
    out << "- **Rotation.** A trick with an axis is scaled by its peak rotation over 360 deg, never below 1x: "
           "a 540 deg backflip is 1.5x the base.\n";
    out << "- **Air bonus.** An air trick is then scaled by 1 + " << num(c.durationBonusRate)
        << " per second airborne + " << num(c.distanceBonusRate) << " per metre covered.\n";
    out << "- **Ground bonus.** A ground trick is scaled by its duration over its full-progress time (never "
           "below 1x) + " << num(c.distanceBonusRate) << " per metre covered, so a stationary trick earns "
           "no distance.\n";
    out << "- **Coaster.** A coaster wheelie adds up to " << COASTER_SCORE_BONUS
        << " points, in proportion to how much of it the clutch was held, before the ground bonus.\n";
    out << "- **Chain.** A landed trick is confirmed after " << secs(c.landingGracePeriod)
        << " (a crash in that window fails it), then " << secs(c.chainPeriod)
        << " are open for the next trick. Every trick after the first adds " << num(c.chainBonusPerTrick)
        << " to the chain's multiplier (two tricks " << num(1.0f + c.chainBonusPerTrick) << "x, three "
        << num(1.0f + 2.0f * c.chainBonusPerTrick) << "x); a trick already in the chain adds "
        << num(c.repetitionPenalty) << "x what its previous occurrence did. A crash loses the whole chain.\n";
    return out.str();
}

}  // namespace

TEST_CASE("docs/tricks.md is current") {
    const std::string generated = generateTricksDoc();
    const std::string path = std::string(MXB_DOCS_DIR) + "/tricks.md";
    const std::string fresh = "/tmp/tricks.new.md";
    {
        std::ofstream out(fresh, std::ios::binary);
        if (out.good()) out << generated;
    }
    std::ifstream in(path, std::ios::binary);
    REQUIRE_MESSAGE(in.good(), "missing " << path << " - seed it with: cp " << fresh << " " << path);
    std::ostringstream ss;
    ss << in.rdbuf();
    CHECK_MESSAGE(ss.str() == generated,
                  "docs/tricks.md is stale: a trick or a number changed and the generated overview "
                  "no longer matches. Review the diff, then: cp " << fresh << " " << path);
}
