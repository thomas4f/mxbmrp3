// ============================================================================
// tools/demo_reel/tapegen.cpp
// Demo tape generator: writes a fabricated race as a Recorder tape (MXBHREC, the
// format the in-plugin EventRecorder writes in-game), so the hover reel plays a
// tape and nothing else. A real tape from the game drops in for this one.
// The race: 8 riders, 4 laps, then a PB lap that ends tailSecs before the tape
// does. Every rider's laps, splits and the classification follow from where they
// are, and the order changes: 10 takes the lead from 22, the player gets by 7, 18
// gets by 3. In the last liveSecs (what the reel plays live): the player catches and
// passes backmarker 12, a lap down (LAPPER AHEAD), then 22 crashes just ahead
// (HAZARD AHEAD), is passed while down, and rejoins behind. Telemetry, positions
// and a Draw at 100 Hz.
//   wine tapegen.exe mxbmrp3_test.dlo out.tape [tailSecs] [liveSecs]
// The harness's own builders fill the payloads (PluginHost's export pointers are
// swapped for writers), so the bytes are what those callbacks carry.
// ============================================================================
// The std and vendored headers first, so only PluginHost sees the access hack.
#include <any>
#include <sstream>
#include <map>
#include <set>
#include <unordered_map>
#include <functional>
#include <memory>
#include <thread>
#include <mutex>
#include <atomic>
#include <optional>
#include <variant>
#include <array>
#include "nlohmann/json.hpp"
#define private public
#include "plugin_host.h"
#undef private
#include "tape.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

static const double PI = 3.14159265358979323846;

// An MX-ish layout: one half (total turn 180 deg) repeated twice closes by symmetry.
struct Piece { double len; double radius; };   // radius 0 = straight, <0 = left
static std::vector<Piece> halfLap() {
    auto arc = [](double r, double deg) { return Piece{ std::fabs(r) * deg * PI / 180.0, r }; };
    return {
        { 140, 0 }, arc(28, 90), { 45, 0 }, arc(-18, 160), { 35, 0 }, arc(22, 120),
        { 60, 0 }, arc(-40, 40), arc(40, 40), { 30, 0 }, arc(26, 70), { 55, 0 }, arc(-30, 60),
        arc(30, 60),
    };
}
static std::vector<Piece> lapPieces() {
    std::vector<Piece> h = halfLap(), out = h;
    // Heading sum check: rights - lefts must be 180 per half.
    out.insert(out.end(), h.begin(), h.end());
    return out;
}
static std::vector<TrackSegmentRow> toSegments(const std::vector<Piece>& ps) {
    std::vector<TrackSegmentRow> segs;
    double heading = 0.0;   // deg, 0 = north
    for (const Piece& p : ps) {
        // Split long pieces so the map's sampling stays smooth.
        int n = std::max(1, static_cast<int>(p.len / 12.0));
        for (int i = 0; i < n; ++i) {
            TrackSegmentRow s;
            s.type = p.radius == 0 ? 0 : 1;
            s.radius = static_cast<float>(p.radius);
            s.length = static_cast<float>(p.len / n);
            s.angle = static_cast<float>(heading);
            if (p.radius != 0) heading += (p.len / n) / p.radius * 180.0 / PI;
            segs.push_back(s);
        }
    }
    segs[0].startX = 0; segs[0].startY = 0;
    return segs;
}

