// ============================================================================
// tests/integration/tests/director_broadcast_test.cpp
// Broadcast measurement for the auto-director. Replays a REAL captured callback
// tape with the director enabled and a simulated clock driven from each event's
// recorded timestamp, so the director's wall-clock pacing (min/max shot, holds,
// variety cadence) plays out at the real recorded cadence instead of collapsing
// into the few real milliseconds a naive replay takes. Every cut the director
// makes is logged by cutTo(); this test reads its own log back and reconstructs
// the broadcast — cut count, cut rate, shot-length spread, shot-type and camera
// mix, and per-rider screen time — printing a report and asserting it lands in a
// plausible sports-broadcast range.
//
// This is the measured answer to "what would the broadcast look like?": the
// numbers below are computed, not estimated. It also guards the director's
// pacing against regressions (a change that freezes the camera, stops rotating
// the field, or breaks the sim clock fails here). See TESTING.md.
//
// Same report, on a REAL session: the director logs each cut into the normal
// plugin log (release too), so `tools/director_report.py mxbmrp3_log.txt` prints
// this exact analysis for any in-game replay/spectate session. Keep the cut-log
// FORMAT (director_manager.cpp cutTo()) in step with parseCuts() here AND that
// tool's regex.
//
// CAVEAT on the numbers: the committed golden tapes are slimmed to the
// state-changing callbacks and DROP the 30 Hz RaceTrackPosition stream. The
// director's evaluate() is data-driven (it runs when standings change), so on a
// stable stretch with only classification traffic it may not tick for several
// seconds — which lets an occasional lull shot run past the 25 s max-shot cap
// that a live session (with the continuous position feed keeping evaluate()
// ticking ~10 Hz) would enforce. So the measured cut count is a slight LOWER
// bound and the longest shots are longer than a real broadcast's; the typical
// pacing (the median shot) is representative. The upper bounds asserted here are
// deliberately loose to tolerate that artifact.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <map>
#include <algorithm>

