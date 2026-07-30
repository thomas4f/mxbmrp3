// ============================================================================
// tests/unit/test_director_detect.cpp
// The auto-director's overtake + drop edge detectors (core/director_detect.h).
//
// The two properties most worth pinning are the ones a reader is most likely to
// "simplify" away, because in both cases the obvious implementation is wrong:
//
//   * OVERTAKE compares RELATIVE ORDER, not absolute positions. Comparing
//     positions directly looks equivalent and is not: when a rider ahead pits or
//     retires, everyone behind gains a place without anyone passing anyone. The
//     pit/retire cases below fail loudly on that mistake.
//   * DROP gates on the BASELINE position, not the current one. Gating on where
//     the rider ended up discards exactly the story worth showing — a front-
//     runner who slid to the back.
// ============================================================================
#include "doctest.h"

#include "core/director_detect.h"

using namespace director_detail;

namespace {

// Position-sorted rider, racing, same lap, past lap 1 unless stated otherwise.
Rider mk(int pos, int num, bool finished = false, int gapLaps = 0, int numLaps = 5) {
    return Rider{ pos, num, /*gapToLeaderMs*/ pos * 1000, gapLaps, numLaps,
                  /*bestLapMs*/ 90000, finished };
}

}  // namespace

// ---------------------------------------------------------------- overtakes --

TEST_CASE("no previous snapshot means no overtake (opening-lap deferral)") {
    const std::vector<Rider> riders = { mk(1, 10), mk(2, 22) };
    CHECK_FALSE(detectOvertake(riders, /*prev*/ {}, /*maxPos*/ 0).valid());
}

TEST_CASE("an unchanged field fires nothing") {
    const std::vector<Rider> riders = { mk(1, 10), mk(2, 22), mk(3, 33) };
    const PositionSnapshot prev = { {10, 1}, {22, 2}, {33, 3} };
    CHECK_FALSE(detectOvertake(riders, prev, 0).valid());
}

TEST_CASE("a simple swap is detected with the right pair and direction") {
    // #22 was P2 behind #10; now P1 ahead of it.
    const std::vector<Rider> riders = { mk(1, 22), mk(2, 10), mk(3, 33) };
    const PositionSnapshot prev = { {10, 1}, {22, 2}, {33, 3} };

    const auto ot = detectOvertake(riders, prev, 0);
    REQUIRE(ot.valid());
    CHECK(ot.overtaker == 22);
    CHECK(ot.overtaken == 10);
    CHECK(ot.gained == 1);
}

TEST_CASE("a rider ahead retiring is NOT a pass") {
    // #10 (was P1) is gone from the field entirely. #22 and #33 each gain a
    // place, but their ORDER relative to each other never changed.
    const std::vector<Rider> riders = { mk(1, 22), mk(2, 33) };
    const PositionSnapshot prev = { {10, 1}, {22, 2}, {33, 3} };
    CHECK_FALSE(detectOvertake(riders, prev, 0).valid());
}

TEST_CASE("a rider ahead pitting is NOT a pass, even for a long train") {
    // The whole field shifts up one. Absolute-position comparison would report
    // several phantom passes here; relative order reports none.
    const std::vector<Rider> riders = { mk(1, 22), mk(2, 33), mk(3, 44), mk(4, 55) };
    const PositionSnapshot prev = { {10, 1}, {22, 2}, {33, 3}, {44, 4}, {55, 5} };
    CHECK_FALSE(detectOvertake(riders, prev, 0).valid());
}

TEST_CASE("riders on different laps are never paired") {
    const std::vector<Rider> riders = { mk(1, 22, false, /*gapLaps*/ 0),
                                        mk(2, 10, false, /*gapLaps*/ 1) };
    const PositionSnapshot prev = { {10, 1}, {22, 2} };
    CHECK_FALSE(detectOvertake(riders, prev, 0).valid());
}

TEST_CASE("a finished rider's slow-down lap never anchors a pass") {
    const std::vector<Rider> riders = { mk(1, 22), mk(2, 10, /*finished*/ true) };
    const PositionSnapshot prev = { {10, 1}, {22, 2} };
    CHECK_FALSE(detectOvertake(riders, prev, 0).valid());
}

TEST_CASE("the front-most pass wins when two happen at once") {
    // #33 passed #44 at P3/P4, and #22 passed #10 at P1/P2. The front one wins.
    const std::vector<Rider> riders = { mk(1, 22), mk(2, 10), mk(3, 33), mk(4, 44) };
    const PositionSnapshot prev = { {22, 2}, {10, 1}, {33, 4}, {44, 3} };

    const auto ot = detectOvertake(riders, prev, 0);
    REQUIRE(ot.valid());
    CHECK(ot.overtaker == 22);
}