// Speed profile along the lap, sampled at N points: corners slow, straights fast.
struct Profile {
    static const int N = 400;
    double lengthM = 0;
    double speed[N];      // m/s
    double cum[N + 1];    // seconds from the line at pace 1.0
};
static Profile buildProfile(const std::vector<Piece>& ps) {
    Profile pr;
    for (const Piece& p : ps) pr.lengthM += p.len;
    double raw[Profile::N];
    for (int i = 0; i < Profile::N; ++i) {
        double s = (i + 0.5) / Profile::N * pr.lengthM, acc = 0;
        double r = 0;
        for (const Piece& p : ps) { if (s < acc + p.len) { r = p.radius; break; } acc += p.len; }
        raw[i] = r == 0 ? 24.0 : std::min(24.0, std::sqrt(7.0 * std::fabs(r)));
    }
    // Smooth (circular) so braking and acceleration ramp.
    for (int i = 0; i < Profile::N; ++i) {
        double sum = 0; int k = 0;
        for (int d = -10; d <= 10; ++d) { sum += raw[(i + d + Profile::N) % Profile::N]; ++k; }
        pr.speed[i] = std::min(raw[i] + 3.0, sum / k);
    }
    pr.cum[0] = 0;
    for (int i = 0; i < Profile::N; ++i) pr.cum[i + 1] = pr.cum[i] + pr.lengthM / Profile::N / pr.speed[i];
    return pr;
}

// Lap time (s) to reach fraction p at a pace that varies along the lap.
struct Lap {
    double t[Profile::N + 1];
    double total() const { return t[Profile::N]; }
    double posAt(double secs) const {   // inverse: fraction of lap at elapsed secs
        if (secs <= 0) return 0;
        if (secs >= total()) return 1;
        int i = static_cast<int>(std::upper_bound(t, t + Profile::N + 1, secs) - t) - 1;
        double f = (secs - t[i]) / (t[i + 1] - t[i]);
        return (i + f) / Profile::N;
    }
};
static Lap makeLap(const Profile& pr, double base, double wave, double phase) {
    Lap l; l.t[0] = 0;
    for (int i = 0; i < Profile::N; ++i) {
        double p = (i + 0.5) / Profile::N;
        double k = base + wave * std::sin(2 * PI * p * 2 + phase);
        l.t[i + 1] = l.t[i] + (pr.cum[i + 1] - pr.cum[i]) * k;
    }
    return l;
}

static float ease(float u) { u = std::clamp(u, 0.0f, 1.0f); return u * u * (3 - 2 * u); }


namespace rec {
tape::TapeWriter w;
uint64_t nowUs = 0;
std::vector<uint8_t> buf;
void put(tape::EventType t, const void* a, size_t na, const void* b = nullptr, size_t nb = 0) {
    buf.resize(na + nb);
    if (na) std::memcpy(buf.data(), a, na);
    if (nb) std::memcpy(buf.data() + na, b, nb);
    tape::EventHeader eh{ static_cast<uint32_t>(t), static_cast<uint32_t>(buf.size()), nowUs };
    fwrite(&eh, sizeof(eh), 1, w.m_f);
    if (!buf.empty()) fwrite(buf.data(), buf.size(), 1, w.m_f);
}
#define REC_DS(NAME, TYPE) void NAME(void* p, int n) { put(tape::EventType::TYPE, p, n); }
REC_DS(eventInit, EventInit) REC_DS(raceEvent, RaceEvent) REC_DS(session, RaceSession)
REC_DS(sessionState, RaceSessionState) REC_DS(addEntry, RaceAddEntry) REC_DS(raceLap, RaceLap)
REC_DS(raceSplit, RaceSplit) REC_DS(runInit, RunInit)
void runStart() { put(tape::EventType::RunStart, nullptr, 0); }
void classify(void* h, int, void* e, int es) {
    // Entry count from the header (m_iNumEntries is the struct's last int).
    const auto* hdr = static_cast<SPluginsRaceClassification_t*>(h);
    tape::ClassificationPrefix pre{ *hdr, hdr->m_iNumEntries };
    put(tape::EventType::RaceClassification, &pre, sizeof(pre), e, static_cast<size_t>(es) * pre.numEntries);
}
void trackPos(int n, void* a, int es) {
    tape::TrackPositionPrefix pre{ n };
    put(tape::EventType::RaceTrackPosition, &pre, sizeof(pre), a, static_cast<size_t>(es) * n);
}
void centerline(int n, void* segs, void*) {
    put(tape::EventType::TrackCenterline, &n, sizeof(n), segs, sizeof(TrackSegmentRow) * n);
}
void telemetry(void* d, int n, float time, float pos) {
    float tail[2] = { time, pos };
    put(tape::EventType::RunTelemetry, d, n, tail, sizeof(tail));
}
void draw(int, int* nq, void**, int* ns, void**) { *nq = 0; *ns = 0; put(tape::EventType::Draw, nullptr, 0); }
}  // namespace rec