namespace {

struct Cut {
    long long tMs;      // sim time of the cut
    int subject;        // followed race number
    std::string shot;   // solo/battle/overtake/pace/...
    std::string cam;    // camera role name (may contain spaces)
    std::string reason; // pacing trigger: acquire/subject-gone/story/maxshot/finish/...
};

// Slurp a file (a Wine Z:\ path resolved by the DLL's Logger) into a string.
std::string readFile(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return {};
    std::string out;
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    fclose(f);
    return out;
}

// Pull the "Director cut:" records out of a plugin log. The line looks like:
//   [HH:MM:SS.mmm] [INFO] Director cut: t=12345ms #7 shot=battle cam=Trackside partner=9 reason=story
// cam can contain spaces, so it's captured up to " partner="; reason (appended last) is
// captured to end-of-line. FORMAT COUPLING: keep this in step with cutTo() in
// director_manager.cpp and CUT_RE in tools/director_report.py.
std::vector<Cut> parseCuts(const std::string& log) {
    std::vector<Cut> cuts;
    size_t pos = 0;
    const std::string tag = "Director cut: t=";
    while ((pos = log.find(tag, pos)) != std::string::npos) {
        const char* p = log.c_str() + pos + tag.size();
        Cut c{};
        c.tMs = strtoll(p, nullptr, 10);
        const char* hash = strstr(p, "#");
        const char* shot = strstr(p, "shot=");
        const char* cam  = strstr(p, "cam=");
        const char* part = strstr(p, " partner=");
        if (!hash || !shot || !cam || !part) { pos += tag.size(); continue; }
        c.subject = atoi(hash + 1);
        shot += 5;
        const char* shotEnd = strchr(shot, ' ');
        c.shot.assign(shot, shotEnd ? (size_t)(shotEnd - shot) : strlen(shot));
        cam += 4;
        c.cam.assign(cam, (size_t)(part - cam));   // up to " partner="
        const char* rsn = strstr(part, "reason=");
        if (rsn) {
            rsn += 7;
            const char* rEnd = strpbrk(rsn, " \r\n");
            c.reason.assign(rsn, rEnd ? (size_t)(rEnd - rsn) : strlen(rsn));
        }
        cuts.push_back(c);
        pos = (size_t)(part - log.c_str());
    }
    return cuts;
}

// Compute + print the broadcast report and return the cut count. durationMs is the
// tape's end (the last shot runs from its cut to there).
size_t report(const std::vector<Cut>& cuts, long long durationMs, const char* label) {
    printf("\n================ Broadcast analysis: %s ================\n", label);
    if (cuts.empty()) { printf("  (no cuts recorded)\n"); return 0; }

    const double durS = durationMs / 1000.0;
    const size_t n = cuts.size();

    // Per-shot length (cut i runs until cut i+1, last until the tape end) and the
    // buckets that airtime falls into, plus shot-type / camera / per-rider tallies.
    std::vector<double> lens;
    std::map<std::string, double> shotAir, camAir;
    std::map<int, double> riderAir;
    double belowMin = 0, atMin = 0, mid = 0, atMax = 0;   // <8s / 8-15 / 15-25 / >25s share of shots
    for (size_t i = 0; i < n; ++i) {
        long long end = (i + 1 < n) ? cuts[i + 1].tMs : durationMs;
        double len = (end - cuts[i].tMs) / 1000.0;
        if (len < 0) len = 0;
        lens.push_back(len);
        shotAir[cuts[i].shot] += len;
        camAir[cuts[i].cam]   += len;
        riderAir[cuts[i].subject] += len;
        if (len < 8.0)       belowMin++;
        else if (len < 15.0) atMin++;
        else if (len <= 25.5) mid++;
        else                 atMax++;
    }
    std::vector<double> sorted = lens;
    std::sort(sorted.begin(), sorted.end());
    const double medLen = sorted[sorted.size() / 2];

    printf("  Race duration:   %.1f s  (%.1f min)\n", durS, durS / 60.0);
    printf("  Total cuts:      %zu\n", n);
    printf("  Cut rate:        %.1f cuts/min   (avg shot %.1f s, median %.1f s, longest %.1f s)\n",
           n / (durS / 60.0), durS / n, medLen, sorted.back());
    printf("  Shot length mix: <8s %.0f%%  |  8-15s %.0f%%  |  15-25s %.0f%%  |  >25s %.0f%%\n",
           100.0 * belowMin / n, 100.0 * atMin / n, 100.0 * mid / n, 100.0 * atMax / n);

    auto printShare = [&](const std::map<std::string, double>& m, const char* title) {
        std::vector<std::pair<std::string, double>> v(m.begin(), m.end());
        std::sort(v.begin(), v.end(), [](auto& a, auto& b){ return a.second > b.second; });
        printf("  %s\n", title);
        for (auto& kv : v)
            printf("      %-12s %5.1f%%  (%.0f s)\n", kv.first.c_str(), 100.0 * kv.second / durS, kv.second);
    };
    printShare(shotAir, "Shot types (share of airtime):");
    printShare(camAir,  "Cameras (share of airtime):");

    // Per-rider screen time: how evenly the field was covered.
    std::vector<std::pair<int, double>> riders(riderAir.begin(), riderAir.end());
    std::sort(riders.begin(), riders.end(), [](auto& a, auto& b){ return a.second > b.second; });
    printf("  Rider airtime:   %zu distinct riders got screen time\n", riders.size());
    printf("      most:   #%d  %.1f s  (%.1f%%)\n", riders.front().first, riders.front().second,
           100.0 * riders.front().second / durS);
    printf("      least:  #%d  %.1f s  (%.1f%%)\n", riders.back().first, riders.back().second,
           100.0 * riders.back().second / durS);
    std::vector<double> secs;
    for (auto& r : riders) secs.push_back(r.second);
    std::sort(secs.begin(), secs.end());
    printf("      median per shown rider: %.1f s\n", secs[secs.size() / 2]);
    printf("========================================================\n");
    return n;
}

// Story-follow bitmask (matches MXBMRP3_Test_DirectorSetStories):
//   1=battles 2=incidents 4=fastestLap 8=pace 16=lappers 32=drops.
enum Story { S_BATTLES = 1, S_INCIDENTS = 2, S_FASTEST = 4, S_PACE = 8, S_LAPPERS = 16, S_DROPS = 32 };
constexpr int STORIES_DEFAULT = S_BATTLES | S_FASTEST | S_PACE;             // ship defaults
constexpr int STORIES_ALL     = S_BATTLES | S_INCIDENTS | S_FASTEST | S_PACE | S_LAPPERS | S_DROPS;
constexpr int STORIES_NONE    = 0;                                          // pure lull round-robin

// Drive one tape through the director with the simulated clock and return the parsed
// cuts. storyMask selects which stories the director may follow; drawTickMs pumps
// synthetic Draw ticks between events (0 = data-only, as the original CI case). The
// per-config savePath keeps each run's log separate so director_report.py can profile
// them independently.
std::vector<Cut> runTape(PluginHost& host, const std::string& savePath,
                         const char* tapeFile, int minApplied, long long* outDurationMs,
                         int storyMask = STORIES_DEFAULT, long long drawTickMs = 0) {
    // The plugin creates <savePath>\mxbmrp3 but NOT savePath itself, and the CI runner
    // only pre-creates the one director_broadcast dir — so make the per-config save dir
    // here or its log (and every cut) silently goes nowhere.
    { std::string d = savePath;
      while (!d.empty() && (d.back() == '\\' || d.back() == '/')) d.pop_back();
      CreateDirectoryA(d.c_str(), nullptr); }
    host.startup(savePath.c_str());
    host.draw();                     // state 1 = spectate, so the director actually directs
    host.directorSetEnabled(true);
    host.directorSetStories(storyMask);
    const std::string tape = std::string("Z:\\tmp\\mxbmrp3-tests\\fixtures\\") + tapeFile;
    const int applied = host.replayTapeTimed(tape, drawTickMs);
    CHECK(applied >= minApplied);
    *outDurationMs = host.lastReplayTimeMs();
    // Read the log back while the session is still live (Logger flushes every line).
    const std::string cuts = readFile(savePath + "mxbmrp3\\mxbmrp3_log.txt");
    host.shutdown();
    return parseCuts(cuts);
}

// Count non-final shots longer than thresholdS. The final shot is excluded because it
// is left open (bounded only by the tape end) and the finish-lock legitimately parks on
// the winner; this counts the MID-broadcast shots that overran the max-shot cap.
int countOverMaxNonFinal(const std::vector<Cut>& cuts, double thresholdS) {
    int over = 0;
    for (size_t i = 0; i + 1 < cuts.size(); ++i)   // i+1 < size => skip the final (open) shot
        if ((cuts[i + 1].tMs - cuts[i].tMs) / 1000.0 > thresholdS) ++over;
    return over;
}

// A cut reason that is ALLOWED to end a shot under the min-shot floor by design.
bool isMinShotBypass(const std::string& reason) {
    return reason == "acquire" || reason == "subject-gone" ||
           reason == "incident" || reason == "finish";
}

// Count min-shot VIOLATIONS: shots shorter than minShotS whose ENDING cut (cuts[i+1])
// was NOT a by-design bypass. A clean director yields 0. The final (open) shot is
// excluded — it has no ending cut. This is the direct "is min-shot respected?" check.
int minShotViolations(const std::vector<Cut>& cuts, double minShotS) {
    int v = 0;
    for (size_t i = 0; i + 1 < cuts.size(); ++i) {
        double dur = (cuts[i + 1].tMs - cuts[i].tMs) / 1000.0;
        if (dur < minShotS && !isMinShotBypass(cuts[i + 1].reason)) ++v;
    }
    return v;
}

// Airtime (s) the single most-shown rider accumulated, and the distinct rider count.
void airtimeSpread(const std::vector<Cut>& cuts, long long durationMs,
                   double* topShare, int* distinct) {
    std::map<int, double> air;
    double total = 0.0;
    for (size_t i = 0; i < cuts.size(); ++i) {
        long long end = (i + 1 < cuts.size()) ? cuts[i + 1].tMs : durationMs;
        double len = (end - cuts[i].tMs) / 1000.0;
        air[cuts[i].subject] += len;
        total += len;
    }
    double mx = 0.0;
    for (auto& kv : air) mx = std::max(mx, kv.second);
    *topShare = total > 0 ? mx / total : 0.0;
    *distinct = static_cast<int>(air.size());
}

}  // namespace

