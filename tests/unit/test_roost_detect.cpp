// ============================================================================
// tests/unit/test_roost_detect.cpp
// Unit tests for core/roost_detect.h — the pairwise proximity core behind the
// Roost achievement. Side by Side is still CLASSIFIED -- it is how the detector
// says "alongside, not behind", which is what stops a rider beside you crediting
// roost time -- it just no longer has a row of its own.
//
// The cases that earn their place are the ones a naive "are they near me?"
// test gets wrong: the start/finish line (two riders nose to tail across it
// are 3m apart, not most of a lap), a rider ALONGSIDE never counting as being
// in your roost and vice versa, and the closest rider winning when a pack
// offers several — because at most one contact is credited per tick and the
// wrong choice makes a mid-field scrap worth more than a fight for the lead.
//
// test_plugin_utils.cpp provides the doctest impl + main
// (DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN); this TU only registers more tests.
// ============================================================================
#include "doctest.h"

#include "core/roost_detect.h"

#include <vector>

namespace {
constexpr float kLap = 1000.0f;              // a tidy 1 km lap: 1m = 0.001 trackPos
constexpr Roost::Tuning kT{};
// A rider `alongM` up the road from the origin and `lateralM` to the side.
Roost::Rider at(float alongM, float lateralM = 0.0f) {
    return Roost::Rider{ lateralM, alongM, alongM / kLap };
}
Roost::Contact classify(const std::vector<Roost::Rider>& riders, int me = 0) {
    return Roost::classify(riders[static_cast<size_t>(me)], riders.data(),
                           static_cast<int>(riders.size()), me, kLap, kT);
}
}  // namespace

TEST_CASE("roost detect: ahead is roost, alongside is side by side, and never both") {
    // 8m up the road and directly in front: in the spray.
    CHECK(classify({ at(0.0f), at(8.0f) }) == Roost::Contact::Roost);
    // Half a metre up the road and a metre across: wheel to wheel.
    CHECK(classify({ at(0.0f), at(0.5f, 1.0f) }) == Roost::Contact::SideBySide);
    // The bands do not overlap: 2m ahead is inside the side-by-side along
    // window and OUTSIDE roost's lower bound, so it is not quietly both.
    CHECK(classify({ at(0.0f), at(2.0f) }) == Roost::Contact::SideBySide);
    // ...and 4m ahead has left the side-by-side window for the roost.
    CHECK(classify({ at(0.0f), at(4.0f) }) == Roost::Contact::Roost);
}

TEST_CASE("roost detect: behind you is not your roost") {
    // The rider you are throwing roost AT gets nothing for it.
    CHECK(classify({ at(0.0f), at(-8.0f) }) == Roost::Contact::None);
    // Two riders, and the one behind is closer: still nothing, the contact is
    // decided per rider and a trailing rider never qualifies as roost.
    CHECK(classify({ at(0.0f), at(-4.0f), at(10.0f) }) == Roost::Contact::Roost);
}

TEST_CASE("roost detect: out of range in either measure is no contact") {
    CHECK(classify({ at(0.0f), at(14.0f) }) == Roost::Contact::None);   // past roostMaxAlong
    CHECK(classify({ at(0.0f), at(30.0f) }) == Roost::Contact::None);   // past the world radius
    // Alongside on the centreline count, but a lane and a half away in the
    // world: too far to be racing them.
    CHECK(classify({ at(0.0f), at(0.0f, 9.0f) }) == Roost::Contact::None);
    // ...and near in the world but half a lap away on the centreline. This is
    // the case a pure world-distance test gets wrong on a tight hairpin.
    CHECK(classify({ at(0.0f), Roost::Rider{ 3.0f, 3.0f, 0.5f } }) == Roost::Contact::None);
}

TEST_CASE("roost detect: the start/finish line is not a cliff") {
    // Me just before the line (trackPos 0.998), them just after (0.002): 4m
    // apart, not 996m. Unwrapped, this reads as most of a lap behind and
    // scores nothing.
    Roost::Rider me{ 0.0f, 0.0f, 0.998f };
    Roost::Rider them{ 0.0f, 4.0f, 0.002f };
    CHECK(classify({ me, them }) == Roost::Contact::Roost);
    // And the other way round: they are 4m BEHIND across the line.
    CHECK(classify({ them, me }) == Roost::Contact::None);

    CHECK(Roost::alongTrackM(0.998f, 0.002f, kLap) == doctest::Approx(4.0));
    CHECK(Roost::alongTrackM(0.002f, 0.998f, kLap) == doctest::Approx(-4.0));
}

TEST_CASE("roost detect: the closest rider decides, and you are never your own contact") {
    // A pack: one alongside at 1m, one in the spray at 10m. The nearer wins,
    // so a tick is credited to side by side and not to roost.
    CHECK(classify({ at(0.0f), at(10.0f), at(0.5f, 1.0f) }) == Roost::Contact::SideBySide);
    // Alone, however many times the array holds you.
    CHECK(classify({ at(0.0f) }) == Roost::Contact::None);
    CHECK(Roost::classify(at(0.0f), nullptr, 0, -1, kLap, kT) == Roost::Contact::None);
    // A track with no length cannot convert centreline positions at all.
    std::vector<Roost::Rider> pair{ at(0.0f), at(8.0f) };
    CHECK(Roost::classify(pair[0], pair.data(), 2, 0, 0.0f, kT) == Roost::Contact::None);
}
