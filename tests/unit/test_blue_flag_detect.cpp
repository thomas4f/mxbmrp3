// ============================================================================
// tests/unit/test_blue_flag_detect.cpp
// The blue-flag / lapping proximity core (core/blue_flag_detect.h).
//
// This logic used to be inline in PluginData, reachable only by driving real
// RaceTrackPosition callbacks through the DLL under Wine — so every edge case
// cost a full cross-build and a headless run, and most of them were simply
// never written. As a pure function over a flat array it costs ~1s here.
//
// blueflag_test.cpp (integration) still owns the end-to-end proof that the
// plugin wires this up correctly off real callbacks; these cover the cases that
// are awkward to stage through a whole race — wraparound at start/finish,
// exactly-on-the-threshold distances, the asymmetric eligibility of the two
// roles, and the ordering contract the director depends on.
// ============================================================================
#include "doctest.h"
#include "core/blue_flag_detect.h"

using BlueFlag::Rider;
using BlueFlag::Player;

namespace {

// A rider that is a valid backmarker AND has a fresh sample, which is the
// common case; individual tests flip the flags they care about.
Rider rider(int num, int laps, float pos, bool active = true, bool eligible = true,
            bool finished = false) {
    Rider r;
    r.raceNum = num; r.laps = laps; r.trackPos = pos;
    r.active = active; r.eligibleBackmarker = eligible; r.finished = finished;
    return r;
}

struct Out {
    std::unordered_set<int> flagged;
    std::unordered_map<int, int> lapperToLapped;
    bool playerLapping = false;

    void run(const std::vector<Rider>& riders, int maxLaps, float threshold,
             const Player& p = Player{}) {
        BlueFlag::detect(riders, maxLaps, threshold, p, flagged, lapperToLapped,
                         playerLapping);
    }
    bool isFlagged(int num) const { return flagged.count(num) > 0; }
};

}  // namespace

TEST_CASE("distanceBehind wraps through start/finish") {
    // Straightforward: 0.2 behind 0.5 is 0.3 of a lap.
    CHECK(BlueFlag::distanceBehind(0.2f, 0.5f) == doctest::Approx(0.3f));
    // Wrapped: 0.95 behind 0.05 is 0.10, not 0.90. Getting this backwards makes
    // a rider about to cross the line look a full lap away from the rider just
    // past it — i.e. no blue flag exactly where one is most needed.
    CHECK(BlueFlag::distanceBehind(0.95f, 0.05f) == doctest::Approx(0.10f));
    // Equal positions are zero distance, not a full lap. This is the case a
    // strict `<` got wrong: equality fell into the wrap branch and returned
    // 1.0, so a lapper exactly on top of a backmarker was skipped as a
    // blue-flag candidate. See the note on distanceBehind().
    CHECK(BlueFlag::distanceBehind(0.5f, 0.5f) == doctest::Approx(0.0f));
}

TEST_CASE("a lapper exactly on top of a backmarker still raises the flag") {
    // The behavioural half of the equal-positions fix: with the old strict
    // comparison this returned "a full lap away" and no flag was raised.
    Out out;
    out.run({ rider(1, 5, 0.50f), rider(2, 6, 0.50f) }, /*maxLaps=*/6, 0.05f);
    CHECK(out.isFlagged(1));
    CHECK(out.lapperToLapped.at(2) == 1);
}

TEST_CASE("a rider being caught by someone a lap up is blue-flagged") {
    Out out;
    out.run({ rider(1, 5, 0.50f),        // backmarker
              rider(2, 6, 0.47f) },      // lapper, 3% of a lap behind
            /*maxLaps=*/6, /*threshold=*/0.05f);

    CHECK(out.isFlagged(1));
    CHECK_FALSE(out.isFlagged(2));       // the lapper is on the lead lap
    REQUIRE(out.lapperToLapped.count(2) == 1);
    CHECK(out.lapperToLapped.at(2) == 1);
}