TEST_CASE("director broadcast: a 24-rider race replays as a plausible broadcast") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());

    long long durMs = 0;
    auto cuts = runTape(host, "Z:\\tmp\\mxbmrp3-tests\\director_broadcast\\",
                        "race_farm14_24riders.tape", /*minApplied=*/29000, &durMs);
    const size_t n = report(cuts, durMs, "farm14 - 24 riders (multiplayer)");

    // A ~16 min race must cut many times (never freeze) but not frantically: the
    // director's 8 s floor / 25 s ceiling put the count in a broad, sane band.
    CHECK(n >= 20);
    CHECK(n <= 200);
    // The average shot must sit inside the min/max-shot envelope (broadcast pacing).
    const double avgShot = (durMs / 1000.0) / (double)n;
    CHECK(avgShot >= 7.0);
    CHECK(avgShot <= 26.0);
    // Rotation must spread the camera across the field, not glue it to the leader:
    // most of a 24-rider grid should get some airtime over 16 minutes.
    std::map<int, int> seen;
    for (auto& c : cuts) seen[c.subject]++;
    CHECK(seen.size() >= 12);
}

TEST_CASE("director broadcast: a lone-rider session degrades gracefully (no thrash)") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());

    // The club golden tape is a solo ride — one active rider (#4) finishing P1, the
    // rest of the field absent. With nobody to cut to, the round-robin correctly has
    // no target, so the director must NOT thrash: it holds the lone rider (and its
    // finish/final-lap story shots) rather than spinning the camera pointlessly.
    long long durMs = 0;
    auto cuts = runTape(host, "Z:\\tmp\\mxbmrp3-tests\\director_broadcast\\",
                        "race2_mxbclub_1lap.tape", /*minApplied=*/8000, &durMs);
    const size_t n = report(cuts, durMs, "club - solo finisher (1 active rider)");

    // A handful of shots at most (opening solo + finish/final-lap), never a stream of
    // cuts — there is no second rider to justify rotation.
    CHECK(n >= 1);
    CHECK(n <= 10);
    std::map<int, int> seen;
    for (auto& c : cuts) seen[c.subject]++;
    CHECK(seen.size() == 1);   // only the lone rider is ever framed
}

