// ============================================================================
// tests/integration/tests/render_probe_test.cpp
// The render probe emits the primitive it says it emits.
//
// WHY THIS FILE EXISTS. The probe ([Advanced] renderProbeQuads / renderProbeType /
// renderProbeSprite) appends synthetic quads to the frame so the ENGINE's cost to
// draw our primitives can be measured differentially -- a cost that is invisible to
// every in-plugin timer by construction, since it is spent after Draw() returns.
// There is therefore nothing here to assert about speed, and that is precisely the
// problem: the probe's output is only ever read as a frame-time difference between
// two runs, so a probe that emits the WRONG primitive does not fail, it produces a
// number. A wrong number that looks exactly like a right one.
//
// THE TRAP THAT MOTIVATED IT. Sprite 0 does not mean "some sprite", it means
// "untextured -- fill with m_ulColor". So an out-of-range renderProbeSprite that
// fell through to 0 would measure the FLAT FILL path while the report's block said
// "sprite quad (textured)", and the conclusion drawn from it -- that texturing is
// free -- would be the opposite of the truth. The fallback is to cycling instead,
// and that is what the third case here holds.
//
// The counts are asserted as DELTAS against a probe-off frame rather than absolutes:
// the baseline is whatever HUDs happen to be on, which is not this file's business.
// See tools/probetheme/README.md for the run matrix these knobs serve.
// Self-contained doctest; see run_tests.sh.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

namespace {

constexpr int RACE = 6;   // PiBoSo Race1 session enum
constexpr int kProbeN = 250;
constexpr int kSprites = 40;   // synthetic sprite table size

void openSession(PluginHost& host) {
    host.showAllHuds(true);
    // A sprite table. The real one comes from DrawInit reading files on disk, and
    // this harness stages no assets -- so without this the plugin has ZERO sprites
    // and the type-1 branch legitimately degenerates to the untextured path, making
    // every assertion about pinning pass vacuously.
    host.installSpriteTable(kSprites);
    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(RACE, /*numLaps=*/5, /*lengthMs=*/0);
    host.addEntry(10, "Alice");
    host.classify(RACE, 100000, { { .num = 10, .best = 90000, .laps = 1, .gap = 0 } });
    host.draw();
}

}  // namespace

TEST_CASE("render probe: off by default, and N quads when asked") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\render_probe_count\\");
    openSession(host);

    const int baseline = host.lastGameQuads();
    REQUIRE_MESSAGE(baseline > 0, "no HUD frame to measure against");

    // Type 0: solid fill, sprite 0, the untextured path.
    host.setRenderProbe(kProbeN, /*type=*/0, /*fullscreen=*/false, /*sprite=*/0);
    host.draw();
    CHECK_MESSAGE(host.lastGameQuads() == baseline + kProbeN,
                  "the probe's quads must reach the game frame -- the whole method is "
                  "that the engine draws them, and the report's quad count is the only "
                  "confirmation a run's INI edit took effect");

    // ...and back off cleanly, so a probe left set cannot silently taint later runs.
    host.setRenderProbe(0, 0, false, 0);
    host.draw();
    CHECK(host.lastGameQuads() == baseline);

    host.shutdown();
}

TEST_CASE("render probe: type 0 is untextured, type 1 is not") {
    // The distinction the whole probe rests on. Type 0 quads carry sprite 0 (the
    // engine fills them with m_ulColor and never touches a texture); type 1 quads
    // carry a real sprite. If these two ever emitted the same thing, R2-R1 in the run
    // matrix -- the cost of texturing -- would measure zero and read as an answer.
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\render_probe_type\\");
    openSession(host);

    const int sprites = host.registeredSpriteCount();
    REQUIRE(sprites == kSprites);

    int lo = 0, hi = 0, flat = 0;
    host.frameSpriteSpan(lo, hi, flat);
    const int flatBaseline = flat;

    host.setRenderProbe(kProbeN, /*type=*/0, false, 0);
    host.draw();
    host.frameSpriteSpan(lo, hi, flat);
    CHECK_MESSAGE(flat == flatBaseline + kProbeN,
                  "type 0 must add exactly N UNTEXTURED quads");

    host.setRenderProbe(kProbeN, /*type=*/1, false, /*sprite=*/0);
    host.draw();
    host.frameSpriteSpan(lo, hi, flat);
    CHECK_MESSAGE(flat == flatBaseline,
                  "type 1 must add no untextured quads -- a textured run that quietly "
                  "drew flat fill would measure the wrong path and still produce a number");

    host.shutdown();
}

