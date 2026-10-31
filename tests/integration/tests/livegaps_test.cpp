// ============================================================================
// tests/integration/tests/livegaps_test.cpp
// Live-gap DATA CONTRACT for the web overlay. The overlay's battle cards can show
// real-time intervals instead of official splits; the plugin ALWAYS surfaces the
// raw per-rider live gap + a validity flag in /api/state (the on/off is a purely
// client-side overlay setting, so it's not in the plugin's contract). This pins
// that contract (via host.snapshot(), no HTTP server):
//
//   * per rider: liveGapMs (leader-relative real-time gap, 0 for the leader) and
//     liveGapValid — whether liveGapMs is trustworthy right now.
//
// liveGapValid is true for the leader (its 0 is valid data) and for any rider in
// the latest track-position batch with a computed same-lap gap; it
// is FALSE for a rider that dropped out of the batch (stale gap → fall back to the
// official split) or one that's lapped. This is the overlay-facing companion to
// trackpos_stale_test, which pins the underlying realTimeGap value itself.
//
// Also here: Standings' "Live updates: Positions" (standings_live_order.h),
// end to end from a hand-written INI - a pass the track positions have held
// moves the rows before the game's next classification; Gaps keeps the
// official order.
// Self-contained doctest; see run_tests.sh.
// ============================================================================
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include "assertions.h"

#include <algorithm>
#include <string>
#include <thread>

static constexpr int RACE1 = 6;

TEST_CASE("live gaps: per-rider liveGapMs/liveGapValid contract") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\livegaps\\");

    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(RACE1, /*numLaps=*/10, /*lengthMs=*/0);   // lap-based
    host.addEntry(10, "Alice");   // leader
    host.addEntry(22, "Bob");     // follower

    // --- Phase 1: both in the batch → both have valid live gaps --------------
    const std::vector<ClassRow> lap1 = {
        { .num = 10, .laps = 1, .gap = 0 },
        { .num = 22, .laps = 1, .gap = 500 },
    };
    host.classify(RACE1, 1000, lap1);
    host.raceTrackPosition({ { 10, 0.20f }, { 22, 0.10f } });   // leader stamps 0.20 @ 1000
    host.classify(RACE1, 2000, lap1);
    host.raceTrackPosition({ { 10, 0.40f }, { 22, 0.20f } });   // Bob reaches 0.20 → gap 1000
    {
        auto d = host.snapshot();
        REQUIRE(d.is_object());

        auto leader = riderByNum(d, 10);
        CHECK(leader.value("liveGapMs", -1) == 0);              // leader is the reference
        CHECK(leader.value("liveGapValid", false) == true);     // its 0 is valid data

        auto bob = riderByNum(d, 22);
        CHECK(bob.value("liveGapMs", -1) == 1000);              // matches the computed real-time gap
        CHECK(bob.value("liveGapValid", false) == true);
        // The JSON field mirrors the internal value read via the white-box hook.
        CHECK(bob.value("liveGapMs", -1) == host.realTimeGap(22));
    }

    // --- Phase 2: Bob drops out of the batch → his live gap is not valid ------
    // (leader still reporting, so gaps still compute for whoever IS in the batch)
    host.classify(RACE1, 4000, lap1);
    host.raceTrackPosition({ { 10, 0.60f } });                  // Bob absent
    {
        auto d = host.snapshot();
        auto bob = riderByNum(d, 22);
        CHECK(bob.value("liveGapValid", true) == false);        // stale → client uses official
        CHECK(riderByNum(d, 10).value("liveGapValid", false) == true);   // leader still valid
    }

    // --- Phase 3: Bob is a full lap down → live gap meaningless across laps ---
    const std::vector<ClassRow> lapped = {
        { .num = 10, .laps = 2, .gap = 0 },
        { .num = 22, .laps = 1, .gap = 0, .gapLaps = 1 },
    };
    host.classify(RACE1, 6000, lapped);
    host.raceTrackPosition({ { 10, 0.20f }, { 22, 0.80f } });   // both in the batch this time
    {
        auto d = host.snapshot();
        CHECK(riderByNum(d, 22).value("liveGapValid", true) == false);   // lapped → invalid
    }

    // --- Phase 4 (regression): removeRaceEntry must evict the "active" bit ----
    // Bob was in the last batch, so his active-track-pos bit is set. If he leaves
    // and a NEW rider joins reusing #22 while no batches arrive (the player could
    // be sitting in menus — no callbacks flow there to refresh the set), the
    // rejoiner must NOT inherit the stale bit. removeRaceEntry() missed this set.
    REQUIRE(host.hasActiveTrackPos(22) == 1);   // set by the phase-3 batch
    host.removeEntry(22);
    host.addEntry(22, "Carl");                  // raceNum reuse, no new batch
    CHECK(host.hasActiveTrackPos(22) == 0);     // fresh rider, no stale "active" bit

    host.shutdown();
}