// Count cuts by shot type.
static std::map<std::string, int> shotTypeCounts(const std::vector<Cut>& cuts) {
    std::map<std::string, int> m;
    for (const auto& c : cuts) m[c.shot]++;
    return m;
}

// Each config gets its OWN test case (fresh PluginHost => the DLL reloads => the
// Director/Logger singletons reset), so replaying the same tape under a different
// story config can't inherit the previous run's shot timer, session token, or log
// path. Each writes its own log (Z:\tmp\mxbmrp3-tests\bcast_<cfg>\...) so
// tools/director_report.py can profile the three side by side. The Draw-tick pump
// (10 Hz) makes the director's per-frame pacing (pollPacing) fire during replay, as
// it would live. See TESTING.md.
static const char* kSweepTape = "race_farm14_24riders.tape";

TEST_CASE("director broadcast: stories OFF is a leader-anchored lull (no battle/fastest cuts)") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    long long dur = 0;
    auto cuts = runTape(host, "Z:\\tmp\\mxbmrp3-tests\\bcast_none\\", kSweepTape,
                        /*minApplied=*/29000, &dur, STORIES_NONE, /*drawTickMs=*/100);
    report(cuts, dur, "24 riders - stories OFF (pure lull)");
    REQUIRE(cuts.size() >= 10);
    // Battles and the fastest-lap cut ARE toggle-gated, so they must be absent.
    // Overtakes / finish are NOT gated (position swaps + the finish lock always fire).
    auto types = shotTypeCounts(cuts);
    CHECK(types.count("battle") == 0);
    CHECK(types.count("fastest") == 0);
    // The round-robin still spreads airtime across the field even with no stories.
    double top = 0; int distinct = 0;
    airtimeSpread(cuts, dur, &top, &distinct);
    CHECK(distinct >= 12);
    // Min-shot is respected: every sub-min shot ends on a by-design bypass, not a
    // pacing cut that should have honored the floor.
    CHECK(minShotViolations(cuts, 8.0) == 0);
}

