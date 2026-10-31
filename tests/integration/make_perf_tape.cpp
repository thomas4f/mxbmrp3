// ============================================================================
// tests/integration/make_perf_tape.cpp
// Generate the SYNTHETIC BUSY TAPE the perf ledger replays (run_perf_ledger.sh):
// a full 50-rider practice + race on the perf_scenario.h circuit, written in the
// recorder's MXBHREC format with real timestamps, so the same file drives the
// headless ledger and, on Windows, tools/replay against the shipping DLL.
//
// Nothing in the committed fixtures is busy: the biggest grid (Farm14, 24
// riders) was slimmed to classifications only, and the only tape with track
// positions has six riders (tests/integration/tapes/README.md). This one
// carries every stream a crowded server sends at once -- classifications,
// track positions, laps, splits, communications and the player's telemetry --
// derived from simulated motion rather than scripted, so standings reshuffle,
// gaps move and laps complete the way a race makes them.
//
// DETERMINISTIC BY CONSTRUCTION: no clock, no RNG, fixed-step integration. The
// same binary writes the same bytes, which run_perf_ledger.sh relies on (it
// regenerates the tape every run instead of committing a binary).
//
// THE RATES ARE THE GAME'S, measured on a real 25-rider online capture
// (2026-10-28, practice + race spectating + warmup, 15 min): track positions
// and the display rider's vehicle data arrive ONCE PER FRAME, classifications
// ~30 Hz while riding (~24 spectating), player telemetry 100 Hz. Per-frame
// streams are written at 120 Hz, the ledger's frame rate, so each simulated
// frame gets exactly one, as in the game. Draw is NOT written -- the ledger
// pumps frames at a fixed simulated cadence of its own, so the frame count
// never depends on the recording machine's FPS. The grid stays at the API's
// 50 (the capture had 25): this tape is the worst case, the capture the
// typical one.
//
// Portable C++17 (no Windows headers): built natively, runs in well under a
// second.   g++ -O2 -std=c++17 -I harness make_perf_tape.cpp -o make_perf_tape
//           ./make_perf_tape out.tape
// ============================================================================
#include "plugin_api.h"
#include "tape.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

// perf_scenario.h needs the segment struct by this name. TrackSegmentRow is the
// harness mirror of mxb_api.h's layout; the recorder writes exactly that array.
struct SPluginsTrackSegment_t {
    int m_iType; float m_fLength, m_fRadius, m_fAngle; float m_afStart[2]; float m_fHeight;
};
#include "perf_scenario.h"