TEST_CASE("proximity is directional and bounded") {
    SUBCASE("a lapper AHEAD of the backmarker does not raise a flag") {
        // Same 3% gap, but the faster rider is in front — nothing to yield to.
        Out out;
        out.run({ rider(1, 5, 0.50f), rider(2, 6, 0.53f) }, 6, 0.05f);
        CHECK_FALSE(out.isFlagged(1));
    }
    SUBCASE("outside the awareness window, no flag") {
        Out out;
        out.run({ rider(1, 5, 0.50f), rider(2, 6, 0.30f) }, 6, 0.05f);
        CHECK_FALSE(out.isFlagged(1));
    }
    SUBCASE("just inside the window flags, just outside does not") {
        // Deliberately NOT testing the exact threshold: 0.50f - 0.45f is
        // 0.0500000119 in float, so "exactly 0.05" already lands outside a
        // <= 0.05f test. The boundary is a float artifact, not a contract —
        // pinning it would make the test a precision detector rather than a
        // behaviour check.
        Out in;
        in.run({ rider(1, 5, 0.50f), rider(2, 6, 0.46f) }, 6, 0.05f);
        CHECK(in.isFlagged(1));

        Out outside;
        outside.run({ rider(1, 5, 0.50f), rider(2, 6, 0.44f) }, 6, 0.05f);
        CHECK_FALSE(outside.isFlagged(1));
    }
    SUBCASE("the window wraps through start/finish") {
        // Backmarker just past the line, lapper just before it.
        Out out;
        out.run({ rider(1, 5, 0.02f), rider(2, 6, 0.99f) }, 6, 0.05f);
        CHECK(out.isFlagged(1));
    }
}

TEST_CASE("everyone on the same lap means nobody is lapped") {
    Out out;
    out.run({ rider(1, 6, 0.50f), rider(2, 6, 0.49f), rider(3, 6, 0.48f) },
            6, 0.05f);
    CHECK(out.flagged.empty());
    CHECK(out.lapperToLapped.empty());
}

TEST_CASE("a rider on the leader's lap is never blue-flagged") {
    // Rider 1 is at maxLaps, so even a nearby rider with more laps recorded
    // (which shouldn't happen, but the guard is what makes it not matter)
    // cannot flag them.
    Out out;
    out.run({ rider(1, 6, 0.50f), rider(2, 7, 0.48f) }, /*maxLaps=*/6, 0.05f);
    CHECK_FALSE(out.isFlagged(1));
}

TEST_CASE("the two roles have deliberately different eligibility") {
    SUBCASE("an ineligible backmarker is not flagged (excluded / finished / pitted)") {
        Out out;
        out.run({ rider(1, 5, 0.50f, /*active=*/true, /*eligible=*/false),
                  rider(2, 6, 0.48f) }, 6, 0.05f);
        CHECK_FALSE(out.isFlagged(1));
    }
    SUBCASE("an EXCLUDED rider can still be the lapper (pit lane)") {
        // This asymmetry is the point: a rider on pit exit is not shown a blue
        // flag, but is still a real bike on track closing on someone.
        // Collapsing the flags into one would silently stop flagging
        // backmarkers being caught out of the pit lane.
        Out out;
        out.run({ rider(1, 5, 0.50f),
                  rider(2, 6, 0.48f, /*active=*/true, /*eligible=*/false) },
                6, 0.05f);
        CHECK(out.isFlagged(1));
        CHECK(out.lapperToLapped.at(2) == 1);
    }
    SUBCASE("a STALE lapper is ignored") {
        // An inactive rider's trackPos may be from a previous lap, so trusting
        // it invents proximity that isn't there.
        Out out;
        out.run({ rider(1, 5, 0.50f),
                  rider(2, 6, 0.48f, /*active=*/false) }, 6, 0.05f);
        CHECK_FALSE(out.isFlagged(1));
    }
}

TEST_CASE("ordering contract: first qualifying lapper wins, last writer keeps the pair") {
    // Two lappers both within range of the same backmarker. The loop takes the
    // first in array order and stops — the director follows that pairing, so it
    // must be stable rather than whichever hash bucket came up.
    Out out;
    out.run({ rider(1, 5, 0.50f), rider(2, 6, 0.49f), rider(3, 6, 0.48f) },
            6, 0.05f);
    CHECK(out.isFlagged(1));
    CHECK(out.lapperToLapped.count(2) == 1);   // first in array order
    CHECK(out.lapperToLapped.count(3) == 0);   // loop broke before reaching it
}