TEST_CASE("director broadcast: stories ALL on adds battle/variety shots") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    long long dur = 0;
    auto cuts = runTape(host, "Z:\\tmp\\mxbmrp3-tests\\bcast_all\\", kSweepTape,
                        /*minApplied=*/29000, &dur, STORIES_ALL, /*drawTickMs=*/100);
    report(cuts, dur, "24 riders - stories ALL on");
    REQUIRE(cuts.size() >= 10);
    auto types = shotTypeCounts(cuts);
    CHECK(types.count("battle") >= 1);   // battles appear once enabled
    CHECK(types.size() >= 3);            // richer shot mix than the pure lull
    CHECK(minShotViolations(cuts, 8.0) == 0);  // stories still honor the min-shot floor
}

TEST_CASE("director broadcast: the pacing pump bounds max-shot during data lulls") {
    // The regression guard for the per-frame pacing pump (DirectorManager::pollPacing).
    // Same tape + config, replayed WITHOUT and WITH the Draw-tick pump. Without it the
    // director only ticks on data callbacks, so a stretch where the classification feed
    // thins lets the current shot overrun the 25s max-shot cap; the pump enforces the cap
    // on wall-clock time. A shot over ~35s is well past the cap+coalesce slack, so it's a
    // genuine overrun (not the +1 finish-lock hold, which both runs share).
    const double kOverrunS = 35.0;
    int overNoPump = 0, overPump = 0;
    {   // scoped so the DLL unloads before the pumped run -> fresh director/logger state
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        long long dur = 0;
        auto cuts = runTape(host, "Z:\\tmp\\mxbmrp3-tests\\bcast_nopump\\", kSweepTape,
                            /*minApplied=*/29000, &dur, STORIES_DEFAULT, /*drawTickMs=*/0);
        overNoPump = countOverMaxNonFinal(cuts, kOverrunS);
        MESSAGE("no-pump overlong shots (>", kOverrunS, "s): ", overNoPump);
    }
    {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        long long dur = 0;
        auto cuts = runTape(host, "Z:\\tmp\\mxbmrp3-tests\\bcast_pump\\", kSweepTape,
                            /*minApplied=*/29000, &dur, STORIES_DEFAULT, /*drawTickMs=*/100);
        overPump = countOverMaxNonFinal(cuts, kOverrunS);
        MESSAGE("pumped overlong shots (>", kOverrunS, "s): ", overPump);
    }
    CHECK(overNoPump >= 2);          // the bug: shots overrun the cap in data lulls (3 here)
    CHECK(overPump <= 1);            // the fix: at most the one finish-lock hold survives
    CHECK(overPump < overNoPump);   // the pump strictly reduces overruns
}

