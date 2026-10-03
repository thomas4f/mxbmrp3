// ============================================================================
// tests/integration/tests/pb_gap_golden_test.cpp
// Real-data golden for the live gap to PB (core/pb_gap_tracker.h via PluginData).
//
// THE TAPE. forest_short_race2_practice_gaps.tape.gz is a recorder capture of
// one real session (Forest Raceway Short, local player #4 "thomas", 2026-09-16):
// a short practice with no laps, a 4-lap Race 2 from a STANDING START, then a
// practice from the pits with seven laps, one of them aborted by leaving to the
// pits and coming back out. Slimmed to the state-changers plus the player's
// run events, splits and track positions; positions kept at every 12th (~30 Hz)
// and classifications at every 8th, since everything asserted here is stated in
// track distance and holds at any callback rate (slim_tape.py --every) -- EXCEPT
// within 250 ms of each RaceLap, where every position is kept (--near), so the
// order of wrap and RaceLap at every line is the game's: five wrap-first, six
// completion-first, the practice's last four wrap-first in a row. Thinned there,
// the kept sample nearest the crossing decided the order, three lap ends had
// flipped to completion-first, and this test passed over the bug below.
//
// WHY THIS TEST EXISTS. The tracker's review found defects that no hand-written
// test had exercised because every synthetic test drove ONE callback order at
// the line. A real capture drives whatever order the game produced, at the real
// rate, with the real grid start and the real pit-out -- so this is the test that
// catches the class, not an instance. It replays on the recorded clock
// (PluginHost::replayTapeClocked feeds each event's timestamp to the lap timer),
// which is what makes the elapsed times, and so the gaps, the ones the game saw.
//
// WHAT IT PINS, from the session log the capture was made alongside:
//   - the race's opening lap (57.0 s, gate to line) shows NO gap: it is measured
//     from the gate and can never be the reference (the grid-start rule);
//   - from lap 2 on (42.4 s, the first reference) the gap is valid almost
//     everywhere, and at every line it matches the official lap delta against the
//     best eligible lap before it (lap 3: +136 ms, lap 4: -805 ms) within 0.5 s;
//   - the practice measures against ITS OWN laps (the session change resets the
//     reference), and after the pit-out the aborted lap counts for nothing while
//     the next full lap reads against the best before it;
//   - the fence holds for wrap-first endings in a ROW (practice laps 4-7), and
//     for the grid lap ending wrap-first (184.70 s): the completion that closes a
//     wrap-first ending used to leave "strays to shed" set, so the next wrap
//     wiped the whole lap and its completion committed the EMPTY table -- gap and
//     ghost blank for the rest of the session. The line order is asserted (5/6)
//     so a re-slim that thins the line fails here instead of narrowing the test;
//   - no valid gap in the first 3% of any lap exceeds 1 s (the spike the review
//     found read +3 s there), and no two consecutive samples inside a lap differ
//     by more than 1.5 s unless an official split re-anchored between them. The
//     bound is not tighter because the practice's reference lap (50.5 s, the
//     first of the session) has a stop in it: the rider spent 4.2 s between 0.78
//     and 0.80 of the track, so a later lap passing that spot at speed gains
//     about 1.1 s in two metres -- a true reading, not a fault. The faults this
//     guards against read tens of seconds.
// ============================================================================
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr int PLAYER = 4;
constexpr int RACE2 = 7;

struct LapEnd {
    int session;
    int lapIndex;        // 1-based within the session
    int lapTime;         // ms, 0 = aborted
    bool hadReference;   // a reference lap existed before this lap
    int expectedGap;     // lapTime - best eligible lap before it (when hadReference)
    bool lineGapValid;   // the last position sample before the RaceLap had a gap
    int lineGap;
    float linePos;       // where that sample was
};

template <typename T>
T readAs(const std::vector<uint8_t>& buf, size_t offset = 0) {
    T v{};
    std::memcpy(&v, buf.data() + offset, sizeof(T));
    return v;
}

}  // namespace

