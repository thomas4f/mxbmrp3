// ============================================================================
// tests/integration/tests/stripchart_parity_test.cpp
// Pins the rendered primitives of the four "strip chart" HUDs — Telemetry,
// Rumble, Performance and Session Charts — whose shared grid-line / axis-label /
// history-polyline blocks were consolidated into the BaseHud strip-chart helpers
// (addStripChartFrame / addStripChartHistoryLine, base_hud.{h,cpp}). NOTE on
// coverage: the deterministic setup keeps the rumble history deques EMPTY (rumble
// processing is disabled for determinism) and telemetry draws its polylines via
// its own inline multi-channel loop — so addStripChartHistoryLine's non-empty
// path is pinned only lightly here; it was a verbatim move of RumbleHud's
// addHistoryGraph, and its geometry math is what the frame/grid checks share.
// There is no
// pixel test for these HUDs, so this is the guard that the consolidation (and any
// future change to the shared helpers) is quad/string-identical: each HUD is
// rendered alone from a deterministic synthetic session and its full primitive
// fingerprint (counts + position/color/text checksums) is compared against golden
// values captured from the pre-refactor renderer. The additive checksums are
// order-INDEPENDENT (sums), so each phase also pins an order-SENSITIVE rolling
// hash over the primitive streams in emission order (qorder/sorder — draw order
// IS z-order for this renderer, so a reordering regression must trip something);
// those hashes were pinned from the current build AFTER the pre-refactor parity
// had been proven, making the then-current emission order the reference.
//
// Isolation: every HUD section in a freshly saved settings.ini gets visible=0
// except the one under test (rewriting the real INI keeps this free of a
// hand-maintained HUD list), loaded via the LoadSettings hook. The baseline
// (everything off) is asserted EMPTY, so each phase's frame is exactly the
// target HUD's primitives — read straight from the real Draw export with
// state 0 (on track), the state that enables full telemetry.
//
// Determinism notes:
//  * Telemetry: TelemetryHud clears the shared history buffers on show, so the
//    fixed telemetry ramp is fed AFTER its phase INI is applied.
//  * Rumble: rumble processing is disabled via the hook, so the effect history
//    deques stay empty and the legend reads a constant 0% — the frame (grid +
//    axis labels + bars + legend) is fully deterministic. The moved history-
//    polyline path is exercised deterministically by the Telemetry HUD's traces
//    (same emission math; Rumble's own traces depend on wall-clock effect state).
//  * Performance: its graph values are measured (live fps / plugin time), so
//    only the deterministic subset is pinned: string count, the six axis labels
//    (exact text/pos/font/color), and the six 0%/50%/100% grid lines (found by
//    their exact grid thickness). The polyline is intentionally not pinned.
//  * Session Charts: fed a fixed 3-rider, 3-lap race; all four charts enabled.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include "ini.h"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <set>
#include <string>

// Local mirrors of the draw structs (mxb_api.h layout, default alignment).
struct QuadRow { float pos[4][2]; int sprite; unsigned long color; };
struct StrRow  { char text[100]; float pos[2]; int font; float size; int justify; unsigned long color; };

// Order-independent additive fingerprint of a frame (sums subtract cleanly, so a
// target HUD's contribution is exact even if a stray widget were ever visible),
// PLUS an order-SENSITIVE rolling hash per primitive stream: the additive sums
// are blind to draw order (a z-order regression reorders terms without moving
// any sum), so qorder/sorder chain every primitive's identity in EMISSION order
// and change whenever two primitives swap places.
struct Fp {
    int nq = 0, ns = 0;
    double qpos = 0, qcol = 0, qspr = 0;             // quads: corner coords / colors / sprites
    double spos = 0, scol = 0, smeta = 0, stext = 0; // strings: pos / color / font+justify+size / text hash
    uint64_t qorder = 0, sorder = 0;                 // ORDER-SENSITIVE rolling hashes
};

static double djb2(const char* s, size_t cap) {
    unsigned long h = 5381;
    for (size_t i = 0; i < cap && s[i]; ++i) h = h * 33 + (unsigned char)s[i];
    return (double)h;
}