namespace {

constexpr int    kRiders      = PERF_RIDERS;     // 50, the API maximum
constexpr int    kPlayer      = 1;               // race number of the local rider
constexpr double kStepS       = 1.0 / 600.0;     // integration step, a divisor of every rate below
constexpr double kFrameHz     = 120.0;           // positions + vehicle data, one per ledger frame
constexpr double kClassHz     = 30.0;
constexpr double kTelemHz     = 100.0;
constexpr double kPracticeS   = 40.0;
constexpr double kGridS       = 8.0;             // pre-start before the race goes green
constexpr double kRaceS       = 240.0;
constexpr double kAfterS      = 12.0;            // race over -> completed

class Writer {
public:
    bool open(const char* path) {
        m_f = std::fopen(path, "wb");
        if (!m_f) return false;
        tape::FileHeader h{};
        std::memcpy(h.magic, "MXBHREC\0", 8);
        h.version = 1;
        std::fwrite(&h, sizeof(h), 1, m_f);
        return true;
    }
    void write(tape::EventType t, double simS, const void* data, uint32_t size) {
        tape::EventHeader eh{ static_cast<uint32_t>(t), size,
                              static_cast<uint64_t>(std::llround(simS * 1e6)) };
        std::fwrite(&eh, sizeof(eh), 1, m_f);
        if (data && size) std::fwrite(data, size, 1, m_f);
        ++m_count; m_endUs = eh.timestampUs;
    }
    template <typename T> void write(tape::EventType t, double simS, const T& v) {
        write(t, simS, &v, static_cast<uint32_t>(sizeof(T)));
    }
    void close() {
        tape::FileHeader h{};
        std::memcpy(h.magic, "MXBHREC\0", 8);
        h.version = 1;
        h.numEvents = m_count;
        h.endTimeUs = m_endUs;
        std::fseek(m_f, 0, SEEK_SET);
        std::fwrite(&h, sizeof(h), 1, m_f);
        std::fclose(m_f);
    }
    uint32_t count() const { return m_count; }
private:
    FILE* m_f = nullptr;
    uint32_t m_count = 0;
    uint64_t m_endUs = 0;
};

// Same arc maths as MapHud's advanceAlongArc (map_hud_internal.h), so the
// positions written here sit ON the centerline the map draws.
void advance(float& x, float& y, float& headingDeg, float radius, float dist) {
    const float kDeg = 3.14159265f / 180.0f;
    float h0 = headingDeg * kDeg;
    if (std::fabs(radius) < 0.01f) { x += std::sin(h0) * dist; y += std::cos(h0) * dist; return; }
    float theta = dist / radius;
    x += radius * (std::cos(h0) - std::cos(h0 + theta));
    y += radius * (std::sin(h0 + theta) - std::sin(h0));
    headingDeg += theta / kDeg;
}

struct Sample { float x, z, yawDeg; };

struct Rider {
    int num;
    double dist = 0.0;        // metres since the session's start line
    double speed = 0.0;       // m/s this step
    int laps = 0;             // completed laps
    int splitsThisLap = 0;
    double lapStartS = 0.0;
    double splitStartS = 0.0;
    int bestLapMs = 0, bestLapNum = 0;
    int splitMs[2] = {0, 0};
    bool finished = false;
};

}  // namespace

