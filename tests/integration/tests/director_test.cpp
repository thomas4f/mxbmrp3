// ============================================================================
// tests/integration/tests/director_test.cpp
// Auto-director battle detection (PluginData::getBattleGroups) — the core of the
// broadcast director. Drives a race with gaps engineered into two distinct
// close-running groups separated by a clear break, then asserts `battles[]` is
// exactly those two groups and the director advisory block is present but inert
// by default. Battles are derived from the standings gaps, deterministically.
// Self-contained doctest; see run_tests.sh.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include "assertions.h"

TEST_CASE("director: battle detection splits two groups at the gap break") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\director\\");

    host.eventInit("TestTrack", "Cam");
    host.raceEvent("TestTrack");
    host.session(/*session=*/6, /*numLaps=*/10, /*lengthMs=*/0);

    // 6 riders. Gaps-to-leader engineered so consecutive diffs are <= 2500
    // (battle, default battleGapMs) within two groups, with a 5500ms break:
    //   P1..P3 (#10,#22,#7): 0,1000,2500   -> battle [10,22,7]
    //   P4..P6 (#3,#5,#9):   8000,9000,9500 -> battle [3,5,9]  (P3->P4 = 5500)
    struct G { int num, gap; };
    const G grid[6] = { {10,0},{22,1000},{7,2500},{3,8000},{5,9000},{9,9500} };
    for (const auto& g : grid) {
        char nm[32]; snprintf(nm, sizeof(nm), "Rider%d", g.num);
        host.addEntry(g.num, nm);
    }

    std::vector<ClassRow> rows;
    for (int i = 0; i < 6; ++i)
        rows.push_back({ .num = grid[i].num, .best = 90000 + i * 100, .laps = 3, .gap = grid[i].gap });
    host.classify(6, 200000, rows);

    auto d = host.snapshot();
    REQUIRE(d.is_object());

    // Two battles, split at the 5500ms break.
    const std::vector<std::vector<int>> want = { {10, 22, 7}, {3, 5, 9} };
    CHECK(d.value("battles", nlohmann::json::array()) == nlohmann::json(want));

    // Director advisory: present, and inert by default (feature off until enabled).
    const nlohmann::json dir = d.value("director", nlohmann::json::object());
    CHECK(dir.contains("on"));
    CHECK(dir.value("on", true) == false);
    CHECK(dir.value("active", true) == false);
    CHECK(dir.value("subject", 0) == -1);

    host.shutdown();
}