// FNV-1a-style chaining step: mixing before AND after the xor makes the hash
// depend on the order of the chained values, not just their multiset.
static uint64_t chainU64(uint64_t h, uint64_t v) {
    return (h ^ (v + 0x9e3779b97f4a7c15ULL)) * 1099511628211ULL;
}
// Exact float identity (bit pattern) - no rounding, fully deterministic for a
// deterministic renderer.
static uint64_t fbits(float f) {
    uint32_t u = 0;
    memcpy(&u, &f, sizeof(u));
    return u;
}
static uint64_t chainQuad(uint64_t h, const QuadRow& q) {
    for (int c = 0; c < 4; ++c) {
        h = chainU64(h, fbits(q.pos[c][0]));
        h = chainU64(h, fbits(q.pos[c][1]));
    }
    h = chainU64(h, (uint64_t)q.color);
    h = chainU64(h, (uint64_t)(unsigned)q.sprite);
    return h;
}
static uint64_t chainStr(uint64_t h, const StrRow& s) {
    h = chainU64(h, fbits(s.pos[0]));
    h = chainU64(h, fbits(s.pos[1]));
    h = chainU64(h, (uint64_t)s.color);
    h = chainU64(h, (uint64_t)(unsigned)s.font);
    h = chainU64(h, (uint64_t)(unsigned)s.justify);
    h = chainU64(h, fbits(s.size));
    for (size_t i = 0; i < sizeof(s.text) && s.text[i]; ++i)
        h = chainU64(h, (unsigned char)s.text[i]);
    return h;
}

class DrawProbe {
public:
    explicit DrawProbe(PluginHost& host) : m_draw(host.sym<PFN_Draw>("Draw")) {}
    bool ok() const { return m_draw != nullptr; }

    // One Draw with state 0 (= on track), returning the raw arrays.
    void draw(int& nq, QuadRow*& q, int& ns, StrRow*& s) {
        int inq = 0, ins = 0; void* pq = nullptr; void* ps = nullptr;
        m_draw(0, &inq, &pq, &ins, &ps);
        nq = inq; ns = ins; q = (QuadRow*)pq; s = (StrRow*)ps;
    }

    Fp fingerprint() {
        int nq = 0, ns = 0; QuadRow* q = nullptr; StrRow* s = nullptr;
        draw(nq, q, ns, s);
        Fp f; f.nq = nq; f.ns = ns;
        for (int i = 0; i < nq; ++i) {
            for (int c = 0; c < 4; ++c) { f.qpos += q[i].pos[c][0] + q[i].pos[c][1]; }
            f.qcol += (double)q[i].color;
            f.qspr += (double)q[i].sprite;
            f.qorder = chainQuad(f.qorder, q[i]);
        }
        for (int i = 0; i < ns; ++i) {
            f.spos += s[i].pos[0] + s[i].pos[1];
            f.scol += (double)s[i].color;
            f.smeta += (double)s[i].font * 1000.0 + (double)s[i].justify * 100.0 + (double)s[i].size;
            f.stext += djb2(s[i].text, sizeof(s[i].text));
            f.sorder = chainStr(f.sorder, s[i]);
        }
        return f;
    }

private:
    PFN_Draw m_draw = nullptr;
};

static std::string fpStr(const Fp& f) {
    char buf[512];
    snprintf(buf, sizeof(buf),
             "nq=%d ns=%d qpos=%.9f qcol=%.0f qspr=%.0f spos=%.9f scol=%.0f smeta=%.6f stext=%.0f"
             " qorder=0x%016llx sorder=0x%016llx",
             f.nq, f.ns, f.qpos, f.qcol, f.qspr, f.spos, f.scol, f.smeta, f.stext,
             (unsigned long long)f.qorder, (unsigned long long)f.sorder);
    return buf;
}

// Golden fingerprint for one HUD phase. The additive fields were harvested from
// the pre-refactor build (parity to it was proven then); the ORDER hashes were
// pinned later from the verified-correct current build - the additive sums are
// order-independent, so without them a draw-order (z-order) regression would
// pass unnoticed.
struct Golden {
    int nq, ns;
    double qpos, qcol, qspr, spos, scol, smeta, stext;
    uint64_t qorder, sorder;
};

