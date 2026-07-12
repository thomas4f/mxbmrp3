// ============================================================================
// tests/integration/tests/replay_test.cpp
// Callback-tape replay: proves PluginHost::replayTape() reconstructs plugin
// state from a recorded MXBHREC tape (the format the in-plugin recorder captures
// in-game). Here the tape is SYNTHESIZED with TapeWriter so the round-trip runs
// without a game — it validates the tape read/dispatch machinery. The real
// value is the same path fed a genuine capture: drop a recorded .rec in and
// assert its snapshot (a real-data golden master). See TESTING.md.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include "assertions.h"
#include "tape.h"
#include <cstring>

static void put(char* dst, size_t cap, const char* s) {
    std::strncpy(dst, s, cap - 1); dst[cap - 1] = '\0';
}

TEST_CASE("replay: a recorded callback tape reconstructs plugin state") {
    const std::string tapePath = "Z:\\tmp\\mxbmrp3-tests\\replay\\race.rec";

    // --- Author a tape: the same callback stream race_test drives by hand. ----
    {
        tape::TapeWriter w;
        REQUIRE(w.open(tapePath));

        SPluginsBikeEvent_t ev{};
        put(ev.m_szRiderName, 100, "Alice"); put(ev.m_szBikeName, 100, "Test 450");
        put(ev.m_szCategory, 100, "MX1"); put(ev.m_szTrackName, 100, "TestTrack");
        ev.m_fTrackLength = 1600.0f; ev.m_iType = 2;
        w.writeSimple(tape::EventType::EventInit, &ev, sizeof(ev));

        SPluginsRaceEvent_t re{};
        re.m_iType = 2; put(re.m_szName, 100, "TestTrack");
        put(re.m_szTrackName, 100, "TestTrack"); re.m_fTrackLength = 1600.0f;
        w.writeSimple(tape::EventType::RaceEvent, &re, sizeof(re));

        SPluginsRaceSession_t ss{};
        ss.m_iSession = 6; ss.m_iSessionState = 16; ss.m_iSessionNumLaps = 10;
        w.writeSimple(tape::EventType::RaceSession, &ss, sizeof(ss));

        // Entries added in reverse finishing order (as race_test does).
        for (auto& r : { std::pair<int, const char*>{7, "Carol"},
                         { 22, "Bob" }, { 10, "Alice" } }) {
            SPluginsRaceAddEntry_t e{};
            e.m_iRaceNum = r.first; put(e.m_szName, 100, r.second);
            put(e.m_szBikeName, 100, "Test 450"); put(e.m_szBikeShortName, 100, "T450");
            put(e.m_szCategory, 100, "MX1"); e.m_iNumberOfGears = 5; e.m_iMaxRPM = 13000;
            w.writeSimple(tape::EventType::RaceAddEntry, &e, sizeof(e));
        }

        SPluginsRaceClassification_t cls{};
        cls.m_iSession = 6; cls.m_iSessionState = 16; cls.m_iSessionTime = 300000;
        cls.m_iNumEntries = 3;
        std::vector<SPluginsRaceClassificationEntry_t> entries(3);
        const int nums[3] = { 10, 22, 7 }, best[3] = { 90000, 91000, 92500 }, gap[3] = { 0, 1500, 3200 };
        for (int i = 0; i < 3; ++i) {
            entries[i] = SPluginsRaceClassificationEntry_t{};
            entries[i].m_iRaceNum = nums[i]; entries[i].m_iBestLap = best[i];
            entries[i].m_iBestLapNum = 3; entries[i].m_iNumLaps = 5; entries[i].m_iGap = gap[i];
        }
        w.writeClassification(cls, entries);
        w.close();
    }

    // --- Replay it into a fresh plugin and assert the reconstructed state. -----
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\replay\\");

    const int applied = host.replayTape(tapePath);
    // EventInit + RaceEvent + RaceSession + 3x RaceAddEntry + RaceClassification.
    CHECK(applied == 7);

    auto d = host.snapshot();
    REQUIRE(d.is_object());
    CHECK(d["session"].value("type", std::string()) == "Race 1");
    CHECK(d["session"].value("time", std::string()) == "05:00");
    checkStandings(d, {
        { 1, 10, "Alice", "Leader", "1:30.000" },
        { 2, 22, "Bob",   "+1.500", "1:31.000" },
        { 3,  7, "Carol", "+3.200", "1:32.500" },
    });

    host.shutdown();
}