TEST_CASE("render probe: a pinned sprite is the only sprite, a bad one falls back to cycling") {
    // THE CASE THAT MATTERS. Pinning exists to separate texture SAMPLING from texture
    // SWITCHING: cycling charges a switch per quad, pinning charges none, and the
    // difference between the two runs is the switch cost. That only holds if pinning
    // really pins -- and if a typo does NOT quietly become sprite 0.
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\render_probe_pin\\");
    openSession(host);

    const int sprites = host.registeredSpriteCount();
    REQUIRE(sprites == kSprites);

    int lo = 0, hi = 0, flat = 0;
    host.frameSpriteSpan(lo, hi, flat);
    const int flatBaseline = flat;

    // Cycling: the probe alone spans the whole registered range.
    host.setRenderProbe(sprites * 2, /*type=*/1, false, /*sprite=*/0);
    host.draw();
    host.frameSpriteSpan(lo, hi, flat);
    CHECK_MESSAGE(hi == sprites,
                  "cycling must reach the LAST registered sprite -- stopping short "
                  "understates the switch cost it exists to provoke");

    // Pinned: every probe quad on one sprite. The pin is the highest index, so it is
    // distinguishable from whatever the HUDs themselves happen to draw.
    const int pin = sprites;
    host.setRenderProbe(kProbeN, /*type=*/1, false, pin);
    host.draw();
    host.frameSpriteSpan(lo, hi, flat);
    CHECK(hi == pin);
    CHECK_MESSAGE(flat == flatBaseline,
                  "a pinned run must add no untextured quads");

    // Out of range: falls back to CYCLING, never to sprite 0.
    host.setRenderProbe(kProbeN, /*type=*/1, false, /*sprite=*/sprites + 5000);
    host.draw();
    host.frameSpriteSpan(lo, hi, flat);
    CHECK_MESSAGE(flat == flatBaseline,
                  "an out-of-range pin fell through to sprite 0 -- the run would have "
                  "measured flat fill while reporting itself as textured, which is a "
                  "wrong answer in the shape of a right one");
    CHECK_MESSAGE(hi == sprites, "the fallback must be the cycling behaviour");

    host.shutdown();
}

TEST_CASE("render probe sweep: gives the user's probe settings back") {
    // THE ONE THING THE SWEEP MUST NOT GET WRONG. It drives the probe settings
    // itself, stepping through the whole matrix -- so if it fails to put them back,
    // the plugin is left permanently emitting up to 2000 synthetic primitives a
    // frame. That does not crash and does not warn: it looks like the plugin got
    // slow. And the abort path is the one that matters most, because it is the path
    // taken by a user who pressed the key, watched the frame rate collapse, and
    // pressed it again.
    //
    // The stepping itself is wall-clock paced (~2.8s per step, 19 steps), so this
    // asserts the bracket rather than sitting through a minute of sweeping.
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\probe_sweep\\");
    openSession(host);

    // A distinctive configuration, so "restored" cannot be confused with "reset".
    host.setRenderProbe(300, /*type=*/1, /*fullscreen=*/true, /*sprite=*/5);
    host.setRenderProbeTextChars(37);
    int n = 0, type = 0, fs = 0, sprite = 0;
    host.getRenderProbe(n, type, fs, sprite);
    REQUIRE_MESSAGE(n == 300, "MXBMRP3_Test_GetRenderProbe absent -- rebuild the test DLL");

    host.probeSweepStart();
    CHECK(host.probeSweepRunning());
    host.draw();
    host.getRenderProbe(n, type, fs, sprite);
    CHECK_MESSAGE(!(n == 300 && type == 1 && fs == 1 && sprite == 5),
                  "the sweep is running and has not taken over the probe settings -- "
                  "it would be measuring the user's configuration, not its own steps");

    host.probeSweepAbort();
    CHECK_FALSE(host.probeSweepRunning());
    host.getRenderProbe(n, type, fs, sprite);
    CHECK(n == 300);
    CHECK(type == 1);
    CHECK(fs == 1);
    CHECK_MESSAGE(host.getRenderProbeTextChars() == 37,
                  "the sweep's text-length steps left the string length changed");
    CHECK_MESSAGE(sprite == 5,
                  "an aborted sweep left the probe settings changed -- the plugin "
                  "keeps emitting synthetic primitives, which reads as 'it got slow'");

    // Starting twice must not stack, or the second start would capture the FIRST
    // sweep's step settings as the user's and restore those at the end.
    host.probeSweepStart();
    host.probeSweepStart();
    host.probeSweepAbort();
    host.getRenderProbe(n, type, fs, sprite);
    CHECK(n == 300);
    CHECK(sprite == 5);

    host.shutdown();
}

