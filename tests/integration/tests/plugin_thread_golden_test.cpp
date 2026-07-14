// ============================================================================
// tests/integration/tests/plugin_thread_golden_test.cpp
// Threaded twin of replay_golden_test.cpp. Replays the SAME real captured tape
// (a 1-lap Race 2 on MXB Club, #4 "Thomas" finishing P1) but with the
// EXPERIMENTAL plugin worker thread ON — every one of the ~8238 recorded
// callbacks is copied across the queue and applied on the worker thread instead
// of inline. It asserts the plugin reconstructs the IDENTICAL result.
//
// This is the strongest equivalence check for the off-thread path: the sync path
// is already pinned to these exact golden values by replay_golden_test, so
// matching them here on the real callback stream proves the worker-thread path is
// functionally identical — no event dropped, reordered, or raced across the
// queue — on data far richer than any hand-authored scenario.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include "assertions.h"

TEST_CASE("plugin thread golden: real captured session reconstructs identically off-thread") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\ptgold\\");

    // Turn the worker on before replaying, and confirm it actually spawned (so this
    // can't silently degrade into a second copy of the sync golden test).
    host.pluginThreadEnable();
    REQUIRE(host.pluginThreadEnabled());

    const int applied =
        host.replayTape("Z:\\tmp\\mxbmrp3-tests\\fixtures\\race2_mxbclub_1lap.tape");
    CHECK(applied == 8238);   // identical dispatch count to the sync golden

    // Barrier: let the worker finish applying all ~8238 queued callbacks before we
    // read state (in the game there is deliberately no such wait — that's the point).
    host.pluginThreadFlush();

    auto d = host.snapshot();
    REQUIRE(d.is_object());

    // --- identical to replay_golden_test.cpp's assertions --------------------
    CHECK(d["session"].value("type", std::string()) == "Race 2");
    CHECK(d["session"].value("state", std::string()) == "Race Over");
    CHECK(d["session"].value("trackName", std::string()) == "MXB Club");
    CHECK(d["session"].value("isRace", false) == true);

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

    CHECK(hasEvent(d, "Race 2 started"));
    CHECK(hasEvent(d, "Race 2 ended"));
    CHECK(hasEvent(d, "#4 finished P1"));
    CHECK(hasEvent(d, "#4 fastest lap"));

    host.pluginThreadStop();
    host.shutdown();
}