TEST_CASE("the position cutoff suppresses a deep-field pass") {
    const std::vector<Rider> riders = { mk(1, 10), mk(2, 22), mk(3, 44), mk(4, 33) };
    const PositionSnapshot prev = { {10, 1}, {22, 2}, {33, 3}, {44, 4} };

    CHECK(detectOvertake(riders, prev, /*maxPos*/ 0).valid());   // no cutoff -> fires
    CHECK(detectOvertake(riders, prev, /*maxPos*/ 10).valid());  // inside cutoff
    CHECK_FALSE(detectOvertake(riders, prev, /*maxPos*/ 2).valid());  // P3 pass, cutoff P2
}

TEST_CASE("a multi-place move counts everyone actually cleared") {
    // #55 was last (P4) and is now P1 — it cleared three riders.
    const std::vector<Rider> riders = { mk(1, 55), mk(2, 10), mk(3, 22), mk(4, 33) };
    const PositionSnapshot prev = { {10, 1}, {22, 2}, {33, 3}, {55, 4} };

    const auto ot = detectOvertake(riders, prev, 0);
    REQUIRE(ot.valid());
    CHECK(ot.overtaker == 55);
    CHECK(ot.gained == 3);
}

TEST_CASE("gained is never zero for a valid pass") {
    const std::vector<Rider> riders = { mk(1, 22), mk(2, 10) };
    const PositionSnapshot prev = { {10, 1}, {22, 2} };
    const auto ot = detectOvertake(riders, prev, 0);
    REQUIRE(ot.valid());
    CHECK(ot.gained >= 1);
}

TEST_CASE("a single-rider field is safe") {
    const std::vector<Rider> riders = { mk(1, 10) };
    CHECK_FALSE(detectOvertake(riders, { {10, 2} }, 0).valid());
    CHECK_FALSE(detectOvertake({}, { {10, 2} }, 0).valid());   // empty field
}

// -------------------------------------------------------------------- drops --

TEST_CASE("a rider holding station is not dropping") {
    const std::vector<Rider> riders = { mk(1, 10), mk(2, 22) };
    const PositionSnapshot base = { {10, 1}, {22, 2} };
    CHECK_FALSE(detectDrop(riders, base, /*maxPos*/ 0, /*threshold*/ 3).valid());
}

TEST_CASE("a slide below the threshold does not fire") {
    const std::vector<Rider> riders = { mk(3, 10) };   // P1 -> P3, lost 2
    const PositionSnapshot base = { {10, 1} };
    CHECK_FALSE(detectDrop(riders, base, 0, /*threshold*/ 3).valid());
}

TEST_CASE("a slide at or past the threshold fires with the places lost") {
    const std::vector<Rider> riders = { mk(4, 10) };   // P1 -> P4, lost 3
    const PositionSnapshot base = { {10, 1} };
    const auto d = detectDrop(riders, base, 0, 3);
    REQUIRE(d.valid());
    CHECK(d.raceNum == 10);
    CHECK(d.lost == 3);
}

TEST_CASE("the worst tumbler wins") {
    const std::vector<Rider> riders = { mk(5, 10), mk(9, 22) };
    const PositionSnapshot base = { {10, 1}, {22, 2} };   // lost 4 and 7
    const auto d = detectDrop(riders, base, 0, 3);
    REQUIRE(d.valid());
    CHECK(d.raceNum == 22);
    CHECK(d.lost == 7);
}

TEST_CASE("the cutoff is applied to where the slide STARTED, not where it ended") {
    // A P2 runner who slid to P15 is the story. Gating on the CURRENT position
    // would throw it away — this is the regression that matters most here.
    const std::vector<Rider> riders = { mk(15, 10) };
    const PositionSnapshot base = { {10, 2} };
    const auto d = detectDrop(riders, base, /*maxPos*/ 6, /*threshold*/ 3);
    REQUIRE(d.valid());
    CHECK(d.raceNum == 10);
    CHECK(d.lost == 13);

    // ...whereas a rider who started outside the cutoff is correctly ignored.
    const std::vector<Rider> deep = { mk(25, 33) };
    CHECK_FALSE(detectDrop(deep, { {33, 12} }, /*maxPos*/ 6, 3).valid());
}

TEST_CASE("finished and lap-1 riders never register a drop") {
    const PositionSnapshot base = { {10, 1} };
    CHECK_FALSE(detectDrop({ mk(9, 10, /*finished*/ true) }, base, 0, 3).valid());
    CHECK_FALSE(detectDrop({ mk(9, 10, false, 0, /*numLaps*/ 0) }, base, 0, 3).valid());
}

TEST_CASE("a rider with no baseline entry cannot fire") {
    // Riders only enter the baseline once past lap 1, so a late joiner has none.
    CHECK_FALSE(detectDrop({ mk(9, 77) }, { {10, 1} }, 0, 3).valid());
}

TEST_CASE("gaining places is never a drop") {
    const std::vector<Rider> riders = { mk(1, 10) };
    CHECK_FALSE(detectDrop(riders, { {10, 9} }, 0, 3).valid());
}
