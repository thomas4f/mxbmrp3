// ============================================================================
// tests/unit/test_standings_live_order.cpp
// Unit tests for hud/standings_live_order.h — Standings' live running order.
//
// The rules a plausible edit breaks silently (see the header):
//   - a pass shows only after it has held HOLD_MS, and is taken back only
//     after the reverse has held HOLD_MS (side-by-side riders must not flicker)
//   - the official order is the baseline: once it agrees, nothing is left over
//   - a stale sample or a different lap count is no evidence either way: an
//     applied pass survives a rider missing from a batch
//   - riders that may not move keep their official row
//   - riders more than half a lap apart are no evidence (the uncounted first
//     crossing after a gate start inverts the field otherwise)
// The thresholds were checked against a real online tape (see the header).
// ============================================================================
#include "doctest.h"

#include "hud/standings_live_order.h"
#include <vector>

using namespace StandingsLiveOrder;

namespace {

Rider racer(int raceNum, float trackPos, int numLaps = 2) {
    Rider r;
    r.raceNum = raceNum;
    r.numLaps = numLaps;
    r.trackPos = trackPos;
    r.racing = true;
    r.fresh = true;
    return r;
}

std::vector<int> run(Tracker& t, const std::vector<Rider>& riders, int64_t nowMs, bool* pending = nullptr) {
    std::vector<int> out;
    const bool p = t.update(riders, nowMs, out);
    if (pending) *pending = p;
    return out;
}

constexpr int64_t HOLD = Tracker::HOLD_MS;

}  // namespace

TEST_CASE("no passes: the official order comes back unchanged") {
    Tracker t;
    CHECK(run(t, { racer(1, 0.5f), racer(2, 0.4f), racer(3, 0.3f) }, 0) == std::vector<int>{ 1, 2, 3 });
    CHECK(t.pairCount() == 0);
}

TEST_CASE("a pass shows only once it has held for the hold time") {
    Tracker t;
    const std::vector<Rider> passed = { racer(1, 0.40f), racer(2, 0.41f), racer(3, 0.3f) };
    bool pending = false;
    CHECK(run(t, passed, 1000, &pending) == std::vector<int>{ 1, 2, 3 });
    CHECK(pending);  // the caller must come back without new data
    CHECK(run(t, passed, 1000 + HOLD - 1) == std::vector<int>{ 1, 2, 3 });
    CHECK(run(t, passed, 1000 + HOLD, &pending) == std::vector<int>{ 2, 1, 3 });
    CHECK_FALSE(pending);
}

TEST_CASE("side by side for less than the hold: no swap at all") {
    Tracker t;
    for (int64_t ms = 0; ms < 3000; ms += 100) {
        // #2 noses ahead for 200 ms out of every 400
        const bool ahead = (ms / 200) % 2 == 0;
        CHECK(run(t, { racer(1, 0.40f), racer(2, ahead ? 0.401f : 0.399f) }, ms) == std::vector<int>{ 1, 2 });
    }
}

TEST_CASE("an applied pass is taken back only once the reverse has held") {
    Tracker t;
    run(t, { racer(1, 0.40f), racer(2, 0.41f) }, 0);
    REQUIRE(run(t, { racer(1, 0.40f), racer(2, 0.41f) }, HOLD) == std::vector<int>{ 2, 1 });
    // #1 draws back ahead: the live pass stays until the reverse has held
    const std::vector<Rider> back = { racer(1, 0.43f), racer(2, 0.42f) };
    CHECK(run(t, back, HOLD + 100) == std::vector<int>{ 2, 1 });
    CHECK(run(t, back, 2 * HOLD + 99) == std::vector<int>{ 2, 1 });
    CHECK(run(t, back, 2 * HOLD + 100) == std::vector<int>{ 1, 2 });
    CHECK(t.pairCount() == 0);
}

TEST_CASE("the official order catching up leaves nothing behind") {
    Tracker t;
    run(t, { racer(1, 0.40f), racer(2, 0.41f) }, 0);
    REQUIRE(run(t, { racer(1, 0.40f), racer(2, 0.41f) }, HOLD) == std::vector<int>{ 2, 1 });
    // The game's next update has #2 ahead: the pair is no longer a pass
    CHECK(run(t, { racer(2, 0.45f), racer(1, 0.44f) }, HOLD + 100) == std::vector<int>{ 2, 1 });
    CHECK(t.pairCount() == 0);
}