// The Standings rows' race numbers, top to bottom, read from the drawn strings.
static std::vector<int> standingsRows(PluginHost& host, const std::vector<int>& nums) {
    std::vector<std::pair<double, int>> found;
    for (const auto& r : host.hudStringRows("standings_hud")) {
        for (int n : nums) {
            if (r.text == std::to_string(n)) found.push_back({ r.y, n });
        }
    }
    std::sort(found.begin(), found.end());
    std::vector<int> out;
    for (const auto& f : found) out.push_back(f.second);
    return out;
}

TEST_CASE("live updates: Both moves a held pass up before the game does") {
    const char* save = "Z:\\tmp\\mxbmrp3-tests\\liveorder\\";
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.hasStringRows());
    REQUIRE(host.startup(save) >= 0);
    host.writeSettingsFile(save, "[Settings]\nversion=10\n\n[StandingsHud]\nvisible=1\nliveGaps=2\n"
                                 "posGainMode=RACE_START\n");
    host.loadSettings(save);

    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.addEntry(10, "Alice");
    host.addEntry(22, "Bob");
    host.addEntry(33, "Carl");
    const std::vector<int> nums = { 10, 22, 33 };
    // The grid, snapshotted for Positions gained on the green flag (see
    // spotter_test's race-start case for why it starts in PRE_START)
    host.session(RACE1, /*numLaps=*/10, /*lengthMs=*/0, /*state=*/256);
    host.classify(RACE1, 0, { { .num = 10, .laps = 0 }, { .num = 22, .laps = 0 }, { .num = 33, .laps = 0 } },
                  /*sessionState=*/256);
    host.raceSessionState(RACE1, 16);
    const std::vector<ClassRow> official = {
        { .num = 10, .laps = 1, .gap = 0 },
        { .num = 22, .laps = 1, .gap = 400 },
        { .num = 33, .laps = 1, .gap = 900 },
    };
    host.classify(RACE1, 1000, official);
    host.raceTrackPosition({ { 10, 0.30f }, { 22, 0.29f }, { 33, 0.20f } });
    host.draw();
    REQUIRE(standingsRows(host, nums) == std::vector<int>{ 10, 22, 33 });

    // Bob goes by mid-sector and stays ahead; the game's order still has Alice
    // ahead. Batches every 50 ms, as the game sends them (about one a frame).
    float pos = 0.40f;
    host.raceTrackPosition({ { 10, pos }, { 22, pos + 0.01f }, { 33, pos - 0.1f } });
    host.draw();
    CHECK(standingsRows(host, nums) == std::vector<int>{ 10, 22, 33 });   // not held yet
    for (int i = 0; i < 24; ++i) {   // 1.2 s: past the half-second hold
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        pos += 0.002f;
        host.raceTrackPosition({ { 10, pos }, { 22, pos + 0.01f }, { 33, pos - 0.1f } });
        host.draw();
    }
    CHECK(standingsRows(host, nums) == std::vector<int>{ 22, 10, 33 });
    // Positions gained counts the live move too: Alice's row, now P2, shows one
    // lost ("1" beside her "2"), not the game's nothing
    {
        double aliceY = -1.0;
        for (const auto& r : host.hudStringRows("standings_hud")) if (r.text == "10") aliceY = r.y;
        bool lostOne = false;
        for (const auto& r : host.hudStringRows("standings_hud")) lostOne = lostOne || (r.y == aliceY && r.text == "1");
        CHECK(lostOne);
    }
    // Every other consumer keeps the game's order
    {
        const auto st = host.snapshot().value("standings", nlohmann::json::array());
        REQUIRE(!st.empty());
        CHECK(st[0].value("num", -1) == 10);
    }

    // Gaps: the official order again, at once
    host.writeSettingsFile(save, "[Settings]\nversion=10\n\n[StandingsHud]\nvisible=1\nliveGaps=1\n");
    host.loadSettings(save);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));   // the 100 ms poll
    host.draw();
    CHECK(standingsRows(host, nums) == std::vector<int>{ 10, 22, 33 });

    host.shutdown();
}

