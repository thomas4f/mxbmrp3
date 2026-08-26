// ============================================================================
// tests/integration/make_spotter_demo_tape.cpp
// Generate the committed SPOTTER DEMO fixture — a ~3 minute synthetic race
// WEEKEND (short qualifying, then a 1:00 + 2 laps race) in which every
// MX Bikes-triggerable spotter cue fires from REALISTIC race dynamics: the
// events are DERIVED from simulated motion, not scripted beats. Laps come
// from actual start/finish crossings with their measured times; fastest-lap
// flags from beating the session best; classification order and gaps from
// actual distances; the blue flag from a genuinely faster rider (#99)
// charging through and lapping the player; the wrong-way hazard from a
// crashed rider rejoining backwards.
//
// The cast (an 800 m test loop, ~27 s laps at race pace):
//   #12 Player   steady pace, one push lap, a pit stop, finishes P2
//   #56 Rival    the early dance partner: closes, goes alongside both
//                sides, drops back; quick first lap; a 10 s penalty
//   #99 Flyer    slow start, then 42 m/s charge: fastest laps, takes the
//                lead, laps the player (blue flag), wins (leader's flag)
//   #90 Backmark crashes ahead (hazard), rejoins BACKWARD (wrong-way),
//                then gets lapped by the player (backmarker + pass cues)
//   #7  Retiree  parks it and retires;  #3 Disq disqualified
//
// Approximate cue timeline (absolute seconds; transcripts stamp elapsed):
//   2 quali green | 18 quali flyer lap: fastest lap + new PB | 22 time's up
//   26 session complete | 30 race pre-start | 34 GREEN
//   +8 rider behind, +13 right, +16 left, +19 behind again, +22 clear
//   ~25 rival fastest lap | 30 halfway | ~36 crash ahead | 40/46 penalties
//   ~51 player fastest lap + PB | ~57 wrong way ahead | ~60 new leader #99
//   ~61 OVERTIME two laps after this one | ~64 backmarker ahead, pass cues
//   68 retirement | 70-76 pit in/out | 84 DSQ | ~93 final lap
//   ~112 leader's flag | ~126 BLUE FLAG | ~134 checkered, you | rival
//   finishes | session complete
//   (position reports "P two."/"P three." on every player crossing)
//
// Not triggerable in MX Bikes: penalty_clear / penalty_change (the adapter
// never maps those communication types — GPB/WRS/KRP only). session_state
// only fires on unusual transitions (e.g. cancellation) and stays out of a
// happy-path weekend. leader_you is integration-tested instead — a rider
// being lapped does not also take the lead.
//
// REAL TIME (~3 min of sleeps): the recorder stamps wall-clock time and the
// wrong-way/stationary hazards confirm on the wall clock — replay at 1x
// (the replay tool, or PluginHost::replayTapePaced) or those cues stand
// down. Regenerate + commit via make_spotter_demo_tape.sh.
// ============================================================================
#include "plugin_host.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>
#include <vector>