int main(int argc, char** argv) {
    const char* out = argc > 1 ? argv[1] : "perf_busy.tape";

    SPluginsTrackSegment_t* segs = nullptr;
    float trackLen = 0.0f;
    const int numSegs = buildPerfTrack(&segs, &trackLen);

    // Pre-sample the centerline every metre: positions are then a table lookup,
    // and the generator stays a fraction of a second however long the session.
    std::vector<Sample> line;
    {
        float x = 0, y = 0, h = segs[0].m_fAngle;
        int s = 0; float into = 0.0f;
        const int n = static_cast<int>(trackLen) + 1;
        line.reserve(n);
        for (int m = 0; m < n; ++m) {
            line.push_back({x, y, h});
            float step = 1.0f;
            while (step > 0.0f && s < numSegs) {
                const float left = segs[s].m_fLength - into;
                const float d = step < left ? step : left;
                advance(x, y, h, segs[s].m_iType == 0 ? 0.0f : segs[s].m_fRadius, d);
                into += d; step -= d;
                if (into >= segs[s].m_fLength - 1e-4f) { ++s; into = 0.0f; }
            }
        }
    }
    auto sampleAt = [&](double dist) -> const Sample& {
        double d = std::fmod(dist, static_cast<double>(trackLen));
        if (d < 0) d += trackLen;
        size_t i = static_cast<size_t>(d);
        return line[i < line.size() ? i : line.size() - 1];
    };

    Writer w;
    if (!w.open(out)) { std::fprintf(stderr, "cannot write %s\n", out); return 1; }

    // --- event setup -------------------------------------------------------
    SPluginsBikeEvent_t ev{};
    std::strcpy(ev.m_szRiderName, "Player");
    std::strcpy(ev.m_szBikeID, "perf450");
    std::strcpy(ev.m_szBikeName, "Perf 450");
    ev.m_iNumberOfGears = 5; ev.m_iMaxRPM = 13500; ev.m_iLimiter = 13000; ev.m_iShiftRPM = 12000;
    ev.m_fMaxFuel = 6.5f;
    std::strcpy(ev.m_szCategory, "MX1");
    std::strcpy(ev.m_szTrackID, "perftrack");
    std::strcpy(ev.m_szTrackName, "Perf Track");
    ev.m_fTrackLength = trackLen;
    ev.m_iType = 2;                                   // online race
    std::strcpy(ev.m_szServerName, "Perf Server 50");
    w.write(tape::EventType::EventInit, 0.0, ev);

    SPluginsRaceEvent_t re{};
    re.m_iType = 2;
    std::strcpy(re.m_szName, "Perf Server 50");
    std::strcpy(re.m_szTrackName, "Perf Track");
    re.m_fTrackLength = trackLen;
    w.write(tape::EventType::RaceEvent, 0.0, re);

    {   // [int numSegments][SPluginsTrackSegment_t...], the recorder's layout
        std::vector<uint8_t> buf(sizeof(int) + numSegs * sizeof(SPluginsTrackSegment_t));
        std::memcpy(buf.data(), &numSegs, sizeof(int));
        std::memcpy(buf.data() + sizeof(int), segs, numSegs * sizeof(SPluginsTrackSegment_t));
        w.write(tape::EventType::TrackCenterline, 0.0, buf.data(), static_cast<uint32_t>(buf.size()));
    }

    // Names of mixed length, because the standings' name column and the
    // glyph count both depend on it.
    static const char* kFirst[] = { "Max", "Jonathan", "Eli", "Alexander", "Ken", "Sebastian",
                                    "Tom", "Christopher", "Jo", "Maximilian" };
    static const char* kLast[]  = { "Ito", "Lindqvist", "Ross", "Vanderhoeven", "Moe",
                                    "Kowalczyk", "Ng", "Fitzgerald-Hart", "Bo", "Oyelaran" };
    std::vector<Rider> riders(kRiders);
    for (int i = 0; i < kRiders; ++i) {
        riders[i].num = (i == 0) ? kPlayer : (i + 1) * 3 % 997 + 2;   // unordered numbers
        SPluginsRaceAddEntry_t e{};
        e.m_iRaceNum = riders[i].num;
        if (i == 0) std::strcpy(e.m_szName, "Player");
        else std::snprintf(e.m_szName, sizeof(e.m_szName), "%s %s", kFirst[i % 10], kLast[(i * 7) % 10]);
        std::strcpy(e.m_szBikeName, (i % 3 == 0) ? "Perf 450" : (i % 3 == 1) ? "Perf 250" : "Perf 350");
        std::strcpy(e.m_szBikeShortName, (i % 3 == 0) ? "P450" : (i % 3 == 1) ? "P250" : "P350");
        std::strcpy(e.m_szCategory, (i % 3 == 1) ? "MX2" : "MX1");
        e.m_iNumberOfGears = 5; e.m_iMaxRPM = 13500;
        w.write(tape::EventType::RaceAddEntry, 0.0, e);
    }

    // --- sessions ----------------------------------------------------------
    auto session = [&](double t, int sess, int state, int lengthMs, int laps) {
        SPluginsRaceSession_t s{};
        s.m_iSession = sess; s.m_iSessionState = state; s.m_iSessionLength = lengthMs;
        s.m_iSessionNumLaps = laps; s.m_fAirTemperature = 21.0f;
        w.write(tape::EventType::RaceSession, t, s);
    };
    auto sessionState = [&](double t, int sess, int state, int lengthMs) {
        SPluginsRaceSessionState_t s{ sess, state, lengthMs };
        w.write(tape::EventType::RaceSessionState, t, s);
    };

    // Each rider's pace: a spread of ~6% plus a slow weave, so the order
    // changes on track instead of freezing after the start.
    auto paceAt = [&](int i, double t) {
        const double base = 21.0 + 1.3 * std::cos(i * 2.399);           // golden-angle spread
        const double weave = 0.9 * std::sin(t * (0.05 + 0.003 * i) + i);
        return base + weave;
    };

    struct Phase { int session; double start, end; int lengthMs; };
    const Phase phases[2] = {
        { 1, 1.0, 1.0 + kPracticeS, static_cast<int>(kPracticeS * 1000) },               // practice
        { 6, 1.0 + kPracticeS + kGridS, 1.0 + kPracticeS + kGridS + kRaceS,
          static_cast<int>(kRaceS * 1000) },                                             // race 1
    };

    std::vector<uint8_t> posBuf(sizeof(tape::TrackPositionPrefix) + kRiders * sizeof(SPluginsRaceTrackPosition_t));
    std::vector<uint8_t> clsBuf(sizeof(tape::ClassificationPrefix) + kRiders * sizeof(SPluginsRaceClassificationEntry_t));
    std::vector<int> order(kRiders);
    bool playerRunning = false;
    double nextPos = 0.0, nextCls = 0.0, nextTel = 0.0;
    int penaltiesIssued = 0;

    for (int p = 0; p < 2; ++p) {
        const Phase& ph = phases[p];
        const bool race = ph.session >= 6;
        const double open = race ? ph.start - kGridS : ph.start;
        session(open, ph.session, race ? 256 : 16, ph.lengthMs, 0);
        if (race) sessionState(ph.start, ph.session, 16, ph.lengthMs);
        for (auto& r : riders) {
            r.dist = 0.0; r.laps = 0; r.splitsThisLap = 0; r.lapStartS = ph.start;
            r.splitStartS = ph.start; r.bestLapMs = 0; r.bestLapNum = 0; r.finished = false;
        }
        // Practice spreads the field around the lap; the race starts from a grid.
        for (int i = 0; i < kRiders; ++i)
            riders[i].dist = race ? -2.0 * (i / 2) : (trackLen * i) / kRiders;

        // The player joins the track (RunInit/RunStart) and stays on it.
        if (!playerRunning) {
            SPluginsBikeSession_t bs{};
            bs.m_iSession = ph.session; bs.m_fAirTemperature = 21.0f;
            std::strcpy(bs.m_szSetupFileName, "perf.stp");
            w.write(tape::EventType::RunInit, open, bs);
            w.write(tape::EventType::RunStart, open, nullptr, 0);
            playerRunning = true;
        }

        const double stop = ph.end + (race ? kAfterS : 0.0);
        bool overSent = false;
        for (double t = open; t < stop; t += kStepS) {
            const bool green = t >= ph.start;
            for (int i = 0; i < kRiders; ++i) {
                Rider& r = riders[i];
                r.speed = (green && !r.finished) ? paceAt(i, t) : 0.0;
                const double before = r.dist;
                r.dist += r.speed * kStepS;
                // Splits at 1/3 and 2/3 of the lap, the lap at the line. A lap
                // only counts from the first full crossing after the start.
                const double lapPos = r.dist - r.laps * static_cast<double>(trackLen);
                if (r.splitsThisLap < 2 && lapPos >= trackLen * (r.splitsThisLap + 1) / 3.0 && before >= 0.0) {
                    const int ms = static_cast<int>((t - r.splitStartS) * 1000.0);
                    r.splitMs[r.splitsThisLap] = (r.splitsThisLap == 0) ? ms : r.splitMs[0] + ms;
                    SPluginsRaceSplit_t sp{ ph.session, r.num, r.laps, r.splitsThisLap,
                                            r.splitMs[r.splitsThisLap] };
                    w.write(tape::EventType::RaceSplit, t, sp);
                    ++r.splitsThisLap;
                    r.splitStartS = t;
                }
                if (lapPos >= trackLen) {
                    const int lapMs = static_cast<int>((t - r.lapStartS) * 1000.0);
                    int best = 0;
                    if (r.bestLapMs == 0 || lapMs < r.bestLapMs) {
                        r.bestLapMs = lapMs; r.bestLapNum = r.laps; best = 1;
                    }
                    SPluginsRaceLap_t lap{ ph.session, r.num, r.laps, 0, lapMs,
                                           { r.splitMs[0], r.splitMs[1] }, best };
                    w.write(tape::EventType::RaceLap, t, lap);
                    ++r.laps; r.splitsThisLap = 0; r.lapStartS = t; r.splitStartS = t;
                    if (race && t >= ph.end) r.finished = true;   // checkered after time
                }
            }

            // Two penalties mid-race: the event log, the notices and the
            // standings' penalty column all react to them.
            if (race && penaltiesIssued < 2 && t >= ph.start + 60.0 + 45.0 * penaltiesIssued) {
                SPluginsRaceCommunication_t c{};
                c.m_iSession = ph.session; c.m_iRaceNum = riders[7 + 11 * penaltiesIssued].num;
                c.m_iCommunication = 2; c.m_iOffence = 1; c.m_iLap = 1; c.m_iType = 0; c.m_iTime = 10000;
                w.write(tape::EventType::RaceCommunication, t, c);
                ++penaltiesIssued;
            }
            if (race && !overSent && t >= ph.end) {
                sessionState(t, ph.session, 16 | 512, ph.lengthMs);
                overSent = true;
            }

            // Player telemetry, 100 Hz.
            if (t >= nextTel) {
                nextTel += 1.0 / kTelemHz;
                const Rider& r = riders[0];
                const Sample& s = sampleAt(r.dist);
                const double phase = t * 0.9;
                SPluginsBikeData_t d{};
                d.m_fSpeedometer = static_cast<float>(r.speed);
                d.m_iGear = r.speed < 1.0 ? 1 : 1 + static_cast<int>(r.speed / 6.0) % 5;
                d.m_iRPM = static_cast<int>(6000 + 6000 * (0.5 + 0.5 * std::sin(phase * 3.0)));
                d.m_fEngineTemperature = 88.0f; d.m_fWaterTemperature = 82.0f;
                d.m_fFuel = static_cast<float>(6.5 - 0.004 * (t - open));
                d.m_fPosX = s.x; d.m_fPosZ = s.z; d.m_fPosY = 0.0f;
                d.m_fYaw = s.yawDeg;
                d.m_fRoll = static_cast<float>(35.0 * std::sin(phase));
                d.m_fPitch = static_cast<float>(4.0 * std::sin(phase * 1.7));
                d.m_fAccelerationX = static_cast<float>(0.8 * std::sin(phase));
                d.m_fAccelerationZ = static_cast<float>(0.4 * std::cos(phase * 0.5));
                d.m_afSuspLength[0] = 0.15f; d.m_afSuspLength[1] = 0.14f;
                d.m_afSuspVelocity[0] = static_cast<float>(0.3 * std::sin(t * 7.0));
                d.m_afSuspVelocity[1] = static_cast<float>(0.3 * std::sin(t * 6.3));
                d.m_fThrottle = static_cast<float>(0.5 + 0.5 * std::sin(phase * 2.0));
                d.m_fFrontBrake = d.m_fThrottle < 0.2f ? 0.6f : 0.0f;
                d.m_afWheelSpeed[0] = d.m_afWheelSpeed[1] = static_cast<float>(r.speed);
                d.m_aiWheelMaterial[0] = d.m_aiWheelMaterial[1] = 1;
                std::vector<uint8_t> tb(sizeof(d) + 2 * sizeof(float));
                const float st = static_cast<float>(t - open);
                const float pos = static_cast<float>(std::fmod(std::fmax(r.dist, 0.0), static_cast<double>(trackLen)) / trackLen);
                std::memcpy(tb.data(), &d, sizeof(d));
                std::memcpy(tb.data() + sizeof(d), &st, sizeof(float));
                std::memcpy(tb.data() + sizeof(d) + sizeof(float), &pos, sizeof(float));
                w.write(tape::EventType::RunTelemetry, t, tb.data(), static_cast<uint32_t>(tb.size()));
            }

            if (t >= nextPos) {
                nextPos += 1.0 / kFrameHz;
                // The display rider's live state, every frame like the game.
                SPluginsRaceVehicleData_t vd{};
                vd.m_iRaceNum = kPlayer; vd.m_iActive = 1;
                vd.m_iRPM = 6000 + static_cast<int>(riders[0].speed * 250.0);
                vd.m_iGear = riders[0].speed < 1.0 ? 1 : 1 + static_cast<int>(riders[0].speed / 6.0) % 5;
                vd.m_fSpeedometer = static_cast<float>(riders[0].speed);
                vd.m_fThrottle = 0.8f;
                vd.m_fLean = static_cast<float>(35.0 * std::sin(t * 0.9));
                w.write(tape::EventType::RaceVehicleData, t, vd);
                auto* pre = reinterpret_cast<tape::TrackPositionPrefix*>(posBuf.data());
                pre->numVehicles = kRiders;
                auto* rows = reinterpret_cast<SPluginsRaceTrackPosition_t*>(posBuf.data() + sizeof(*pre));
                for (int i = 0; i < kRiders; ++i) {
                    const Sample& s = sampleAt(riders[i].dist);
                    rows[i].m_iRaceNum = riders[i].num;
                    rows[i].m_fPosX = s.x + 1.5f * static_cast<float>(i % 3 - 1);   // lines across the track
                    rows[i].m_fPosY = 0.0f;
                    rows[i].m_fPosZ = s.z;
                    rows[i].m_fYaw = s.yawDeg;
                    rows[i].m_fTrackPos = static_cast<float>(
                        std::fmod(std::fmax(riders[i].dist, 0.0), static_cast<double>(trackLen)) / trackLen);
                    rows[i].m_iCrashed = 0;
                }
                w.write(tape::EventType::RaceTrackPosition, t, posBuf.data(), static_cast<uint32_t>(posBuf.size()));
            }

            if (t >= nextCls) {
                nextCls += 1.0 / kClassHz;
                for (int i = 0; i < kRiders; ++i) order[i] = i;
                // Insertion sort by distance: deterministic, stable, and the
                // order barely changes between calls so it is near-linear.
                for (int a = 1; a < kRiders; ++a) {
                    int v = order[a], b = a - 1;
                    while (b >= 0 && riders[order[b]].dist < riders[v].dist) { order[b + 1] = order[b]; --b; }
                    order[b + 1] = v;
                }
                auto* pre = reinterpret_cast<tape::ClassificationPrefix*>(clsBuf.data());
                pre->header.m_iSession = ph.session;
                pre->header.m_iSessionState = (race && t >= ph.end) ? (16 | 512) : (green ? 16 : 256);
                pre->header.m_iSessionTime = static_cast<int>((green ? (ph.end - t) : ph.lengthMs / 1000.0) * 1000.0);
                pre->header.m_iNumEntries = kRiders;
                pre->numEntries = kRiders;
                auto* rows = reinterpret_cast<SPluginsRaceClassificationEntry_t*>(clsBuf.data() + sizeof(*pre));
                const Rider& lead = riders[order[0]];
                for (int k = 0; k < kRiders; ++k) {
                    const Rider& r = riders[order[k]];
                    SPluginsRaceClassificationEntry_t& e = rows[k];
                    e = SPluginsRaceClassificationEntry_t{};
                    e.m_iRaceNum = r.num;
                    e.m_iBestLap = r.bestLapMs;
                    e.m_iBestLapNum = r.bestLapNum;
                    e.m_iNumLaps = r.laps;
                    if (race) {
                        const double behind = lead.dist - r.dist;
                        e.m_iGapLaps = static_cast<int>(behind / trackLen);
                        const double v = r.speed > 1.0 ? r.speed : 20.0;
                        e.m_iGap = (k == 0) ? 0 : static_cast<int>(std::fmod(behind, static_cast<double>(trackLen)) / v * 1000.0);
                    } else {
                        // Practice ranks by best lap; the game reports the gap to P1's best.
                        e.m_iGap = 0;
                    }
                    e.m_iPenalty = (penaltiesIssued > 0 && order[k] == 7) ? 10000 :
                                   (penaltiesIssued > 1 && order[k] == 18) ? 10000 : 0;
                }
                w.write(tape::EventType::RaceClassification, t, clsBuf.data(), static_cast<uint32_t>(clsBuf.size()));
            }
        }
        if (race) sessionState(stop, ph.session, 32, ph.lengthMs);
        else      sessionState(ph.end, ph.session, 32, ph.lengthMs);
    }

    w.write(tape::EventType::RunStop, phases[1].end + kAfterS, nullptr, 0);
    w.write(tape::EventType::RunDeinit, phases[1].end + kAfterS, nullptr, 0);
    w.close();
    std::free(segs);
    std::printf("wrote %s: %u events, %d riders, %.0f m track, %.0f s simulated\n",
                out, w.count(), kRiders, trackLen, phases[1].end + kAfterS);
    return 0;
}