int main(int argc, char** argv) {
    const char* dll = argc > 1 ? argv[1] : "mxbmrp3_test.dlo";
    const char* out = argc > 2 ? argv[2] : "demo.tape";
    const double tailSecs = argc > 3 ? std::atof(argv[3]) : 4.5;
    const double liveSecs = argc > 4 ? std::atof(argv[4]) : 116.0;
    PluginHost host(dll);
    if (!host.loaded()) { fprintf(stderr, "failed to load %s\n", dll); return 1; }
    if (!rec::w.open(out)) { fprintf(stderr, "cannot write %s\n", out); return 1; }
    host.m_eventInit = rec::eventInit; host.m_raceEvent = rec::raceEvent; host.m_session = rec::session;
    host.m_sessionState = rec::sessionState; host.m_addEntry = rec::addEntry; host.m_raceLap = rec::raceLap;
    host.m_raceSplit = rec::raceSplit; host.m_runInit = rec::runInit; host.m_runStart = rec::runStart;
    host.m_classify = rec::classify; host.m_trackPos = rec::trackPos; host.m_trackCenter = rec::centerline;
    host.m_telemetry = rec::telemetry; host.m_draw = rec::draw;

    const std::vector<Piece> pieces = lapPieces();
    const Profile prof = buildProfile(pieces);
    const float L = static_cast<float>(prof.lengthM);
    host.eventInit("Southwick", "Thomas", L, 2, "KTM 450 SX-F 2023", "MX1", "showreel", 0, "",
                   7.0f, 10500, 12000, 13000);
    host.raceEvent("Southwick", /*type=*/2);
    host.session(/*session=*/6, /*numLaps=*/10, /*lengthMs=*/0);
    struct Rider { int num; const char* name; const char* bike; };
    const Rider grid[] = {
        {  4, "Thomas",      "KTM 450 SX-F 2023" },
        { 10, "Wilhelmina",  "Kawasaki KX450 2023" },
        { 22, "Bartholomew", "Honda CRF450R 2023" },
        {  7, "Cassiopeia",  "Yamaha YZ450F 2023" },
        {  3, "Aleksander",  "GasGas MC 450F 2024" },
        { 18, "Genevieve",   "Husqvarna FC 450 2023" },
        { 31, "Maximilian",  "Suzuki RM-Z450 2023" },
        { 12, "Rosalind",    "Husqvarna FC 350 2023" },
    };
    constexpr int NR = sizeof(grid) / sizeof(grid[0]);
    for (const Rider& r : grid) host.addEntry(r.num, r.name, r.bike);
    host.runInit(6);
    host.runStart();
    host.trackCenterline(toSegments(pieces), { 8.0f, L / 3.0f, 2.0f * L / 3.0f, 0.0f });

    // The centerline in world metres, sampled every 0.25 m (midpoint heading, the
    // same geometry the Map draws), so riders carry real positions and headings.
    struct WorldPt { float x, z, h; };
    std::vector<WorldPt> world;
    {
        double x = 0, z = 0, h = 0;
        const double ds = 0.25;
        for (const Piece& pc : pieces) {
            const int n = std::max(1, static_cast<int>(pc.len / ds));
            const double d = pc.len / n;
            for (int i = 0; i < n; ++i) {
                world.push_back({ static_cast<float>(x), static_cast<float>(z), static_cast<float>(h) });
                const double dh = pc.radius == 0 ? 0.0 : d / pc.radius * 180.0 / PI;
                const double hm = (h + dh * 0.5) * PI / 180.0;
                x += std::sin(hm) * d;
                z += std::cos(hm) * d;
                h += dh;
            }
        }
    }
    auto worldAt = [&](double q, double lateral) {
        q -= std::floor(q);
        const double f = q * static_cast<double>(world.size());
        const size_t i0 = static_cast<size_t>(f) % world.size(), i1 = (i0 + 1) % world.size();
        const double u = f - std::floor(f);
        WorldPt a = world[i0], b = world[i1];
        WorldPt r{ static_cast<float>(a.x + (b.x - a.x) * u), static_cast<float>(a.z + (b.z - a.z) * u),
                   static_cast<float>(a.h + (b.h - a.h) * u) };
        const double hr = r.h * PI / 180.0;
        r.x += static_cast<float>(std::cos(hr) * lateral);   // right of the line
        r.z -= static_cast<float>(std::sin(hr) * lateral);
        return r;
    };
    auto lateralOf = [](int num, double q) { return 1.6 * std::sin(num * 1.7 + q * 2 * PI * 3.0); };
    auto wrapYaw = [](float h) {
        float y = std::fmod(h, 360.0f);
        if (y > 180.0f) y -= 360.0f;
        if (y < -180.0f) y += 360.0f;
        return y;
    };
    // The player's laps: 4, then a PB, then tailSecs into the next.
    constexpr int NB = 4;
    constexpr double HZ = 100.0;
    const double paces[NB + 1] = { 1.000, 0.992, 1.006, 1.002, 0.978 };   // lap 2 a PB; the last: a PB
    std::vector<Lap> laps;
    std::vector<double> lapStart;   // session secs
    double totalSecs = 0;
    for (int i = 0; i <= NB; ++i) {
        laps.push_back(makeLap(prof, paces[i], i < NB ? 0.010 + 0.001 * i : 0.016, i < NB ? 1.3 * i : 2.6));
        lapStart.push_back(totalSecs);
        totalSecs += laps.back().total();
    }
    const double raceEnd = totalSecs;
    totalSecs += tailSecs;
    // The player's distance in laps at session time t.
    auto playerD = [&](double t) {
        for (int i = NB; i >= 0; --i) {
            if (t < lapStart[i]) continue;
            if (i == NB && t >= raceEnd) return NB + 1.0 + laps[NB].posAt((t - raceEnd) * 1.004);
            return i + laps[i].posAt(t - lapStart[i]);
        }
        return 0.0;
    };

    // Everyone else: an offset in laps from the player, eased between keyframes,
    // so the order changes for real. Session time ~354 s; the reel plays the last
    // liveSecs live.
    const double liveStart = totalSecs - liveSecs;
    const double catchAt = liveStart + 14.0;   // the player reaches backmarker 12
    const double crashAt = liveStart + 32.0;   // 22 goes down just ahead
    struct Key { double t, off; };
    auto keyed = [](const std::vector<Key>& ks, double t) {
        if (t <= ks.front().t) return ks.front().off;
        for (size_t i = 1; i < ks.size(); ++i) {
            if (t > ks[i].t) continue;
            double u = (t - ks[i - 1].t) / (ks[i].t - ks[i - 1].t);
            u = u * u * (3 - 2 * u);
            return ks[i - 1].off + (ks[i].off - ks[i - 1].off) * u;
        }
        return ks.back().off;
    };
    const double E = totalSecs;
    const std::vector<Key> k10 = { { 0, 0.045 }, { 110, 0.030 }, { 200, 0.075 }, { E, 0.085 } };   // takes the lead from 22
    const std::vector<Key> k22 = { { 0, 0.075 }, { 110, 0.050 }, { 200, 0.030 }, { liveStart, 0.022 }, { crashAt, 0.035 } };
    const std::vector<Key> k7  = { { 0, 0.020 }, { 90, 0.008 }, { 150, -0.012 }, { E, -0.025 } };  // the player gets by
    const std::vector<Key> k3  = { { 0, -0.030 }, { E, -0.060 } };
    const std::vector<Key> k18 = { { 0, -0.070 }, { 140, -0.050 }, { 240, -0.040 }, { E, -0.035 } };  // gets by 3
    const std::vector<Key> k31 = { { 0, -0.100 }, { E, -0.170 } };
    double stopD = -1e9, rejoinOff = 0;
    auto offsetOf = [&](int num, double t, double dP) {
        switch (num) {
            case 4:  return 0.0;
            case 10: return keyed(k10, t);
            case 7:  return keyed(k7, t);
            case 3:  return keyed(k3, t);
            case 18: return keyed(k18, t);
            case 31: return keyed(k31, t);
            case 12: {   // a backmarker: a lap down by catchAt, closing faster late
                const double u = t / catchAt;
                return -0.13 - 0.87 * u * u;
            }
            case 22: {
                if (t < crashAt) return keyed(k22, t);
                if (stopD < -1e8) stopD = dP + keyed(k22, crashAt);
                if (t < crashAt + 4.0) { rejoinOff = stopD - dP; return rejoinOff; }   // down, then picking it up
                const double k = std::min(1.0, (t - crashAt - 4.0) / 10.0);
                return rejoinOff + (-0.012 - rejoinOff) * k;
            }
        }
        return 0.0;
    };

    double lastThrottle = 0.6, lastBrake = 0.0;
    float fuel = 6.4f;
    auto telemetry = [&](double p, double t) {
        const double f = std::clamp(p, 0.0, 0.99999) * Profile::N;
        const int i = static_cast<int>(f);
        const double u = f - i;
        auto at = [&](int k) { return prof.speed[(k + Profile::N) % Profile::N]; };
        const double v = at(i) + (at(i + 1) - at(i)) * u;
        const double a = (at(i + 3) - at(i - 3)) / 6.0;
        TelemetryRow r;
        r.speed = static_cast<float>(v);
        const double band = 5.2;
        const int gear = std::clamp(1 + static_cast<int>(v / band), 1, 5);
        const double inGear = std::clamp(v / band - (gear - 1), 0.0, 1.0);
        r.gear = gear;
        r.rpm = static_cast<int>(5400 + 5600 * inGear);
        const double thr = std::clamp(0.55 + a * 1.8, 0.05, 1.0);
        const double brk = std::clamp(-a * 1.6 - 0.05, 0.0, 0.85);
        lastThrottle += (thr - lastThrottle) * 0.25;
        lastBrake += (brk - lastBrake) * 0.25;
        r.throttle = static_cast<float>(lastThrottle);
        r.frontBrake = static_cast<float>(lastBrake);
        fuel -= 0.00004f;
        r.fuel = fuel;
        const WorldPt w = worldAt(p, lateralOf(4, p));
        r.posX = w.x; r.posZ = w.z;
        r.yaw = wrapYaw(w.h);
        r.trackPos = static_cast<float>(p);
        r.time = static_cast<float>(t);
        // A wheelie while the reel's cursor rests on the FMX switch (43.4 to
        // 47.6 s into the live window), so the FMX preview shows a real trick.
        // Under 5 s, so it earns no Wheelie King toast in the middle of the reel.
        {
            const double w0 = liveStart + 43.4, w1 = liveStart + 47.6;
            if (t > w0 && t < w1) {
                const double up = std::min({ 1.0, (t - w0) / 0.35, (w1 - t) / 0.35 });
                r.frontMaterial = 0;
                r.pitch = static_cast<float>(-24.0 * up * up * (3 - 2 * up));
                r.throttle = 0.85f;
            }
        }
        host.telemetryFrame(r);
    };
    // Per rider: distance last step, lap start, splits so far, best lap.
    struct State { double d = 0; double lapT0 = 0; int split[2] = { -1, -1 }; int laps = 0; int best = 0; };
    State st[NR];
    int overallBest = 0;
    double dNow[NR];
    auto classifyNow = [&](double t) {
        int order[NR];
        for (int i = 0; i < NR; ++i) order[i] = i;
        std::sort(order, order + NR, [&](int a, int b) { return dNow[a] > dNow[b]; });
        const double lapSecs = laps[0].total();
        std::vector<ClassRow> rows;
        for (int k = 0; k < NR; ++k) {
            const int i = order[k];
            ClassRow r{ .num = grid[i].num };
            r.best = st[i].best;
            r.laps = st[i].laps;
            const int lead = order[0];
            const int down = st[lead].laps - st[i].laps;
            if (k == 0) r.gap = 0;
            else if (dNow[lead] - dNow[i] >= 1.0) { r.gapLaps = down; r.gap = 0; }
            else r.gap = static_cast<int>((dNow[lead] - dNow[i]) * lapSecs * 1000);
            r.bestLapNum = 1;
            rows.push_back(r);
        }
        host.classify(6, static_cast<int>(t * 1000), rows);
    };

    rec::nowUs = 1000;
    double nextClassify = 0;
    const int steps = static_cast<int>(totalSecs * HZ);
    for (int k = 0; k <= steps; ++k) {
        const double t = k / HZ;
        rec::nowUs = 1000 + static_cast<uint64_t>(t * 1e6);
        const double dP = playerD(t);
        bool crossed = false;
        std::vector<TrackRow> rows;
        for (int i = 0; i < NR; ++i) {
            const int num = grid[i].num;
            const double d = dP + offsetOf(num, t, dP);
            dNow[i] = d;
            // Split and line crossings since the last step (thirds of a lap).
            if (k > 0) {
                const int before = static_cast<int>(std::floor(st[i].d * 3)), after = static_cast<int>(std::floor(d * 3));
                for (int c = before + 1; c <= after; ++c) {
                    const int lapNum = (c - 1) / 3 - (c % 3 == 0 ? 0 : 0);
                    const int third = ((c % 3) + 3) % 3;
                    const int inLapMs = static_cast<int>((t - st[i].lapT0) * 1000);
                    if (c <= 0) continue;   // still on the grid side of the line
                    if (third == 1 || third == 2) {
                        st[i].split[third - 1] = inLapMs;
                        host.raceSplit(6, num, c / 3, third - 1, inLapMs);
                    } else {
                        const int ln = c / 3 - 1;
                        const bool pb = st[i].best == 0 || inLapMs < st[i].best;
                        const bool ob = overallBest == 0 || inLapMs < overallBest;
                        if (pb) st[i].best = inLapMs;
                        if (ob) overallBest = inLapMs;
                        host.raceLap(6, num, ln, inLapMs, ob ? 2 : pb ? 1 : 0, st[i].split[0], st[i].split[1]);
                        st[i].laps = ln + 1;
                        st[i].lapT0 = t;
                        st[i].split[0] = st[i].split[1] = -1;
                        crossed = true;
                    }
                    (void)lapNum;
                }
            }
            st[i].d = d;
            double q = d - std::floor(d);
            const WorldPt w = worldAt(q, lateralOf(num, q));
            rows.push_back({ .num = num, .trackPos = static_cast<float>(q),
                             .crashed = (num == 22 && t >= crashAt && t < crashAt + 3.0) ? 1 : 0,
                             .posX = w.x, .posZ = w.z, .yaw = wrapYaw(w.h) });
        }
        host.raceTrackPosition(rows);
        if (crossed || t >= nextClassify) { classifyNow(t); nextClassify = t + 1.0; }
        telemetry(dP - std::floor(dP), t);
        int nq, ns;
        host.m_draw(0, &nq, nullptr, &ns, nullptr);
    }
    fprintf(stderr, "wrote %s: %.1f s, the PB lap ends %.1f s before the end, live from %.1f s\n",
            out, totalSecs, tailSecs, liveStart);
    return 0;
}
