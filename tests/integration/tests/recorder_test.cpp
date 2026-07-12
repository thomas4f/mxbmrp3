// ============================================================================
// tests/integration/tests/recorder_test.cpp
// End-to-end test for the in-plugin callback-tape recorder (GAME_HAS_RECORDER),
// which replaced the old standalone mxbmrp3_record.dlo plugin. In one flow:
//   1. DISABLED: with the recorder off (the default), the mxb_api taps do nothing
//      and no tape is written (exercises the fast-path guards).
//   2. FORMAT: with recording on, a known synthetic event stream produces a
//      well-formed MXBHREC tape — asserted by parsing the RAW bytes (magic,
//      framing, per-type counts, payload sizes, the compound RaceClassification /
//      RaceTrackPosition packings, and RaceHoleshot).
//   3. REPLAY: replaying that tape dispatches every event (including RaceHoleshot)
//      and reconstructs the standings.
//
// IMPORTANT — this test uses exactly ONE PluginHost and calls shutdown() before it
// unloads. Do NOT add a second host or drop the shutdown():
//   - Each PluginHost is a LoadLibrary + FreeLibrary. A FreeLibrary WITHOUT a prior
//     Shutdown() runs the plugin's static-destructor teardown, which under CI's Wine
//     is layout-sensitive and crashes (observed twice: a near-null fault in the
//     mingw CRT's tlsmthread.c, then in ntdll's loader) once the process does it
//     more than once. The game only ever loads the DLL once, so this is a test
//     artifact, not a plugin bug.
//   - Calling host.shutdown() first tears the plugin down cleanly while the
//     singletons are alive (m_bInitialized=false), so the later FreeLibrary's
//     static destructors are inert — the same proven-safe pattern teardown_test
//     uses. Fresh-from-empty replay reconstruction is already covered by
//     replay_golden_test / replay_test with committed tapes.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include "assertions.h"

namespace {
// The same synthetic grid used by race_test's phase 1, so the expected standings
// are a known-good reference: Alice P1, Bob +1.5, Carol +3.2.
void driveGrid(PluginHost& h) {
    h.eventInit("TestTrack", "Alice");
    h.raceEvent("TestTrack");
    h.session(/*session=*/6, /*numLaps=*/10, /*lengthMs=*/0);
    h.addEntry(7,  "Carol");
    h.addEntry(22, "Bob");
    h.addEntry(10, "Alice");
    h.classify(6, 300000, {
        { .num = 10, .best = 90000, .laps = 5, .gap = 0 },
        { .num = 22, .best = 91000, .laps = 5, .gap = 1500 },
        { .num = 7,  .best = 92500, .laps = 5, .gap = 3200 },
    });
}

const std::vector<ExpectRow> kExpected = {
    { 1, 10, "Alice", "Leader", "1:30.000" },
    { 2, 22, "Bob",   "+1.500", "1:31.000" },
    { 3,  7, "Carol", "+3.200", "1:32.500" },
};

bool fileExists(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (f) { fclose(f); return true; }
    return false;
}

// A parsed tape: the header + every [EventHeader, payload] framed to EOF. Uses the
// harness's own tape:: structs (the byte-for-byte contract with the recorder), so
// this validates the ON-DISK format directly, not just what replay reconstructs.
struct ParsedTape {
    bool ok = false;               // magic + version valid and the framing walked cleanly to EOF
    tape::FileHeader header{};
    std::vector<tape::EventHeader> events;
    std::vector<std::vector<uint8_t>> payloads;   // parallel to events

    int count(tape::EventType t) const {
        int n = 0;
        for (const auto& e : events) if (e.eventType == (uint32_t)t) ++n;
        return n;
    }
    // First event of a type, or -1.
    int find(tape::EventType t) const {
        for (size_t i = 0; i < events.size(); ++i)
            if (events[i].eventType == (uint32_t)t) return (int)i;
        return -1;
    }
};

ParsedTape parseTape(const std::string& path) {
    ParsedTape p;
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return p;
    std::vector<uint8_t> buf;
    { char tmp[8192]; size_t r; while ((r = fread(tmp, 1, sizeof(tmp), f)) > 0) buf.insert(buf.end(), tmp, tmp + r); }
    fclose(f);
    if (buf.size() < sizeof(tape::FileHeader)) return p;
    std::memcpy(&p.header, buf.data(), sizeof(tape::FileHeader));
    if (std::memcmp(p.header.magic, "MXBHREC\0", 8) != 0 || p.header.version != 1) return p;
    size_t off = sizeof(tape::FileHeader);
    while (off + sizeof(tape::EventHeader) <= buf.size()) {
        tape::EventHeader eh{};
        std::memcpy(&eh, buf.data() + off, sizeof(eh));
        off += sizeof(eh);
        if (off + eh.dataSize > buf.size()) return p;   // truncated payload → framing broken
        p.events.push_back(eh);
        p.payloads.emplace_back(buf.begin() + off, buf.begin() + off + eh.dataSize);
        off += eh.dataSize;
    }
    p.ok = (off == buf.size());   // consumed exactly to EOF: every frame's size was right
    return p;
}
}  // namespace