TEST_CASE("live gap to PB: a real grid-start race and pit-out practice replay clean") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\pbgapgold\\");

    // --- replay state -------------------------------------------------------
    int session = -1;
    int lapIndex = 0;              // RaceLaps seen for the player in this session
    int bestEligible = -1;         // best lap time so far that may be the reference
    std::vector<LapEnd> laps;

    bool haveLastValid = false;    // last valid gap sample in this lap
    int lastValidGap = 0;
    float lastValidPos = 0.0f;
    bool splitSinceLast = false;

    long samples = 0, validSamples = 0;   // over the whole tape
    float lastPos = -1.0f;
    uint64_t lastWrapUs = 0;
    int wrapFirstEnds = 0, completionFirstEnds = 0;   // the order at each of the 11 lines
    long gridLapSamples = 0, gridLapValid = 0;
    long lap3onSamples = 0, lap3onValid = 0;   // Race 2 laps 3-4: a reference exists throughout
    int worstNearLine = 0;         // max |gap| seen at trackPos < 0.03
    int worstJump = 0;             // max |delta gap| between consecutive samples, no split between
    int worstJumpAtSplit = 0;
    struct Where { int session = 0, lap = 0, prevGap = 0, gap = 0; float prevPos = 0, pos = 0; uint64_t tsUs = 0; };
    Where worstJumpAt, worstNearLineAt;

    auto onLapEnd = [&](int lapTime) {
        ++lapIndex;
        const bool gridLap = (session == RACE2 && lapIndex == 1);
        LapEnd e{ session, lapIndex, lapTime, bestEligible > 0,
                  bestEligible > 0 ? lapTime - bestEligible : 0,
                  haveLastValid, lastValidGap, lastValidPos };
        laps.push_back(e);
        if (lapTime > 0 && !gridLap && (bestEligible < 0 || lapTime < bestEligible)) {
            bestEligible = lapTime;
        }
        haveLastValid = false;
        splitSinceLast = false;
    };

    const int applied = host.replayTapeClocked(
        "Z:\\tmp\\mxbmrp3-tests\\fixtures\\forest_short_race2_practice_gaps.tape",
        [&](tape::EventType type, uint64_t tsUs, const std::vector<uint8_t>& buf) {
            switch (type) {
                case tape::EventType::RaceSession: {
                    const auto s = readAs<SPluginsRaceSession_t>(buf);
                    if (s.m_iSession != session) {
                        session = s.m_iSession;
                        lapIndex = 0;
                        bestEligible = -1;     // the plugin resets its reference with the timer
                        haveLastValid = false;
                    }
                    break;
                }
                case tape::EventType::RaceLap: {
                    const auto l = readAs<SPluginsRaceLap_t>(buf);
                    if (l.m_iRaceNum == PLAYER) {
                        // A wrap in the last 200 ms came first; else the wrap is still to come.
                        if (tsUs - lastWrapUs < 200000) ++wrapFirstEnds; else ++completionFirstEnds;
                        onLapEnd(l.m_iLapTime);
                    }
                    break;
                }
                case tape::EventType::RaceSplit: {
                    const auto sp = readAs<SPluginsRaceSplit_t>(buf);
                    if (sp.m_iRaceNum == PLAYER) splitSinceLast = true;
                    break;
                }
                case tape::EventType::RaceTrackPosition: {
                    const int n = readAs<int>(buf);
                    float pos = -1.0f;
                    for (int i = 0; i < n; ++i) {
                        const auto tp = readAs<SPluginsRaceTrackPosition_t>(
                            buf, sizeof(int) + i * sizeof(SPluginsRaceTrackPosition_t));
                        if (tp.m_iRaceNum == PLAYER) { pos = tp.m_fTrackPos; break; }
                    }
                    if (pos < 0.0f) break;
                    if (lastPos >= 0.0f && pos - lastPos < -0.5f) lastWrapUs = tsUs;
                    lastPos = pos;
                    ++samples;
                    int gap = 0;
                    const bool valid = host.liveGapToPb(gap);
                    if (session == RACE2 && lapIndex == 0) { ++gridLapSamples; if (valid) ++gridLapValid; }
                    if (session == RACE2 && lapIndex >= 2) { ++lap3onSamples; if (valid) ++lap3onValid; }
                    if (!valid) break;
                    ++validSamples;
                    if (pos < 0.03f && std::abs(gap) > worstNearLine) {
                        worstNearLine = std::abs(gap);
                        worstNearLineAt = Where{ session, lapIndex, lastValidGap, gap, lastValidPos, pos, tsUs };
                    }
                    if (haveLastValid) {
                        const int jump = std::abs(gap - lastValidGap);
                        if (splitSinceLast) {
                            worstJumpAtSplit = std::max(worstJumpAtSplit, jump);
                        } else if (jump > worstJump) {
                            worstJump = jump;
                            worstJumpAt = Where{ session, lapIndex, lastValidGap, gap, lastValidPos, pos, tsUs };
                        }
                    }
                    haveLastValid = true;
                    lastValidGap = gap;
                    lastValidPos = pos;
                    splitSinceLast = false;
                    break;
                }
                default: break;
            }
        });
    REQUIRE(applied > 30000);           // the slimmed tape: ~30k events, most of them positions
    REQUIRE(samples > 20000);           // the player is in nearly every position batch
    // The order at the line is the master's, not the slimming's (see the header).
    CHECK_MESSAGE(wrapFirstEnds == 5, wrapFirstEnds << " wrap-first lap ends; the master has 5");
    CHECK_MESSAGE(completionFirstEnds == 6, completionFirstEnds << " completion-first lap ends; the master has 6");

    // --- the race's opening lap: measured from the gate, never a gap ---------------
    CHECK(gridLapSamples > 500);
    CHECK_MESSAGE(gridLapValid == 0, gridLapValid << " of " << gridLapSamples
                  << " grid-lap samples showed a gap; lap 1 of a standing start must not");

    // --- every line: the gap matches the official lap delta ----------------------
    // Race 2: 57021 (grid, no ref), 42409 (first ref), 42545 (+136), 41604 (-805).
    // Practice: 50511 (first ref), 51569 (+1058), 41190 (-9321), 0 (pit-out, skipped),
    //           41366 (+176), 40595 (-595), 52465 (+11870).
    int checkedLines = 0;
    for (const LapEnd& e : laps) {
        INFO("session " << e.session << " lap " << e.lapIndex << " time " << e.lapTime
             << " expected " << e.expectedGap << " got " << (e.lineGapValid ? e.lineGap : 999999)
             << " at " << e.linePos);
        if (e.lapTime <= 0) continue;                  // the aborted pit-out lap
        if (!e.hadReference) {
            // The first eligible lap of each session, and the grid lap: no gap at its line.
            CHECK_FALSE(e.lineGapValid);
            continue;
        }
        REQUIRE(e.lineGapValid);
        CHECK(e.linePos > 0.9f);                       // the sample really was at the line
        CHECK(std::abs(e.lineGap - e.expectedGap) <= 500);
        ++checkedLines;
    }
    CHECK(checkedLines == 7);      // race laps 3-4, practice laps 2-3 and 5-7
    REQUIRE(laps.size() == 11);

    // --- coverage: with a reference the gap is valid nearly everywhere -----------
    CHECK(lap3onSamples > 2000);
    CHECK_MESSAGE(lap3onValid * 100 >= lap3onSamples * 90,
                  "only " << lap3onValid << " of " << lap3onSamples << " samples valid in race laps 3-4");

    // --- no spike at the line, no jump within a lap ------------------------------
    auto where = [](const Where& w) {
        return std::string(" (session ") + std::to_string(w.session) + " lap " + std::to_string(w.lap)
             + " t=" + std::to_string(w.tsUs / 1000) + "ms pos " + std::to_string(w.prevPos) + "->"
             + std::to_string(w.pos) + " gap " + std::to_string(w.prevGap) + "->" + std::to_string(w.gap) + ")";
    };
    CHECK_MESSAGE(worstNearLine <= 1000, "gap of " << worstNearLine << " ms within 3% of the line" << where(worstNearLineAt));
    CHECK_MESSAGE(worstJump <= 1500, "gap jumped " << worstJump << " ms between consecutive samples" << where(worstJumpAt));
    CHECK_MESSAGE(worstJumpAtSplit <= 1500, "gap jumped " << worstJumpAtSplit << " ms across a split");
    CHECK(validSamples > 10000);
}