// The drawn text of one HUD, top to bottom.
static bool hudShows(PluginHost& host, const char* hud, const std::string& text) {
    for (const auto& r : host.hudStringRows(hud)) {
        if (r.text == text) return true;
    }
    return false;
}

TEST_CASE("live updates: Both moves the Position widget and the Dash, with Standings hidden") {
    // The live order is Standings', but it runs for its other readers too: here
    // Standings is off and only the Position widget and the Dash's Standings page
    // show it.
    const char* save = "Z:\\tmp\\mxbmrp3-tests\\liveorder_readers\\";
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.hasStringRows());
    REQUIRE(host.startup(save) >= 0);
    host.writeSettingsFile(save, "[Settings]\nversion=10\n\n[Messages]\ndashTried=1\n\n"
                                 "[StandingsHud]\nvisible=0\nliveGaps=2\n\n"
                                 "[DashHud]\nvisible=1\npage=standings\npage_standings=1\n"
                                 "page_names=0\nnotice_takeover=0\n");
    host.loadSettings(save);
    REQUIRE(host.setHudVisible("position_widget", true));
    REQUIRE(host.setHudVisible("dash_hud", true));

    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(RACE1, /*numLaps=*/10, /*lengthMs=*/0);
    host.addEntry(10, "Alice");
    host.addEntry(22, "Bob");
    host.addEntry(33, "Carl");
    const std::vector<int> nums = { 10, 22, 33 };
    host.classify(RACE1, 1000, {
        { .num = 10, .laps = 1, .gap = 0 },
        { .num = 22, .laps = 1, .gap = 400 },
        { .num = 33, .laps = 1, .gap = 900 },
    });
    host.raceTrackPosition({ { 10, 0.30f }, { 22, 0.29f }, { 33, 0.20f } });
    host.draw();
    host.draw();
    REQUIRE(hudShows(host, "position_widget", "1/3"));
    auto dashRows = [&] {
        std::vector<std::pair<double, int>> found;
        for (const auto& r : host.hudStringRows("dash_hud")) {
            for (int n : nums) {
                if (r.text == std::to_string(n)) found.push_back({ r.y, n });
            }
        }
        std::sort(found.begin(), found.end());
        std::vector<int> out;
        for (const auto& f : found) out.push_back(f.second);
        return out;
    };
    REQUIRE(dashRows() == std::vector<int>{ 10, 22, 33 });

    // Bob goes by Alice, the player, and stays ahead
    float pos = 0.40f;
    for (int i = 0; i < 25; ++i) {   // 1.2 s: past the half-second hold
        host.raceTrackPosition({ { 10, pos }, { 22, pos + 0.01f }, { 33, pos - 0.1f } });
        host.draw();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        pos += 0.002f;
    }
    host.draw();
    CHECK(hudShows(host, "position_widget", "2/3"));
    CHECK(dashRows() == std::vector<int>{ 22, 10, 33 });

    // Gaps: the official order again
    host.writeSettingsFile(save, "[Settings]\nversion=10\n\n[Messages]\ndashTried=1\n\n"
                                 "[StandingsHud]\nvisible=0\nliveGaps=1\n\n"
                                 "[DashHud]\nvisible=1\npage=standings\npage_standings=1\n"
                                 "page_names=0\nnotice_takeover=0\n");
    host.loadSettings(save);
    REQUIRE(host.setHudVisible("position_widget", true));
    REQUIRE(host.setHudVisible("dash_hud", true));
    host.draw();
    host.draw();
    CHECK(hudShows(host, "position_widget", "1/3"));
    CHECK(dashRows() == std::vector<int>{ 10, 22, 33 });

    host.shutdown();
}
