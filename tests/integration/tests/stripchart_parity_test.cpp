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
//
// GOLDEN REFRESH, 2026-08-25 (seventh) -- THE ROW-PITCH DEFAULT.
// POSITIONS ONLY, on all four HUDs. Every count, every colour sum, the sprite
// sums, the font/justify/size metadata sums and every text hash are
// BYTE-IDENTICAL; only the position sums moved, and every one of them moved
// DOWN. That combination has exactly one cause and it is not a rendering
// change: the same primitives, in the same order, drawn with the same fonts at
// the same sizes in the same colours, laid out on a shorter row.
//
// LayoutMetrics::lineHeightRatio went 1.17335 -> 1.1 (a sixth of a row of air
// between text rows became a tenth; see the comment on the field, and the
// settings v9 migration that moves an existing INI onto it). cellH is half the
// row, so the vertical snap lattice moved with it, and every panel that stacks
// rows follows.
//
//   telemetry   qpos 597.617209315 -> 583.277590513   spos 33.590229988 -> 32.798418045
//   rumble      qpos  32.127985954 ->  31.413556814   spos 31.982259870 -> 31.285067677
//   charts      qpos 285.187308908 -> 279.215777814   spos 94.109438121 -> 91.801324189
//   perf        gpos  43.309529305 ->  41.866000891   lpos 20.543664455 -> 19.819700003
//
// Session Charts drifts hardest per primitive for the same reason recorded in
// the fifth refresh below -- its geometry is lineHeightSmall-derived.
//
// The order hashes moved because they chain positions, not because anything was
// reordered: the counts pin that, and smeta pins that no font or size moved
// with the pitch.
//
// GOLDEN REFRESH, 2026-08-22 (sixth) -- THE DROP SHADOW'S ALPHA.
// RUMBLE ONLY, and only its string COLOUR sum and string order hash: the quad
// fingerprint, both position sums, the metadata sum, the text hash and every
// count are byte-identical, and the other three HUDs did not move at all. That
// combination has one cause: the same strings, in the same places, one of them a
// different colour.
//
// The colour is a SHADOW's. collectRenderData used to write the configured shadow
// colour verbatim behind every shadowed string; it now modulates it by the
// string's own alpha, so a translucent string gets a translucent shadow instead of
// a solid one behind half-visible text. (That is what let RadarHud drop its
// hand-rolled outline -- it fades rider labels by proximity.) Rumble is the HUD in
// this file that draws translucent TEXT: its legend labels are makeColor(..., 230),
// so their shadows lose ~7% alpha. Nothing moved and nothing was added.
//
// GOLDEN REFRESH, 2026-08-16 (fifth) -- THE SHIPPED DEFAULTS RETUNE.
// EVERY position golden and EVERY order hash moved; NOTHING else did. The quad
// and string COUNTS, all four colour sums, the font/justify/size metadata sum
// and the text hash are byte-identical to the fourth refresh, in all four HUDs
// and in both Performance subsets. That combination has exactly one cause: the
// same elements, in the same order, drawn somewhere else -- a translation, not
// a change of content. (The order hashes move with it because chainQuad and
// chainStr chain POSITIONS as well as identity, by design: an element that
// slides is a change worth failing on.)
//
// What slid: the unthemed box-model defaults were retuned to "the outer ring,
// the seam, and nothing else" -- panelPadding 2 -> 1 cell, titlePadding and
// contentMargin to 0, panelGap 0 -> 1, buttonPadding to 0.5 1 (LayoutMetrics,
// and see its comment for the reasoning). All four of these HUDs compose
// through planPanel and are drawn unthemed here, so each one's chrome changed
// by construction. Per-coordinate the shift is ~0.0018 normalized for quads and
// ~0.0026 for strings, i.e. fractions of a cell -- consistent with a padding
// term moving by a cell and nothing else moving at all.
//
// These were captured AFTER the two geometry fixes that landed with them (the
// border no longer spent where no art is drawn; titleBandBoxHeight matching
// resolvePanelSpec's caption cell), deliberately: neither one can move an
// unthemed plan panel, so a golden taken before them would have had to be taken
// again, and a golden taken twice is a golden nobody trusts.
//
// GOLDEN REFRESH, 2026-08-12 (fourth) -- THE RE-PIN AFTER THE BOX-MODEL PORT.
// This file was removed red, with an unresolved
// question attached: scrapping `[content] gap-y` had taken the unthemed
// sectionGapY() from one cell to zero, and the spec said to decide "a floor of
// one cell, or zero as intended" before re-pinning. DECIDED: the gap came back
// as one term shared by all three boundaries -- `[panel] gap`, default
// LayoutMetrics::sectionGap = 1.0f cells (see contentCardTop's note in
// nine_slice.h) -- so the unthemed advance is a cell again by design, not by
// accident. Every golden below is captured fresh from the plan-model renderer
// (all four HUDs now compose through planPanel); the old goldens describe a
// renderer that no longer exists, and their history is kept above because the
// refresh discipline -- explain the delta before re-blessing -- is the point.
//
// GOLDEN REFRESH, 2026-08-03 (third). Every position golden moved by ~1.5e-6 per
// coordinate -- 0.0016px at 1080p -- and CHARTS did not move at all. That is the
// signature of stating each HUD's DEFAULT POSITION in grid cells instead of as a
// frozen decimal: 0.01173f was 1 cellH rounded to five places, and cellsY(1) is the
// cell itself. Session Charts is the one HUD whose default was already exact.
//
// Sub-pixel, but refreshed rather than absorbed into the tolerance: the goldens are
// worth more pinning the exact numbers than tracking a moving one, and "did anything
// move" is the question they exist to answer.
//
// GOLDEN REFRESH, 2026-08-03 (second). Every STRING position golden moved and no
// QUAD position golden did -- spos/lpos changed, qpos/gpos did not, which is the
// signature of the change that caused it: BaseHud::rowCenterOffset() now drops a
// glyph box to sit centred in its row instead of flush with the row's top. Strings
// move, quads do not. The drift measured 0.00065 per coordinate, which is exactly
// that offset at the small tier applied to Y with X untouched.
//
// Refreshed rather than loosened: the goldens did their job here. They are what said
// "text moved", and the accompanying standings_layout_test failure is what said the
// drag fast path had NOT moved with it -- the actual bug, now fixed by routing
// repositioning through positionString() (see check_hud_helpers.sh rule 8).
//
// GOLDEN REFRESH, 2026-08-03. The Performance axis-label / grid-line goldens and
// the whole Session Charts fingerprint were re-pinned after f934907 ("Sections: a
// real gap between them"), which changed both HUDs' inter-section advance from
// dims.lineHeightNormal to 2*sectionCardPaddingY() + [content] gap-y.
//
// UNTHEMED -- which is how this test runs -- sectionCardPaddingY() is zero, so that
// advance went from 2 grid cells to 1 and every section below the first moved UP
// by one cell per section. The arithmetic checks out exactly: the grid-line drift
// was 0.140802 against 6 lines x 2 coords x one cell = 0.140800, and the label
// drift 0.0704012 against 6 x 0.070400. Charts moved by the same rule, its larger
// drift scaling with section index because chart k shifts by (k-1) cells.
//
// So this was an intended layout change whose UNTHEMED side effect nobody looked
// at -- the commit was reasoning about themed cards touching. Recorded here rather
// than silently re-pinned, because a golden refresh is exactly the move that hides
// a real regression, and the only thing separating the two is whether someone
// explained the delta first.
//
// HOW LONG IT WAS RED, corrected: this file's first version of this note claimed the
// suite had not run since fa225d3 (where the goldens were last corrected), which was
// wrong -- it was run green at bbe8f83 and again at 4c0ef8c. The red window opens at
// f934907 and is a handful of commits wide, not forty. The reason it still went
// unnoticed is narrower and more useful: f934907 landed mid-batch and the suite was
// not run again before the batch was pushed.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include "ini.h"
#include <cmath>
#include <cstdlib>
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
// ORDER hashes. Positions are chained QUANTISED, not raw and not dropped.
//
// Raw float bits were the original, and they made the hash hostage to the last
// bit of every coordinate: when lineHeightNormal stopped being the literal
// 0.023467f and became 0.02 * 1.17335, every position shifted by ~5e-7 normalized
// -- 0.001px at 1080p -- and all four hashes flipped. A hash that changes when
// nothing observable did cannot tell a REORDER from float noise.
//
// Dropping positions was the first fix and it was WRONG, in a way the sums cannot
// cover: a quad is then identified by colour+sprite alone, and qpos is an
// unweighted (commutative) sum. Two same-coloured, same-sprite bars of different
// heights swapping geometry -- an ordinary bar chart -- moved neither field. That
// is a real regression traded for noise immunity, and the comment here asserted
// the opposite ("moves the position sums only if they are at different places",
// which is exactly what a commutative sum cannot see).
//
// Quantising keeps both. QUANT is 1e-5 normalized = 0.011px at 1080p: ~20x above
// the float drift that motivated this, and far below anything a person can see. So
//   * sub-ULP arithmetic changes cannot move it,
//   * ANY reorder of primitives at different places does,
//   * and it doubles as the PER-ELEMENT position bound the additive sums lack --
//     they pool their tolerance, so one element could absorb the whole budget
//     (~1.6px on telemetry) unnoticed; it cannot move 0.011px without tripping
//     this. The two together are strictly stronger than the raw-bit original.
static constexpr float QUANT = 1e-5f;
static uint64_t chainPos(uint64_t h, float v) {
    return chainU64(h, (uint64_t)(int64_t)llroundf(v / QUANT));
}
static uint64_t chainQuad(uint64_t h, const QuadRow& q) {
    for (int c = 0; c < 4; ++c) {
        h = chainPos(h, q.pos[c][0]);
        h = chainPos(h, q.pos[c][1]);
    }
    h = chainU64(h, (uint64_t)q.color);
    h = chainU64(h, (uint64_t)(unsigned)q.sprite);
    return h;
}
static uint64_t chainStr(uint64_t h, const StrRow& s) {
    h = chainPos(h, s.pos[0]);
    h = chainPos(h, s.pos[1]);
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
// RE-BLESSED once, when the order hashes changed what they chain (see chainQuad).
//
// The POSITION sums were not re-derived from a changed renderer, and that is
// measured rather than assumed. Patching derive() back to the pre-migration
// literal `lineHeightNormal = 0.023467f` reproduces the OLD golden sums exactly --
// bit for bit, all three phases -- so every remaining difference is that one
// constant, spread over coordinates rather than any element moving on its own.
// Session Charts drifts ~130x more than Telemetry only because its geometry is
// lineHeightSmall-derived while Telemetry's is not.
//
// That experiment also found a difference the constant did NOT explain:
// calculateMonospaceTextWidth had been regrouped from numChars * (fontSize *
// ratio) to (numChars * fontSize) * ratio, which rounds differently in float32.
// Restored, and Telemetry's sums are back to their original values -- an
// arithmetic-neutral refactor should be arithmetic-neutral.
struct Golden {
    int nq, ns;
    double qpos, qcol, qspr, spos, scol, smeta, stext;
    uint64_t qorder, sorder;
};

// Largest per-coordinate drift a position sum may absorb, in NORMALIZED units.
//
// 1e-6 is 0.002px at 1080p and 0.004px at 4K -- ~50x tighter than anything a
// person could see, and comfortably above float32 accumulation noise. It replaces
// a RELATIVE epsilon of 1e-9 on a sum of several hundred coordinates, which worked
// out to a tolerance far below float32's own precision: the check could only pass
// while the arithmetic was bit-identical, so any refactor of the layout tripped it
// whether or not a pixel moved.
static constexpr double kPosTolPerCoord = 1e-6;

// |a - b| <= tol, reported with both values so a failure names the drift.
#define CHECK_POS(a, b, coords)                                                   \
    do {                                                                          \
        const double _tol = kPosTolPerCoord * (double)(coords);                   \
        INFO("drift " << ((a) - (b)) << " over " << (coords)                      \
                      << " coords, tolerance " << _tol);                          \
        CHECK(std::fabs((a) - (b)) <= _tol);                                      \
    } while (0)

static void checkAgainst(const Fp& f, const Golden& g) {
    CHECK(f.nq == g.nq);
    CHECK(f.ns == g.ns);
    CHECK_POS(f.qpos, g.qpos, f.nq * 8);
    CHECK(f.qcol == doctest::Approx(g.qcol).epsilon(1e-12));
    CHECK(f.qspr == doctest::Approx(g.qspr).epsilon(1e-12));
    CHECK_POS(f.spos, g.spos, f.ns * 2);
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

    // DROP SHADOW ON, STATED RATHER THAN INHERITED. Every golden below counts
    // shadow strings, because a shadowed string is drawn twice -- and the
    // shipped default flipped to OFF in 1.29 (it is a second draw per glyph),
    // which halved `ns` and every string-derived sum in one step. The goldens
    // are not wrong and were NOT refreshed for it: refreshing is the move that
    // hides a real regression, and the shadow path is worth pinning. So the
    // phase declares the setting instead of riding whatever the default is,
    // and a future default flip cannot reach these numbers again.
    auto loadPhase = [&](const std::set<std::string>& on, KeyOverrides overrides = {}) {
        overrides.insert({ { "Display", "dropShadow" }, "1" });
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
                          583.277590513, 436195095084.0, 0.0,
                          32.798418045, 85684202790.0, 24000.43, 14463422246.0,
                          0xd759b3e88eb214beULL, 0xa2b81cb3aa9c17d3ULL });
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
                          31.413556814, 20551431668.0, 0.0,
                          31.285067677, 84962886643.0, 24400.42, 13744300338.0,
                          0xac6a724b95036ec0ULL, 0xed3e3758697080b1ULL });
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

        // The ms axis is the 480fps FRAME BUDGET (PluginConstants::FRAME_BUDGET_MS =
        // 2.083) and its half, not the old 4.0/2.0 pair that was tuned for a 144fps
        // target -- see PerformanceHud::MAX_PLUGIN_TIME_MS.
        const char* expected[] = { "250 FPS", "125 FPS", "0 FPS", "2.1 ms", "1.0 ms", "0.0 ms" };
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
        CHECK_POS(lpos, 19.819700003, found * 2);
        CHECK(lcol == doctest::Approx(42837166920.0).epsilon(1e-12));
        CHECK(lmeta == doctest::Approx(12000.18).epsilon(1e-12));
        // GOLDEN(performance) — axis-label emission order
        CHECK(lorder == 0x38a3a04e2054d534ULL);   // ms labels read the frame budget

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
        CHECK_POS(gpos, 41.866000891, gridCount * 8);
        CHECK(gcol == doctest::Approx(25708616280.0).epsilon(1e-12));
        // GOLDEN(performance) — grid-line emission order
        CHECK(gorder == 0x92e72a14f4c7626cULL);
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
                          279.215777814, 230722193649.0, 0.0,
                          91.801324189, 235651418535.0, 70801.06, 37242547186.0,
                          0x680697ff6e24a904ULL, 0x870a05bf12f1e77dULL });
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