namespace {

constexpr double kTrackLen = 800.0;
constexpr int kQualiMs = 20000;
constexpr int kRaceMs = 60000;
constexpr double kDt = 0.1;

struct SimRider {
    int num;
    double dist;    // total meters travelled (monotonic)
    double lane;    // lateral offset, meters (+ = focused rider's right)
    double speed;   // m/s this frame
    int wraps = 0;
    double lastWrapT = 0.0;
    int crashed = 0;
    int pit = 0;
    int state = 0;           // classification entry state (3 retired, 4 DSQ)
    bool inactive = false;   // retired/DSQ: parked, excluded
};

void sleepMs(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

float tp(double dist) {
    double p = std::fmod(dist / kTrackLen, 1.0);
    if (p < 0.0) p += 1.0;
    return static_cast<float>(p);
}

}  // namespace

int main(int argc, char** argv) {
    const char* dll = argc > 1 ? argv[1] : "mxbmrp3_test.dlo";
    const char* out = argc > 2 ? argv[2] : "Z:\\tmp\\spotter_demo.tape";

    PluginHost host(dll);
    if (!host.loaded()) { fprintf(stderr, "failed to load %s\n", dll); return 1; }
    host.startup("Z:\\tmp\\mxbdemo\\");
    if (!host.startRecording(out)) { fprintf(stderr, "startRecording(%s) failed\n", out); return 2; }

    enum { PLAYER = 12, RIVAL = 56, FLYER = 99, BACK = 90, RET = 7, DSQR = 3 };

    host.eventInit("Spotter Demo", "Player", (float)kTrackLen, 2);
    host.raceEvent("Spotter Demo", 2);
    host.addEntry(PLAYER, "Player");
    host.addEntry(RIVAL, "Rival");
    host.addEntry(FLYER, "Flyer");
    host.addEntry(BACK, "Backmarker");
    host.addEntry(RET, "Retiree");
    host.addEntry(DSQR, "Disqualified");
    host.runInit(4);

    // Grid/staggered starting distances (leader-most first on the road).
    std::vector<SimRider> riders = {
        { PLAYER, 10.0, 0.0, 0.0 }, { RIVAL, 0.0, -1.5, 0.0 },
        { FLYER, -5.0, 1.5, 0.0 },  { BACK, -15.0, 2.0, 0.0 },
        { RET, -25.0, -2.0, 0.0 },  { DSQR, -30.0, 0.5, 0.0 },
    };
    auto rider = [&](int num) -> SimRider& {
        for (auto& r : riders) if (r.num == num) return r;
        return riders[0];
    };

    int session = 4;                 // quali first
    int sessionBestMs = 0;           // 0 = none yet
    auto classifyNow = [&](int sessionTimeMs, int sessionState) {
        std::vector<SimRider*> order;
        for (auto& r : riders) order.push_back(&r);
        std::sort(order.begin(), order.end(),
                  [](const SimRider* a, const SimRider* b) {
                      return a->dist > b->dist;
                  });
        std::vector<ClassRow> rows;
        for (SimRider* r : order) {
            ClassRow c{};
            c.num = r->num;
            c.state = r->state;   // keep retired/DSQ excluded — a 0 here
                                  // un-parks them into phantom hazards
            c.laps = r->wraps;
            c.pit = r->pit;
            const double lead = order.front()->dist;
            c.gap = static_cast<int>((lead - r->dist) / 30.0 * 1000.0);
            rows.push_back(c);
        }
        host.classify(session, sessionTimeMs, rows, sessionState);
    };

    // Advance the sim one frame; emit RaceLap for any completed lap with the
    // measured time, flagged best=2 when it beats the session's best.
    auto step = [&](double t) {
        std::vector<TrackRow> rows;
        for (auto& r : riders) {
            r.dist += r.speed * kDt;
            const int wrapsNow = static_cast<int>(r.dist / kTrackLen);
            if (wrapsNow > r.wraps && !r.inactive) {
                r.wraps = wrapsNow;
                const int lapMs =
                    static_cast<int>((t - r.lastWrapT) * 1000.0);
                r.lastWrapT = t;
                int best = 0;
                if (sessionBestMs == 0 || lapMs < sessionBestMs) {
                    sessionBestMs = lapMs;
                    best = 2;
                }
                host.raceLap(session, r.num, r.wraps, lapMs, best);
            }
            TrackRow row;
            row.num = r.num;
            row.trackPos = tp(r.dist);
            row.crashed = r.crashed;
            row.posX = static_cast<float>(r.lane);
            row.posZ = static_cast<float>(r.dist);
            row.yaw = 0.0f;
            rows.push_back(row);
        }
        host.raceTrackPosition(rows);
        host.draw();
    };

    // ---- Phase 1: qualifying (t = 0..28) ------------------------------------
    host.session(4, 0, kQualiMs);   // arrives in progress: "Qualifying is live."
    for (auto& r : riders) r.speed = 28.0;
    bool qualiLap = false;
    for (int f = 0; f <= 280; ++f) {
        const double t = f * kDt;
        step(t);
        if (!qualiLap && t >= 18.0) {
            qualiLap = true;
            // The flyer's banker: fastest lap of quali AND the player's PB
            // reference for the race (so the race's first laps are NOT PBs;
            // only the genuine push lap beats it).
            host.raceLap(4, PLAYER, 1, 25000, /*best=*/2);
        }
        if (f % 20 == 5) {
            const double run = t > 2.0 ? t - 2.0 : 0.0;
            classifyNow(kQualiMs - static_cast<int>(run * 1000.0), 16);
        }
        sleepMs(100);
    }
    host.raceSessionState(4, 32, kQualiMs);   // "Session complete."

    // ---- Phase 2: race (absolute t = 30..~186) ------------------------------
    session = 6;
    sessionBestMs = 0;
    for (auto& r : riders) {
        // Re-grid: same stagger, zeroed odometers.
        r.speed = 0.0;
        r.wraps = 0;
        r.crashed = 0;
        r.pit = 0;
        r.inactive = false;
    }
    rider(PLAYER).dist = 10.0; rider(RIVAL).dist = 0.0;
    rider(FLYER).dist = -5.0;  rider(BACK).dist = -15.0;
    rider(RET).dist = -25.0;   rider(DSQR).dist = -30.0;
    host.session(6, /*numLaps=*/2, kRaceMs, /*state=*/256);  // pre-start
    classifyNow(kRaceMs, 256);

    bool green = false, pen56 = false, pen12 = false, retire = false;
    bool dsq = false, raceOver = false;
    const double raceGreenT = 34.0;
    const int kEndF = 1860;
    for (int f = 300; f <= kEndF; ++f) {
        const double t = f * kDt;
        const double tr = t - raceGreenT;   // race time since green

        if (!green && t >= raceGreenT) {
            green = true;
            host.raceSessionState(6, 16, kRaceMs);   // "Green green green."
            for (auto& r : riders) r.lastWrapT = t;
        }

        if (green && !raceOver) {
            // Speed scripts — every event downstream falls out of these.
            SimRider& p = rider(PLAYER);
            const bool pLap2 = p.wraps == 1;              // the push lap
            const bool pit = tr >= 70.0 && tr < 74.0;     // pit visit
            p.speed = pit ? 14.0 : (pLap2 ? 33.0 : 30.0);
            p.pit = pit ? 1 : 0;
            SimRider& v = rider(RIVAL);
            if      (tr < 8.0)  v.speed = 30.0;
            else if (tr < 14.0) v.speed = 31.5;                    // closes
            else if (tr < 20.0) { v.speed = p.speed; }             // alongside
            else if (tr < 26.0) v.speed = 27.0;                    // drops
            else if (tr < 60.0) v.speed = 29.7;
            else                v.speed = 28.5;   // fades late: no band churn
            if (tr >= 14.0 && tr < 17.0) v.lane = 1.8;             // your right
            else if (tr >= 17.0 && tr < 20.0) v.lane = -1.8;       // crosses
            else v.lane = -1.5;
            // Hold the rival glued alongside during the dance window.
            if (tr >= 14.0 && tr < 20.0) {
                v.dist = p.dist - 2.0;
            }

            SimRider& fl = rider(FLYER);
            fl.speed = tr < 55.0 ? 29.0 : 42.0;                    // the charge

            SimRider& b = rider(BACK);
            if      (tr < 24.0) b.speed = 30.0;
            else if (tr < 36.0) b.speed = 35.0;                    // charges ahead
            else if (tr < 44.0) { b.speed = 0.0; b.crashed = 1; }  // down!
            else if (tr < 56.0) { b.speed = 0.0; b.crashed = 0; }  // gathering it
            else if (tr < 61.0) b.speed = -2.0;                    // WRONG WAY
            else                b.speed = 24.0;                    // limps on

            rider(RET).speed = rider(RET).inactive ? 0.0 : 27.0;
            rider(DSQR).speed = rider(DSQR).inactive ? 0.0 : 27.5;

            auto due = [&](double when, bool& flag) {
                if (flag || tr < when) return false;
                flag = true;
                return true;
            };
            if (due(40.0, pen56)) host.communication(RIVAL, 0, 2, 10);
            if (due(46.0, pen12)) host.communication(PLAYER, 0, 2, 5);
            if (due(68.0, retire)) {
                rider(RET).inactive = true;
                rider(RET).state = 3;
                host.communication(RET, 3);
            }
            if (due(84.0, dsq)) {
                rider(DSQR).inactive = true;
                rider(DSQR).state = 4;
                host.communication(DSQR, 4);
            }
        }

        step(t);
        // 0.5s cadence: pace-report gaps quantize to the classification
        // clock, and 2s steps made every demo gap read 0.0 or 2.0.
        if (f % 5 == 2) {
            const int stMs =
                green ? kRaceMs - static_cast<int>(tr * 1000.0) : kRaceMs;
            classifyNow(stMs, green ? 16 : 256);
        }
        if (!raceOver && green && tr >= 150.0) {
            raceOver = true;
            host.raceSessionState(6, 32, kRaceMs);   // "Session complete."
        }
        sleepMs(100);
    }

    host.stopRecording();
    fprintf(stderr, "recorded spotter demo weekend -> %s\n", out);
    return 0;
}