static void checkAgainst(const Fp& f, const Golden& g) {
    CHECK(f.nq == g.nq);
    CHECK(f.ns == g.ns);
    CHECK(f.qpos == doctest::Approx(g.qpos).epsilon(1e-9));
    CHECK(f.qcol == doctest::Approx(g.qcol).epsilon(1e-12));
    CHECK(f.qspr == doctest::Approx(g.qspr).epsilon(1e-12));
    CHECK(f.spos == doctest::Approx(g.spos).epsilon(1e-9));
    CHECK(f.scol == doctest::Approx(g.scol).epsilon(1e-12));
    CHECK(f.smeta == doctest::Approx(g.smeta).epsilon(1e-12));
    CHECK(f.stext == doctest::Approx(g.stext).epsilon(1e-12));
    CHECK(f.qorder == g.qorder);
    CHECK(f.sorder == g.sorder);
}

// Rewrite every `visible=` line of the saved INI: 0 for every HUD section except
// those in `on` (which get 1). Section-aware, preserves everything else, and needs
// no hand-maintained HUD list — the plugin's own save enumerates the sections.
// `overrides` rewrites additional (section, key) values (e.g. enabling the FPS
// section of the Performance HUD).
using KeyOverrides = std::map<std::pair<std::string, std::string>, std::string>;
static std::string withOnlyVisible(const std::string& text, const std::set<std::string>& on,
                                   const KeyOverrides& overrides = {}) {
    std::istringstream in(text);
    std::string out, line, section;
    while (std::getline(in, line)) {
        std::string t = ini::trim(line);
        if (!t.empty() && t[0] == '[') {
            size_t e = t.find(']');
            if (e != std::string::npos) section = t.substr(1, e - 1);
        } else {
            size_t eq = t.find('=');
            if (eq != std::string::npos) {
                std::string key = ini::trim(t.substr(0, eq));
                if (key == "visible") {
                    line = std::string("visible=") + (on.count(section) ? "1" : "0");
                } else {
                    auto it = overrides.find({ section, key });
                    if (it != overrides.end()) line = key + "=" + it->second;
                }
            }
        }
        out += line + "\n";
    }
    return out;
}

// The deterministic telemetry ramp (fed after the Telemetry HUD is shown, since
// showing it clears the shared history buffers). Exercises throttle / clutch /
// rpm traces with values spanning the 0..1 range plus near-zero stretches (the
// skip-when-both-near-zero path).
static void feedTelemetryRamp(PluginHost& host) {
    for (int i = 0; i < 60; ++i) {
        TelemetryRow r;
        r.speed = 10.0f + (i % 20);
        r.gear = 1 + (i % 5);
        r.throttle = (i % 10) * 0.1f;
        r.clutch = (i < 20) ? 0.0f : ((i % 4) * 0.25f);
        r.rpm = 2000 + (i % 30) * 300;
        r.time = 10.0f + i * 0.01f;
        host.telemetryFrame(r);
    }
}