TEST_CASE("probe sweep report: the alpha-0 and zero-area verdicts difference "
          "against the opaque fill, not each other") {
    // THE BUG THIS PINS. The derived block's opaque/alpha-0 selector matched
    // every non-fullscreen type-0 row at N=2000 — which includes the DEGENERATE
    // (chars==1) row, and that row iterates last, so `opaque` ended up holding
    // the zero-area quad's cost. The ALPHA-0 percentage then compared against
    // the wrong baseline, and ZERO-AREA computed degen/degen ≈ 100%, printing
    // "degenerate quads are charged in full" UNCONDITIONALLY — the sweep whose
    // whole purpose is replacing hand arithmetic, doing the arithmetic wrong.
    // Costs are injected (no wall-clock sweep), so the percentages are exact.
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\probe_sweep_report\\");
    openSession(host);

    // Transparent at 10% and degenerate at 5% of an opaque quad: the engine
    // short-circuits both. Under the bug, ALPHA 0 reads 200% (0.10/0.05) and
    // ZERO-AREA reads 100%, so both verdicts flip to "charged in full".
    std::string rep = host.probeSweepReport(1.0, 0.10, 0.05);
    REQUIRE_MESSAGE(!rep.empty(),
                    "MXBMRP3_Test_ProbeSweepReport absent -- rebuild the test DLL");
    CHECK_MESSAGE(rep.find("(10% of an opaque quad -- the engine short-circuits them)")
                      != std::string::npos,
                  "ALPHA-0 verdict wrong or mis-baselined:\n" << rep);
    CHECK_MESSAGE(rep.find("(5% of a drawn quad -- the engine rejects them early;")
                      != std::string::npos,
                  "ZERO-AREA verdict wrong or mis-baselined:\n" << rep);

    // And the other branch of each line, so a hardcoded verdict cannot pass.
    rep = host.probeSweepReport(1.0, 0.90, 0.95);
    CHECK(rep.find("(90% of an opaque quad -- transparent quads are charged in full)")
              != std::string::npos);
    CHECK(rep.find("(95% of a drawn quad -- degenerate quads are charged in full;")
              != std::string::npos);

    host.shutdown();
}

TEST_CASE("invisible background: a zero-opacity panel emits no background quads") {
    // MEASURED, then acted on. The probe put a fully transparent quad at 0.969us
    // against an opaque one's 0.973 -- the engine bills for SUBMISSION, not for
    // pixels, so an alpha-0 quad costs exactly what a visible one does. A themed
    // panel at zero opacity was paying for its whole background chain -- frame slices,
    // title band, content cards, ~27 quads -- every frame, for nothing.
    //
    // Asserted on the frame's quad count with one panel's opacity moved, so it needs
    // no knowledge of how many quads a background happens to be.
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\invisible_bg\\");
    host.showAllHuds(true);
    openSession(host);

    host.draw();
    const int opaque = host.lastGameQuads();
    REQUIRE(opaque > 0);

    REQUIRE_MESSAGE(host.setHudOpacity("standings_hud", 0.0f),
                    "MXBMRP3_Test_SetHudOpacity absent -- rebuild the test DLL");
    host.draw();
    host.draw();
    const int clear = host.lastGameQuads();

    CHECK_MESSAGE(clear < opaque,
                  "a zero-opacity panel still emitted its background (" << clear
                  << " vs " << opaque << " quads). Every one is charged in full by the "
                  << "engine and none of them can be seen");

    // ...and it comes back. A skip that were sticky would leave the panel permanently
    // background-less once anyone touched the slider.
    REQUIRE(host.setHudOpacity("standings_hud", 0.85f));
    host.draw();
    host.draw();
    CHECK(host.lastGameQuads() == opaque);

    host.shutdown();
}

TEST_CASE("invisible background: dragging a zero-opacity panel does not corrupt its content") {
    // THE TRAP IN THE SKIP. m_bgQuadFirst is still recorded when the background is
    // skipped, and it then points at the panel's first CONTENT quad. The layout fast
    // path (updateBackgroundQuadPosition) rewrites that index on every drag and scale
    // -- so without the count guard, moving a zero-opacity panel could stretch one of
    // its rows or icons across the whole panel. Silent, and only on the panels a user
    // made transparent on purpose.
    //
    // HONEST LIMIT OF THIS CASE: it does NOT currently discriminate. Removing the
    // guard in updateBackgroundQuadPosition leaves it passing -- in this headless
    // configuration the panel evidently does not reach the flat rewrite with a
    // non-empty quad list, so the hazard is real by inspection but unreached here.
    // The guard stays because the reasoning is sound and the failure would be
    // invisible; this case stays because "repositioning a transparent panel changes
    // no geometry" is worth holding regardless. Do not read a pass here as proof the
    // guard works -- if you make this case reach the flat path, say so here.
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\invisible_bg_drag\\");
    host.showAllHuds(true);
    openSession(host);

    REQUIRE(host.setHudOpacity("standings_hud", 0.0f));
    host.draw();
    host.draw();
    // The largest quad the panel emits. A reposition is a pure TRANSLATION, so every
    // area is invariant across it -- and the corruption is geometric, not countable:
    // the quad count would be identical either way, with one content quad simply
    // stretched to the panel rect. Only a size measure can see that.
    const int before = host.hudMaxQuadArea("standings_hud");
    REQUIRE_MESSAGE(before > 0,
                    "MXBMRP3_Test_HudMaxQuadArea absent -- rebuild the test DLL");

    // A layout-only change: same data, new position, so the fast path runs.
    host.setHudOffset("standings_hud", 0.10f, 0.10f);
    host.draw();

    CHECK_MESSAGE(host.hudMaxQuadArea("standings_hud") == before,
                  "a quad grew when a zero-opacity panel was repositioned ("
                  << before << " -> " << host.hudMaxQuadArea("standings_hud")
                  << "): the layout fast path wrote the panel rect into a span that "
                  << "was never emitted, so it landed on the first CONTENT quad");

    host.shutdown();
}
