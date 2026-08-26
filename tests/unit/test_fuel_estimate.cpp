// ============================================================================
// tests/unit/test_fuel_estimate.cpp
// Pins the fuel arithmetic (core/fuel_estimate.h), which two consumers now
// share: the Fuel widget's readout and the spotter's warning. They must agree
// — a voice saying "two laps" while the number on screen says four reads as
// one of them being broken, and there is no way to tell which.
//
// THE FIRST-LAP RULE is the case worth having. Lap 1 includes sitting on the
// grid with the engine running, so its consumption is inflated; counting it
// drags the average up for the rest of the race and makes the tank look
// emptier than it is — precisely when a warning would fire, which is the worst
// time to be wrong. It is skipped only while it is still IN the window, and
// that condition is expressed through totalLapsRecorded rather than a stored
// flag, which is exactly the kind of thing that rots silently. It was also
// untestable until this moved out of a render path.
// ============================================================================
#include "doctest.h"
#include <limits>
#include <vector>

#include "core/fuel_estimate.h"

using namespace FuelEstimate;

TEST_CASE("averagePerLap: nothing to average yet") {
    CHECK(averagePerLap({}, 0) == 0.0f);
    // One lap, and it is the first: 0 means "cannot be known", which is the
    // truth until a second lap confirms a rate. Trusting it alone shipped a
    // false low-fuel warning -- see the next case.
    CHECK(averagePerLap({ 3.0f }, 1) == 0.0f);
    CHECK(lapsRemaining(10.0f, averagePerLap({ 3.0f }, 1)) == -1.0f);
}

TEST_CASE("averagePerLap: a lap-1 tank change cannot make the tank look empty") {
    // From a real session (2026-08-21). The bike read 7.20L on the pre-start
    // screen and went green with far less, so lap 1 "consumed" 4.48L against a
    // true 0.21L. Trusting that single sample put the estimate at 0.6 laps with
    // roughly a dozen in the tank, and the low-fuel warning fired on it.
    CHECK(lapsRemaining(2.72f, averagePerLap({ 4.48f }, 1)) == -1.0f);
    // The second lap is what makes a rate knowable, and lap 1 drops out of it.
    const std::vector<float> two{ 4.48f, 0.21f };
    CHECK(averagePerLap(two, 2) == doctest::Approx(0.21f));
    CHECK(lapsRemaining(2.51f, averagePerLap(two, 2)) == doctest::Approx(11.95f).epsilon(0.01));
    // Well clear of the warning, which is the point.
    CHECK(lapsRemaining(2.51f, averagePerLap(two, 2)) > kWarnLaps);
}

TEST_CASE("averagePerLap: the grid-inflated first lap is skipped") {
    // Lap 1 burned 5.0 sitting on the grid; the real pace is 2.0.
    const std::vector<float> laps = { 5.0f, 2.0f, 2.0f };
    CHECK(averagePerLap(laps, 3) == doctest::Approx(2.0f));
    // Counting it would say 3.0 — a third high, and the error is in the
    // direction that fires a warning early.
    CHECK(averagePerLap(laps, 3) != doctest::Approx(3.0f));
}

TEST_CASE("averagePerLap: once the window rolls over, every sample counts") {
    // 12 laps recorded but only 10 in the buffer: the grid lap has already
    // fallen out, so there is nothing left to skip and skipping anyway would
    // silently discard a real lap.
    const std::vector<float> laps(10, 2.0f);
    CHECK(averagePerLap(laps, 12) == doctest::Approx(2.0f));

    std::vector<float> mixed(10, 2.0f);
    mixed[0] = 8.0f;                      // an ordinary expensive lap now
    CHECK(averagePerLap(mixed, 12) == doctest::Approx(2.6f));   // counted
    CHECK(averagePerLap(mixed, 10) == doctest::Approx(2.0f));   // skipped
}

TEST_CASE("lapsRemaining: divides, clamps, and refuses to guess") {
    CHECK(lapsRemaining(10.0f, 2.0f) == doctest::Approx(5.0f));

    // No history, so no answer — NOT zero, which would read as an empty tank
    // and fire the critical warning on the out-lap of every session.
    CHECK(lapsRemaining(10.0f, 0.0f) == -1.0f);
    CHECK(lapsRemaining(10.0f, 0.0005f) == -1.0f);
    CHECK(lapsRemaining(-1.0f, 2.0f) == -1.0f);

    // A full tank against a tiny average would otherwise produce a number
    // nobody would believe.
    CHECK(lapsRemaining(100.0f, 0.01f) == doctest::Approx(99.9f));

    // An empty tank IS an answer, and has to be distinguishable from "unknown"
    // — the warning depends on the difference.
    CHECK(lapsRemaining(0.0f, 2.0f) == doctest::Approx(0.0f));
}

TEST_CASE("the thresholds are ordered, and the widget shares them") {
    // Two constants rather than four: the Fuel widget colours at these and
    // the spotter speaks at them, so a change moves both together.
    CHECK(kCriticalLaps < kWarnLaps);
    CHECK(kCriticalLaps > 0.0f);
}

TEST_CASE("non-finite inputs refuse to guess, like every other guard") {
    // The project invariant (CLAUDE.md): NaN slips every ordering comparison
    // and +Inf slips `<=` — an infinite average returned a confident 0.0
    // ("empty tank", so fuel_critical fired) instead of the -1 "cannot be
    // known", and a NaN tank leaked NaN into the widget's readout.
    const float inf = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK(lapsRemaining(10.0f, inf) == -1.0f);
    CHECK(lapsRemaining(10.0f, -inf) == -1.0f);
    CHECK(lapsRemaining(10.0f, nan) == -1.0f);
    CHECK(lapsRemaining(inf, 2.0f) == -1.0f);
    CHECK(lapsRemaining(nan, 2.0f) == -1.0f);
}