TEST_CASE("the mirror case reports when the display rider is doing the lapping") {
    const std::vector<Rider> field = { rider(1, 5, 0.50f), rider(9, 6, 0.48f) };

    SUBCASE("player is the lapper closing on a backmarker") {
        Out out;
        out.run(field, 6, 0.05f, Player{ 9, 6, 0.48f, /*active=*/true });
        CHECK(out.playerLapping);
    }
    SUBCASE("player is too far back") {
        Out out;
        out.run(field, 6, 0.05f, Player{ 9, 6, 0.20f, true });
        CHECK_FALSE(out.playerLapping);
    }
    SUBCASE("player is the one BEING lapped, not lapping") {
        Out out;
        out.run(field, 6, 0.05f, Player{ 1, 5, 0.50f, true });
        CHECK_FALSE(out.playerLapping);
        CHECK(out.isFlagged(1));         // they are flagged, though
    }
    SUBCASE("an inactive player never reports lapping") {
        Out out;
        out.run(field, 6, 0.05f, Player{ 9, 6, 0.48f, /*active=*/false });
        CHECK_FALSE(out.playerLapping);
    }
}

TEST_CASE("outputs are cleared, so reused containers don't leak stale results") {
    // PluginData reuses these containers every rebuild (~30Hz) to avoid
    // allocating. If detect() stopped clearing them, a rider would stay
    // blue-flagged forever after one close pass.
    Out out;
    out.run({ rider(1, 5, 0.50f), rider(2, 6, 0.48f) }, 6, 0.05f);
    REQUIRE(out.isFlagged(1));

    out.run({ rider(1, 6, 0.50f), rider(2, 6, 0.48f) }, 6, 0.05f);   // now same lap
    CHECK(out.flagged.empty());
    CHECK(out.lapperToLapped.empty());
    CHECK_FALSE(out.playerLapping);
}

TEST_CASE("finished riders lap nobody") {
    // The live lap count stops meaning "race progress" once the leader takes
    // the flag: a finished rider keeps completing cool-down laps while
    // everyone still racing is one crossing behind by definition. These pin
    // the demo-weekend transcript bug where P2, racing to the flag on the
    // lead lap, was blue-flagged for the winner cruising to the pits
    // ("Blue flag, faster rider closing, rider ninety nine" eleven
    // milliseconds after "Leader's taken the flag").

    SUBCASE("P2 at the leader's finish is not lapped by the finished winner") {
        // Leader #99 crossed for 5 laps and finished; #12 (P2) holds 4 only
        // because their own final crossing hasn't happened yet. 5 >= 4+1 held
        // before the finished bar, which is exactly the false flag.
        Out out;
        out.run({ rider(12, 4, 0.50f),
                  rider(99, 5, 0.48f, /*active=*/true, /*eligible=*/false,
                        /*finished=*/true) },
                5, 0.05f);
        CHECK_FALSE(out.isFlagged(12));
        CHECK(out.lapperToLapped.empty());
    }

    SUBCASE("cool-down laps do not compound into a phantom deficit") {
        // The finished winner keeps crossing the line; by the time the last
        // riders finish they read several laps "up". Barred is barred, at any
        // margin.
        Out out;
        out.run({ rider(12, 4, 0.50f),
                  rider(99, 7, 0.48f, /*active=*/true, /*eligible=*/false,
                        /*finished=*/true) },
                7, 0.05f);
        CHECK_FALSE(out.isFlagged(12));
    }

    SUBCASE("a genuinely lapped rider still yields to a rider racing to the flag") {
        // The bar must not over-reach: #90 (two laps down, not finished) is
        // still caught by unfinished P2 — that flag is correct and stays.
        Out out;
        out.run({ rider(90, 2, 0.50f),
                  rider(12, 4, 0.48f) }, 5, 0.05f);
        CHECK(out.isFlagged(90));
        CHECK(out.lapperToLapped.at(12) == 90);
    }

    SUBCASE("a finished player is not 'lapping' the backmarkers they roll past") {
        // Mirror case, same rule: cruising after the flag is not lapping.
        Out out;
        out.run({ rider(90, 2, 0.52f) }, 5, 0.05f,
                Player{ 99, 5, 0.50f, /*active=*/true, /*finished=*/true });
        CHECK_FALSE(out.playerLapping);

        // And the identical geometry with an unfinished player still reports.
        out.run({ rider(90, 2, 0.52f) }, 5, 0.05f,
                Player{ 99, 5, 0.50f, /*active=*/true });
        CHECK(out.playerLapping);
    }
}

TEST_CASE("an empty field is handled without touching the outputs") {
    Out out;
    out.run({}, 0, 0.05f);
    CHECK(out.flagged.empty());
    CHECK(out.lapperToLapped.empty());
    CHECK_FALSE(out.playerLapping);
}