TEST_CASE("a rider missing from the batch keeps an applied pass") {
    Tracker t;
    run(t, { racer(1, 0.40f), racer(2, 0.41f) }, 0);
    REQUIRE(run(t, { racer(1, 0.40f), racer(2, 0.41f) }, HOLD) == std::vector<int>{ 2, 1 });
    Rider stale = racer(2, 0.30f);  // a stale position behind #1
    stale.fresh = false;
    CHECK(run(t, { racer(1, 0.45f), stale }, 10 * HOLD) == std::vector<int>{ 2, 1 });
}

TEST_CASE("a lap count bumped before the position wraps never shows as a pass") {
    // At the line the game raises the lap count a few frames before the
    // position wraps, so #2 reads for a moment as a lap further on.
    Tracker t;
    // #1 not yet bumped: different laps, no evidence, no pair at all
    CHECK(run(t, { racer(1, 0.998f, 2), racer(2, 0.999f, 3) }, 0) == std::vector<int>{ 1, 2 });
    CHECK(t.pairCount() == 0);
    // Both on lap 3 but #2 not yet wrapped: it reads as ahead until it wraps,
    // well inside the hold, and is forgotten then
    CHECK(run(t, { racer(1, 0.01f, 3), racer(2, 0.999f, 3) }, 100) == std::vector<int>{ 1, 2 });
    CHECK(run(t, { racer(1, 0.02f, 3), racer(2, 0.001f, 3) }, 200) == std::vector<int>{ 1, 2 });
    CHECK(t.pairCount() == 0);
    CHECK(run(t, { racer(1, 0.03f, 3), racer(2, 0.002f, 3) }, 200 + HOLD) == std::vector<int>{ 1, 2 });
}

TEST_CASE("the uncounted first crossing after a gate start never shows as a pass") {
    // After a gate start the game does not count the first crossing as a lap:
    // the leader is over the line at 0.02 on lap 0, the riders still short of
    // it read 0.97 and 0.95 on lap 0. Seen on the forest_short golden tape.
    Tracker t;
    const std::vector<Rider> field = { racer(1, 0.02f, 0), racer(2, 0.97f, 0), racer(3, 0.95f, 0) };
    run(t, field, 0);
    CHECK(run(t, field, 2 * HOLD) == std::vector<int>{ 1, 2, 3 });
    CHECK(t.pairCount() == 0);
}

TEST_CASE("riders who may not move keep their official row") {
    Tracker t;
    Rider pits = racer(2, 0.10f);
    pits.racing = false;
    const std::vector<Rider> field = { racer(1, 0.40f), pits, racer(3, 0.50f), racer(4, 0.20f) };
    run(t, field, 0);
    // #3 passes #1; #2 stays in row 2, the racers fill rows 1, 3 and 4
    CHECK(run(t, field, HOLD) == std::vector<int>{ 3, 2, 1, 4 });
}

TEST_CASE("one rider passing two at once moves up past both") {
    Tracker t;
    const std::vector<Rider> field = { racer(1, 0.40f), racer(2, 0.39f), racer(3, 0.41f), racer(4, 0.10f) };
    run(t, field, 0);
    CHECK(run(t, field, HOLD) == std::vector<int>{ 3, 1, 2, 4 });
}

TEST_CASE("reset forgets every pass") {
    Tracker t;
    run(t, { racer(1, 0.40f), racer(2, 0.41f) }, 0);
    t.reset();
    CHECK(run(t, { racer(1, 0.40f), racer(2, 0.41f) }, HOLD) == std::vector<int>{ 1, 2 });
}

TEST_CASE("forty riders in reverse: every row filled once, no rider lost") {
    Tracker t;
    std::vector<Rider> field;
    for (int i = 0; i < 40; ++i) field.push_back(racer(100 + i, 0.01f * static_cast<float>(i)));
    run(t, field, 0);
    const std::vector<int> out = run(t, field, HOLD);
    REQUIRE(out.size() == 40);
    for (int i = 0; i < 40; ++i) CHECK(out[i] == 139 - i);
}