TEST_CASE("strip-chart HUDs: primitive parity goldens") {
    const char* saveWin = "Z:\\tmp\\mxbmrp3-tests\\stripchart_parity\\";
    const std::string iniPath =
        "Z:\\tmp\\mxbmrp3-tests\\stripchart_parity\\mxbmrp3\\mxbmrp3_settings.ini";

    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(saveWin);
    DrawProbe probe(host);
    REQUIRE(probe.ok());

    // Rumble processing off BEFORE any telemetry: the effect histories stay empty
    // and the Rumble HUD's legend/traces are deterministic.
    host.rumbleSetEnabled(false);

    // Save the default INI once; each phase rewrites its visible= lines.
    host.save();
    const std::string defIni = ini::readFile(iniPath);
    REQUIRE_MESSAGE(!defIni.empty(), "no settings.ini written at " << iniPath);

    auto loadPhase = [&](const std::set<std::string>& on, const KeyOverrides& overrides = {}) {
        ini::writeFile(iniPath, withOnlyVisible(defIni, on, overrides));
        host.loadSettings(saveWin);
    };

    // --- Deterministic synthetic session: 3 riders, 3 laps of race data --------
    host.eventInit("Parity Track", "Parity Rider");
    host.raceEvent("Parity Track");
    host.session(6 /*Race1*/, 10);
    host.raceSessionState(6, 16 /*green*/);
    host.addEntry(5, "Alpha");
    host.addEntry(7, "Bravo");
    host.addEntry(9, "Charlie");
    host.classify(6, 0, { { 5 }, { 7 }, { 9 } });
    // Laps (rider, lap, time): fixed times, one invalid lap for the pace filter.
    host.raceLap(6, 5, 0, 90000, 2);
    host.raceLap(6, 7, 0, 91500);
    host.raceLap(6, 9, 0, 95000);
    host.raceLap(6, 5, 1, 91000);
    host.raceLap(6, 7, 1, 90500, 1);
    host.raceLap(6, 9, 1, 99000, 0, -1, -1, /*invalid=*/true);
    host.raceLap(6, 5, 2, 92000);
    host.raceLap(6, 7, 2, 93000);
    host.raceLap(6, 9, 2, 96000);
    host.classify(6, 275000, { { 5, 90000, 3 }, { 7, 90500, 3, 2000 }, { 9, 95000, 3, 17000 } });
    host.runInit(6);
    host.runStart();

    // --- Baseline: every HUD hidden; the frame must be empty & stable ----------
    loadPhase({});
    (void)probe.fingerprint();               // settle the state-0 view + rebuilds
    Fp base = probe.fingerprint();
    Fp base2 = probe.fingerprint();
    MESSAGE("baseline:   " << fpStr(base));
    CHECK(base.nq == 0);
    CHECK(base.ns == 0);
    CHECK(base2.nq == base.nq);
    CHECK(base2.ns == base.ns);

    // =========================================================================
    // Telemetry HUD (graphs + values, default elements)
    // =========================================================================
    {
        loadPhase({ "TelemetryHud" });        // becoming visible clears history
        feedTelemetryRamp(host);
        (void)probe.fingerprint();
        Fp f = probe.fingerprint();
        Fp f2 = probe.fingerprint();
        MESSAGE("telemetry:  " << fpStr(f));
        CHECK(fpStr(f2) == fpStr(f));         // stable across draws
        // GOLDEN(telemetry)
        checkAgainst(f, { 102, 24,
                          610.201179266, 436195095084.0, 0.0,
                          34.286767483, 85684202790.0, 24000.43, 14463422246.0,
                          0xc53667d279d58c8cULL, 0x362ba1aa23bf7c7cULL });
    }

    // =========================================================================
    // Rumble HUD (frame + bars + legend; effect histories empty)
    // =========================================================================
    {
        loadPhase({ "RumbleHud" });
        (void)probe.fingerprint();
        Fp f = probe.fingerprint();
        Fp f2 = probe.fingerprint();
        MESSAGE("rumble:     " << fpStr(f));
        CHECK(fpStr(f2) == fpStr(f));
        // GOLDEN(rumble)
        checkAgainst(f, { 6, 24,
                          32.838792920, 20551431668.0, 0.0,
                          32.679687142, 85248099315.0, 24400.42, 13744300338.0,
                          0x916c7e0777d715e0ULL, 0xf3c6c346d619579fULL });
    }

    // =========================================================================
    // Performance HUD (live-measured graphs: pin the deterministic subset)
    // =========================================================================
    {
        // Enable BOTH sections (FPS is off by default) so both addStripChartFrame
        // call sites are exercised.
        loadPhase({ "PerformanceHud" }, { { { "PerformanceHud", "elem_fps" }, "1" } });
        (void)probe.fingerprint();
        int nq = 0, ns = 0; QuadRow* q = nullptr; StrRow* s = nullptr;
        probe.draw(nq, q, ns, s);

        // The graph VALUES are live-measured (fps / plugin ms), so only the
        // deterministic subset is pinned: the string COUNT (the legend prints a
        // value whatever it reads; every string is emitted twice — shadow pass +
        // main), the six axis labels' full identity, and the grid-line geometry.
        MESSAGE("performance: nq=" << nq << " ns=" << ns);
        // GOLDEN(performance) — string count (2 title + 2x2 subhead + 6x2 axis + 16x2 legend)
        CHECK(ns == 50);

        const char* expected[] = { "250 FPS", "125 FPS", "0 FPS", "4.0 ms", "2.0 ms", "0.0 ms" };
        double lpos = 0, lcol = 0, lmeta = 0; int found = 0;
        uint64_t lorder = 0;   // ORDER-SENSITIVE: labels chained in emission order
        for (int i = 0; i < ns; ++i) {
            for (const char* e : expected) {
                if (strncmp(s[i].text, e, sizeof(s[i].text)) == 0) {
                    ++found;
                    lpos += s[i].pos[0] + s[i].pos[1];
                    lcol += (double)s[i].color;
                    lmeta += (double)s[i].font * 1000.0 + (double)s[i].justify * 100.0 + (double)s[i].size;
                    lorder = chainStr(lorder, s[i]);
                }
            }
        }
        char lbuf[200];
        snprintf(lbuf, sizeof(lbuf), "found=%d lpos=%.9f lcol=%.0f lmeta=%.6f lorder=0x%016llx",
                 found, lpos, lcol, lmeta, (unsigned long long)lorder);
        MESSAGE("perf axis labels: " << lbuf);
        CHECK(found == 12);   // 6 labels x (shadow + main)
        // GOLDEN(performance) — axis-label triple identity
        CHECK(lpos == doctest::Approx(20.981319189).epsilon(1e-9));
        CHECK(lcol == doctest::Approx(42837166920.0).epsilon(1e-12));
        CHECK(lmeta == doctest::Approx(12000.18).epsilon(1e-9));
        // GOLDEN(performance) — axis-label emission order
        CHECK(lorder == 0x647420719db70d49ULL);

        // The six grid lines are the only quads with the exact grid thickness
        // (0.001 * scale); the trace polylines are 0.002 * scale thick.
        int gridCount = 0; double gpos = 0, gcol = 0;
        uint64_t gorder = 0;   // ORDER-SENSITIVE: grid lines chained in emission order
        for (int i = 0; i < nq; ++i) {
            float h = std::fabs(q[i].pos[1][1] - q[i].pos[0][1]);
            float w = std::fabs(q[i].pos[2][0] - q[i].pos[0][0]);
            if (std::fabs(h - 0.001f) < 1e-5f && w > 0.05f) {
                ++gridCount;
                for (int c = 0; c < 4; ++c) gpos += q[i].pos[c][0] + q[i].pos[c][1];
                gcol += (double)q[i].color;
                gorder = chainQuad(gorder, q[i]);
            }
        }
        char gbuf[160];
        snprintf(gbuf, sizeof(gbuf), "grid=%d gpos=%.9f gcol=%.0f gorder=0x%016llx",
                 gridCount, gpos, gcol, (unsigned long long)gorder);
        MESSAGE("perf grid lines: " << gbuf);
        CHECK(gridCount == 6);
        // GOLDEN(performance) — grid-line geometry
        CHECK(gpos == doctest::Approx(44.216040015).epsilon(1e-9));
        CHECK(gcol == doctest::Approx(25708616280.0).epsilon(1e-12));
        // GOLDEN(performance) — grid-line emission order
        CHECK(gorder == 0xc391d5f9f572db34ULL);
    }

    // =========================================================================
    // Session Charts HUD (all four charts, fixed race data)
    // =========================================================================
    {
        loadPhase({ "SessionChartsHud" });
        host.sessionChartsSetCharts(15);      // Lap | Trace | Gap | Pace
        (void)probe.fingerprint();
        Fp f = probe.fingerprint();
        Fp f2 = probe.fingerprint();
        MESSAGE("charts:     " << fpStr(f));
        CHECK(fpStr(f2) == fpStr(f));
        // GOLDEN(charts)
        checkAgainst(f, { 54, 66,
                          295.355640173, 230722193649.0, 0.0,
                          97.364435256, 235651418535.0, 70801.06, 37242547186.0,
                          0xeb4b86e9782e2853ULL, 0xf648732bd0d2f7e6ULL });
    }

    // --- Baseline again: phases must leave nothing behind -----------------------
    loadPhase({});
    (void)probe.fingerprint();
    Fp tail = probe.fingerprint();
    MESSAGE("tail:       " << fpStr(tail));
    CHECK(tail.nq == base.nq);
    CHECK(tail.ns == base.ns);

    host.shutdown();
}