TEST_CASE("director broadcast: ship-default stories replay (report sweep)") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    long long dur = 0;
    auto cuts = runTape(host, "Z:\\tmp\\mxbmrp3-tests\\bcast_default\\", kSweepTape,
                        /*minApplied=*/29000, &dur, STORIES_DEFAULT, /*drawTickMs=*/100);
    report(cuts, dur, "24 riders - ship default stories");
    CHECK(cuts.size() >= 10);
}

TEST_CASE("director: an incident cuts in immediately, bypassing the min-shot floor") {
    // The multiplayer tape has no crashes, so the min-shot sweep above can't exercise the
    // ONE cut that is *meant* to ignore the floor: a fresh incident. Hand-build a spectated
    // race, settle the director on a shot, then put a rider down < min-shot later and assert
    // the director cuts to the crash at once with reason=incident — proving the intended
    // bypass (breaking news) is exactly that, and is tagged so a log can tell it apart from
    // a real violation. Uses the injectable director clock so timing is deterministic.
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\director_incident\\";
    { std::string d = save; while (!d.empty() && d.back() == '\\') d.pop_back();
      CreateDirectoryA(d.c_str(), nullptr); }
    host.startup(save.c_str());
    host.eventInit("TestTrack", "Cam");
    host.raceEvent("TestTrack");
    host.session(/*session=*/6, /*numLaps=*/10, /*lengthMs=*/0);
    for (int num : { 10, 22, 7, 3 }) {
        char nm[16]; snprintf(nm, sizeof(nm), "R%d", num);
        host.addEntry(num, nm);
    }
    host.draw();                       // state 1 = spectate, so the director directs
    host.directorSetEnabled(true);
    // Incidents ONLY (no battles): so the director sits on a plain leader/solo shot. While
    // framing a battle/overtake the director deliberately suppresses a LOWER-placed crash
    // (a P15 tip-over must not yank the camera off a P1 fight - the position-weighted
    // preemption), so a solo shot is the clean way to show the incident min-shot bypass.
    host.directorSetStories(S_INCIDENTS);

    auto classify = [&]() {
        host.classify(6, 200000, {
            { .num = 10, .best = 90000, .laps = 3, .gap = 0 },
            { .num = 22, .best = 90500, .laps = 3, .gap = 1200 },
            { .num = 7,  .best = 91000, .laps = 3, .gap = 2600 },
            { .num = 3,  .best = 91500, .laps = 3, .gap = 5000 },
        });
    };
    auto positions = [&](bool crash7) {
        host.raceTrackPosition({
            { .num = 10, .trackPos = 0.50f, .crashed = 0 },
            { .num = 22, .trackPos = 0.49f, .crashed = 0 },
            { .num = 7,  .trackPos = 0.40f, .crashed = crash7 ? 1 : 0 },
            { .num = 3,  .trackPos = 0.30f, .crashed = 0 },
        });
    };

    // Settle the director over a few evals (clock advancing past the 300ms coalesce each
    // time). No crashes -> seeds the per-rider crash baseline so the later one is an edge.
    long long t = 1000;
    host.directorSetNowMs(t); classify(); positions(false);   // acquire + seed
    for (int i = 0; i < 3; ++i) { t += 500; host.directorSetNowMs(t); positions(false); classify(); }

    auto before = parseCuts(readFile(save + "mxbmrp3\\mxbmrp3_log.txt"));
    REQUIRE(before.size() >= 1);
    const long long lastCutT = before.back().tMs;

    // #7 goes down 500 ms later — far under the 8 s min-shot floor.
    t += 500; host.directorSetNowMs(t); positions(true); classify();

    auto after = parseCuts(readFile(save + "mxbmrp3\\mxbmrp3_log.txt"));
    REQUIRE(after.size() > before.size());
    const Cut& inc = after.back();
    CHECK(inc.shot == "incident");
    CHECK(inc.reason == "incident");
    CHECK(inc.subject == 7);
    // The shot it interrupted ran well under the min-shot floor -> the incident bypassed it.
    CHECK((inc.tMs - lastCutT) < 8000);
    host.shutdown();
}