TEST_CASE("recorder: off writes nothing; on writes a well-formed tape that replays") {
    const std::string dir  = "Z:\\tmp\\mxbmrp3-tests\\recorder\\";
    const std::string tape = dir + "recorder.tape";
    std::remove(tape.c_str());   // clean slate (no stale file from a prior run)

    PluginHost h(dllPath());
    REQUIRE(h.loaded());
    h.startup(dir.c_str());   // [Recorder] not enabled -> beginSessionRecording is a no-op

    // (1) DISABLED: drive a full grid with recording OFF. Every mxb_api tap calls
    // its record* method, which must early-out (the fast-path guards) — no work,
    // no crash, and no file, since nothing opened the tape.
    driveGrid(h);
    h.raceTrackPosition({ {10, 0.9f}, {22, 0.6f} });
    for (int i = 0; i < 5; ++i) h.draw();
    CHECK(fileExists(tape) == false);   // recording off => the recorder stayed silent

    // (2) RECORD: enable and drive a known, hand-counted stream — one export per
    // driver call (classify() also flushes a Draw). Because no file existed before
    // startRecording, everything in the tape is post-enable: proof the disabled taps
    // above wrote nothing, not just that a file was overwritten.
    REQUIRE(h.startRecording(tape));
    h.eventInit("TestTrack", "Alice");                 // 1x EventInit
    h.raceEvent("TestTrack");                          // 1x RaceEvent
    h.session(6, 10, 0);                               // 1x RaceSession
    h.addEntry(7, "Carol");                            // 3x RaceAddEntry
    h.addEntry(22, "Bob");
    h.addEntry(10, "Alice");
    h.classify(6, 300000, {                            // 1x RaceClassification (3) + 1x Draw
        { .num = 10, .best = 90000, .laps = 5, .gap = 0 },
        { .num = 22, .best = 91000, .laps = 5, .gap = 1500 },
        { .num = 7,  .best = 92500, .laps = 5, .gap = 3200 },
    });
    h.raceTrackPosition({ {10, 0.9f}, {22, 0.6f}, {7, 0.3f} });  // 1x RaceTrackPosition (3)
    h.raceHoleshot(/*raceNum=*/10, /*timeMs=*/4200);   // 1x RaceHoleshot (recorded, no plugin action)
    h.stopRecording();

    // (3) FORMAT: parse the raw bytes and assert the on-disk framing is exactly right.
    ParsedTape t = parseTape(tape);
    REQUIRE(t.ok);                                       // magic + version ok, framed cleanly to EOF
    REQUIRE(t.events.size() > 0);
    CHECK(t.header.numEvents == (uint32_t)t.events.size());   // finalized header count is self-consistent

    CHECK(t.count(tape::EventType::EventInit)         == 1);
    CHECK(t.count(tape::EventType::RaceEvent)         == 1);
    CHECK(t.count(tape::EventType::RaceSession)       == 1);
    CHECK(t.count(tape::EventType::RaceAddEntry)      == 3);
    CHECK(t.count(tape::EventType::RaceClassification)== 1);
    CHECK(t.count(tape::EventType::RaceTrackPosition) == 1);

    // Simple typed events carry exactly one struct.
    CHECK(t.events[t.find(tape::EventType::EventInit)].dataSize == sizeof(SPluginsBikeEvent_t));
    CHECK(t.events[t.find(tape::EventType::RaceSession)].dataSize == sizeof(SPluginsRaceSession_t));

    // Compound packings (the trickiest part of the format): prefix + N structs.
    {
        int i = t.find(tape::EventType::RaceClassification);
        REQUIRE(i >= 0);
        REQUIRE(t.payloads[i].size() >= sizeof(tape::ClassificationPrefix));
        tape::ClassificationPrefix pre{};
        std::memcpy(&pre, t.payloads[i].data(), sizeof(pre));
        CHECK(pre.numEntries == 3);
        CHECK(t.events[i].dataSize ==
              sizeof(tape::ClassificationPrefix) + 3 * sizeof(SPluginsRaceClassificationEntry_t));
    }
    {
        int i = t.find(tape::EventType::RaceTrackPosition);
        REQUIRE(i >= 0);
        tape::TrackPositionPrefix pre{};
        std::memcpy(&pre, t.payloads[i].data(), sizeof(pre));
        CHECK(pre.numVehicles == 3);
        CHECK(t.events[i].dataSize ==
              sizeof(tape::TrackPositionPrefix) + 3 * sizeof(SPluginsRaceTrackPosition_t));
    }

    // RaceHoleshot: the game doesn't fire it today and the plugin takes no action on
    // it, but it MUST still land on the tape (a master must stay complete if PiBoSo
    // ever starts sending it). Verify it's captured with the right struct + values.
    CHECK(t.count(tape::EventType::RaceHoleshot) == 1);
    {
        int i = t.find(tape::EventType::RaceHoleshot);
        REQUIRE(i >= 0);
        CHECK(t.events[i].dataSize == sizeof(SPluginsRaceHoleshot_t));
        SPluginsRaceHoleshot_t hs{};
        std::memcpy(&hs, t.payloads[i].data(), sizeof(hs));
        CHECK(hs.m_iRaceNum == 10);
        CHECK(hs.m_iTime == 4200);
    }

    // (4) REPLAY: feed the recorded tape back through the real exports. The replayer
    // must dispatch EVERY event — including RaceHoleshot (it would be skipped, and
    // applied < total, if replay didn't handle it) — and the grid still holds.
    const int applied = h.replayTape(tape);
    CHECK(applied == (int)t.events.size());
    {
        auto d = h.snapshot();
        REQUIRE(d.is_object());
        checkStandings(d, kExpected);
    }

    // Clean teardown BEFORE the host unloads: tears the plugin down while every
    // singleton is alive, so the FreeLibrary in ~PluginHost runs inert static
    // destructors and can't fault (see the note at the top).
    h.shutdown();
}
