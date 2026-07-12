// ============================================================================
// tests/integration/tests/replay_golden_test.cpp
// Real-data golden master. Replays a REAL callback tape captured in-game by
// the recorder (a 1-lap Race 2 on MXB Club, local player #4 "Thomas" on a
// Husqvarna, finishing P1) and asserts the plugin reconstructs that result. This
// is the fidelity anchor for the synthetic tests: it proves the plugin processes
// the exact callback stream the game produces, not just our hand-authored one.
//
// The tape is committed gzipped (MXBHREC recorder format); run_tests.sh unpacks
// fixtures to Z:\tmp\mxbmrp3-tests\fixtures\. It was slimmed to the state-changing
// events (telemetry/vehicle/draw/track-position dropped) — verified to yield the
// identical /api/state as the full 9 MB capture. Ground truth cross-checked
// against the session log. See TESTING.md (Layer 2 → callback tapes).
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include "assertions.h"

TEST_CASE("replay golden: a real captured session reconstructs its result") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\replaygold\\");

    const int applied =
        host.replayTape("Z:\\tmp\\mxbmrp3-tests\\fixtures\\race2_mxbclub_1lap.tape");
    CHECK(applied == 8238);   // the slimmed tape's state-changing event count

    auto d = host.snapshot();
    REQUIRE(d.is_object());

    // Session: a 1-lap Race 2 on MXB Club, over.
    CHECK(d["session"].value("type", std::string()) == "Race 2");
    CHECK(d["session"].value("state", std::string()) == "Race Over");
    CHECK(d["session"].value("trackName", std::string()) == "MXB Club");
    CHECK(d["session"].value("isRace", false) == true);

    // Result: the lone rider #4 Thomas finished P1 on a Husqvarna, lap 1:33.889.
    const auto st = d.value("standings", nlohmann::json::array());
    REQUIRE(st.size() == 1);
    const auto& r = st[0];
    CHECK(r.value("num", -1) == 4);
    CHECK(r.value("pos", -1) == 1);
    CHECK(r.value("fullName", std::string()) == "Thomas");
    CHECK(r.value("finished", false) == true);
    CHECK(r.value("gap", std::string()) == "Leader");
    CHECK(r.value("bestLap", std::string()) == "1:33.889");
    CHECK(r.value("brand", std::string()) == "Husqvarna");
    CHECK(hasChip(r, "finished"));
    CHECK(hasChip(r, "fastest"));

    // The event narrative the plugin derived from the real stream.
    CHECK(hasEvent(d, "Race 2 started"));
    CHECK(hasEvent(d, "Race 2 ended"));
    CHECK(hasEvent(d, "#4 finished P1"));
    CHECK(hasEvent(d, "#4 fastest lap"));

    host.shutdown();
}
