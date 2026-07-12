// ============================================================================
// tests/integration/tests/replay_golden_multi_test.cpp
// Real-data golden master — a FULL 24-rider race. Replays a real in-game capture
// (Race 2 on Farm14, recorded via the in-plugin recorder) and asserts the plugin
// reconstructs the whole classification: the winner, time gaps, the fastest-lap
// chip on a non-winner, a real penalty, a lapped rider, and DSQ/DNS/retired
// states. Every value here is cross-checked against the session log — it asserts
// what actually happened, not just what the plugin decided.
//
// The strongest fidelity anchor for the synthetic tests: it exercises the entire
// standings/gap/state pipeline on real data at once. Tape committed gzipped
// (min-profile slim — state-changing events; see tests/integration/slim_tape.py);
// run_tests.sh unpacks fixtures. See TESTING.md.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include "assertions.h"

TEST_CASE("replay golden (24-rider race): full real classification reconstructs") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\replaygoldmulti\\");

    const int applied =
        host.replayTape("Z:\\tmp\\mxbmrp3-tests\\fixtures\\race_farm14_24riders.tape");
    CHECK(applied == 29908);

    auto d = host.snapshot();
    REQUIRE(d.is_object());

    CHECK(d["session"].value("type", std::string()) == "Race 2");
    CHECK(d["session"].value("trackName", std::string()) == "Farm14");

    const auto st = d.value("standings", nlohmann::json::array());
    CHECK(st.size() == 23);

    // Podium (winner + leader-relative gaps).
    auto p1 = riderByNum(d, 147);
    CHECK(p1.value("pos", -1) == 1);
    CHECK(p1.value("fullName", std::string()) == "Hjalleballe");
    CHECK(p1.value("gap", std::string()) == "Leader");
    CHECK(p1.value("finished", false) == true);
    CHECK(p1.value("bestLap", std::string()) == "1:28.118");

    auto p2 = riderByNum(d, 816);
    CHECK(p2.value("pos", -1) == 2);
    CHECK(p2.value("gap", std::string()) == "+11.323");
    // Set the fastest lap (1:26.059) despite finishing 2nd — so it owns the chip.
    CHECK(p2.value("bestLap", std::string()) == "1:26.059");
    CHECK(hasChip(p2, "fastest"));

    auto p3 = riderByNum(d, 53);
    CHECK(p3.value("pos", -1) == 3);
    CHECK(p3.value("gap", std::string()) == "+23.343");
    CHECK(p3.value("penaltyMs", -1) == 5000);   // a real "Cutting" penalty
    CHECK(hasChip(p3, "penalty"));

    // A lapped rider still classified + finished.
    auto lapped = riderByNum(d, 86);
    CHECK(lapped.value("gap", std::string()) == "+1L");
    CHECK(lapped.value("finished", false) == true);

    // Special end states (state ints: 4=DSQ, 1=DNS, 3=retired).
    CHECK(riderByNum(d, 13).value("gap", std::string()) == "DSQ");
    CHECK(riderByNum(d, 13).value("state", -1) == 4);
    CHECK(riderByNum(d, 236).value("gap", std::string()) == "DNS");
    CHECK(riderByNum(d, 236).value("state", -1) == 1);

    auto ret = riderByNum(d, 4);                 // the local player retired
    CHECK(ret.value("fullName", std::string()) == "Thomas");
    CHECK(ret.value("gap", std::string()) == "RET");
    CHECK(ret.value("state", -1) == 3);

    host.shutdown();
}
