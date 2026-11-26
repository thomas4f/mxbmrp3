// ============================================================================
// tests/integration/tests/clip_marks_test.cpp
// CLIPPING (core/render_clip.h, core/hud_manager_clip.cpp) on the frames the
// plugin hands its renderers: the game's (the engine draws it; Direct GL is
// off in the test build) and the companion window's (always ours).
//
// What it pins, each on both surfaces at once (the one edge rule,
// hud/clip_marks.h, as each HUD hands it its content):
//   * a settings list mid-glide: every row and card crossing the list's frame
//     (the view and the cards' pads) is cut at that ONE frame on the
//     companion, drawn there whole and opaque, cards included; the game's
//     frame carries no cut, and those rows are faded there; nothing wholly
//     past the frame is drawn;
//   * a list without cards: its frame is the view itself;
//   * the gear roll mid-shift: the companion's frame rolls each digit the
//     height of the gear's row, fading toward its edges, each cut at the row;
//     the game's has them faded by how far they are out; settled, both are
//     the one digit and no cut;
//   * a Standings slide: the rows cut at their area on the companion only.
// The Dash, on by default, is switched off here: its strip has its own case
// in dash_test.cpp.
// The cut itself (vertices, pixels) is pinned by the unit tests named in
// render_clip.h.
// ============================================================================
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>
#include <thread>
#include <vector>

namespace {
constexpr int ACHIEVEMENTS = 2;   // ScrollView::ACHIEVEMENTS

unsigned alphaOf(unsigned color) { return (color >> 24) & 0xFFu; }
}  // namespace

TEST_CASE("clip: a settings list mid-glide is cut on the companion, faded in game") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.startup("Z:\\tmp\\mxbmrp3-tests\\clip_marks_list\\") >= 0);
    REQUIRE_MESSAGE(host.hasFrameClips(), "clip test hooks not exported (test build?)");
    REQUIRE(host.hasMotion());
    REQUIRE(host.hasStringRows());
    // The menu draws on the active surface only: the companion's, here. Its
    // own fade-in settles first, then the list glides.
    const long long t0 = 1000000000ll;
    // A theme with card art, so the list's groups sit on cards.
    host.installTheme("clip", /*inset=*/1.0f, /*cardBorder=*/1.0f, /*titleBand=*/1, /*card=*/1);
    host.setMotion(2);
    host.setMotionNowUs(t0);
    host.companionWindow(true);
    REQUIRE(host.setHudVisibleBoth("dash_hud", false));
    host.forceActiveSurface(1);
    host.showSettings(true);
    REQUIRE(host.openSettingsTab("Achievements"));
    for (int i = 1; i <= 10; ++i) { host.setMotionNowUs(t0 + i * 100000ll); host.draw(); }
    const long long t1 = t0 + 1000000ll;

    // Halfway through a glide one step down: rows hang across both edges.
    host.stepScroll(ACHIEVEMENTS, 1);
    host.setMotionNowUs(t1 + 90000ll);
    host.draw();
    host.draw();
    const PluginHost::ScrollView v = host.settingsScroll(ACHIEVEMENTS);
    REQUIRE(v.laidOut);
    INFO("shown " << v.shown << " target " << v.target);
    REQUIRE(v.shown > 0.0f);
    REQUIRE(v.shown < v.target);

    // The game's frame never carries a cut; the companion's cuts the list.
    CHECK(host.frameClips(false).empty());
    // ONE frame for the whole list: every row and card crossing it is cut at
    // the same line, so a row and its card leave together. In a carded list
    // it is the view and the cards' pads around it, the bare section's box.
    // A card (its nine slices, no strings) is drawn whole and cut there.
    const auto cuts = host.frameClips(true);
    const auto quadYs = host.frameQuadYs(true);
    REQUIRE(cuts.size() >= 1);
    int cardCuts = 0, cardsPastFrame = 0, rowCuts = 0;
    for (size_t i = 0; i < cuts.size(); ++i) {
        const PluginHost::FrameClip& k = cuts[i];
        if (i > 0) CHECK(k.q0 >= cuts[i - 1].q1);   // never overlapping
        CHECK(k.b - k.t > v.h);   // the frame reaches past the view, by the pads
        CHECK(k.t == doctest::Approx(cuts[0].t));
        CHECK(k.b == doctest::Approx(cuts[0].b));
        if (k.q1 - k.q0 == 9u && k.s1 == k.s0) {
            ++cardCuts;
            float lo = 1e9f, hi = -1e9f;
            for (unsigned q = k.q0; q < k.q1 && q < quadYs.size(); ++q) {
                lo = std::min(lo, quadYs[q].first);
                hi = std::max(hi, quadYs[q].second);
            }
            if (lo < k.t - 1e-4f || hi > k.b + 1e-4f) ++cardsPastFrame;
        } else {
            ++rowCuts;
        }
    }
    CHECK(cardCuts >= 1);
    CHECK(rowCuts >= 1);
    // Mid-glide some card goes on past the frame, drawn whole and cut there.
    CHECK(cardsPastFrame >= 1);
    const PluginHost::FrameClip& frame = cuts[0];

    // What the engine would draw is the HUD's own arrays; line each cut row's
    // strings up with the companion's (which may hold drop shadows between).
    const auto rows = host.hudStringRows("settings_hud");
    const auto comp = host.frameStrings(true);
    int cutNotFaded = 0;
    for (const auto& k : cuts) {
        if (k.s1 <= k.s0) continue;
        for (unsigned i = k.s0; i < k.s1; ++i) {
            const PluginHost::FrameString& s = comp[i];
            if (s.text.empty() || alphaOf(s.color) < 255u) continue;
            for (size_t l = 0; l < rows.size(); ++l) {
                if (rows[l].text != s.text) continue;
                const unsigned engine =
                    alphaOf(static_cast<unsigned>(host.stringColor("settings_hud", static_cast<int>(l))));
                if (engine < 255u) ++cutNotFaded;   // opaque here, for the cut; faded in game
                break;
            }
        }
    }
    CHECK(cutNotFaded >= 1);
    // A cut row reaches into the frame: drawn whole, so a line of a two-line
    // row may lie past it, never the whole row (that one is not drawn).
    int outside = 0;
    for (const auto& k : cuts)
        for (unsigned i = k.s0; i < k.s1 && i < comp.size(); ++i) {
            const PluginHost::FrameString& s = comp[i];
            if (s.text.empty()) continue;
            if (s.y >= frame.b + 2.0f * s.size || s.y + s.size <= frame.t - 2.0f * s.size) ++outside;
        }
    CHECK(outside == 0);

    // Settled: the companion still cuts the list (a card goes on past the
    // frame, a row may hang across its foot); the game's frame never is.
    host.setMotionNowUs(t1 + 1000000ll);
    host.draw();
    host.draw();
    CHECK(host.frameClips(true).size() >= 1);
    CHECK(host.frameClips(false).empty());
    host.setMotion(0);
    host.setMotionNowUs(-1);
}

TEST_CASE("clip: a list without cards is cut at the view itself, never past it") {
    // No card art (the test build's default theme): no pads to reach into, so
    // nothing of the list may cross into the heading above it or the footer.
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.startup("Z:\\tmp\\mxbmrp3-tests\\clip_marks_flat\\") >= 0);
    REQUIRE(host.hasFrameClips());
    REQUIRE(host.hasMotion());
    const long long t0 = 1000000000ll;
    host.setMotion(2);
    host.setMotionNowUs(t0);
    host.companionWindow(true);
    REQUIRE(host.setHudVisibleBoth("dash_hud", false));
    host.forceActiveSurface(1);
    host.showSettings(true);
    REQUIRE(host.openSettingsTab("Achievements"));
    for (int i = 1; i <= 10; ++i) { host.setMotionNowUs(t0 + i * 100000ll); host.draw(); }
    host.stepScroll(ACHIEVEMENTS, 1);
    host.setMotionNowUs(t0 + 1000000ll + 90000ll);
    host.draw();
    host.draw();
    const PluginHost::ScrollView v = host.settingsScroll(ACHIEVEMENTS);
    REQUIRE(v.shown > 0.0f);
    REQUIRE(v.shown < v.target);
    const auto cuts = host.frameClips(true);
    REQUIRE(cuts.size() >= 1);
    for (const auto& k : cuts) CHECK(k.b - k.t == doctest::Approx(v.h).epsilon(0.001));
    host.setMotion(0);
    host.setMotionNowUs(-1);
}

TEST_CASE("clip: the gear roll is a cut, edge-faded drum on the companion, faded by how far it is out in game") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.startup("Z:\\tmp\\mxbmrp3-tests\\clip_marks_gear\\") >= 0);
    host.setMasterEnabled(PluginHost::Master::Widgets, true);   // the Gear widget, off for a new install
    REQUIRE(host.hasFrameClips());
    REQUIRE(host.hasMotion());
    host.eventInit("RollTrack", "Player");
    host.companionWindow(true);
    REQUIRE(host.setHudVisibleBoth("dash_hud", false));
    host.setMotion(1);
    host.setMotionNowUs(1000000);
    host.drawWithState(0);
    host.telemetry(10.0f, 1);
    host.drawWithState(0);
    host.drawWithState(0);
    CHECK(host.frameClips(true).empty());   // settled: one digit, no cut

    host.telemetry(10.0f, 2);
    host.drawWithState(0);
    host.setMotionNowUs(1000000 + 100000);   // halfway
    host.drawWithState(0);
    host.drawWithState(0);

    CHECK(host.frameClips(false).empty());
    // One cut per digit (each its own piece at the edge), the same box.
    const auto cuts = host.frameClips(true);
    REQUIRE(cuts.size() >= 2);
    const PluginHost::FrameClip& c = cuts[0];
    for (const auto& k : cuts) {
        CHECK(k.t == doctest::Approx(c.t));
        CHECK(k.b == doctest::Approx(c.b));
    }
    const auto comp = host.frameStrings(true);
    std::vector<PluginHost::FrameString> drum;
    for (const auto& k : cuts)
        for (unsigned i = k.s0; i < k.s1 && i < comp.size(); ++i)
            if (comp[i].text == "1" || comp[i].text == "2") drum.push_back(comp[i]);
    // Both digits (and their drop shadows, if on, each inserted in front of
    // its string), one box apart: up a gear the old one drops out of the
    // gear's row as the new one comes down into it, halfway each, and each
    // halfway faded (it fades as it nears the edge, digit_roll.h cutAt).
    REQUIRE(drum.size() >= 2);
    const float box = c.b - c.t;
    float oldY = 0.0f, newY = 0.0f, size = 0.0f;
    unsigned oldA = 0, newA = 0;
    for (const auto& s : drum) {   // the last of each text is the digit itself
        size = s.size;
        if (s.text == "1") { oldY = s.y; oldA = alphaOf(s.color); }
        else { newY = s.y; newA = alphaOf(s.color); }
    }
    CHECK(oldA > 0u);
    CHECK(oldA < 255u);
    CHECK(newA > 0u);
    CHECK(newA < 255u);
    CHECK(oldY - newY == doctest::Approx(box).epsilon(0.01));
    CHECK(oldY > newY);

    // The game: the same two digits, each fainter than ours -- halfway, each
    // half its box out, all but gone.
    int gearDigits = 0;
    for (const auto& s : host.frameStrings(false)) {
        if (std::abs(s.size - size) > 1e-5f || (s.text != "1" && s.text != "2")) continue;
        ++gearDigits;
        CHECK(alphaOf(s.color) < (s.text == "1" ? oldA : newA));
    }
    CHECK(gearDigits >= 2);

    // Settled again: one digit on both, no cut.
    host.setMotionNowUs(1000000 + 400000);
    host.drawWithState(0);
    host.drawWithState(0);
    CHECK(host.frameClips(true).empty());
    host.setMotion(0);
    host.setMotionNowUs(-1);
}

TEST_CASE("clip: a Standings row sliding into place is cut at the rows on the companion only") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.startup("Z:\\tmp\\mxbmrp3-tests\\clip_marks_standings\\") >= 0);
    REQUIRE(host.hasFrameClips());
    host.companionWindow(true);
    REQUIRE(host.setHudVisibleBoth("dash_hud", false));
    host.session(/*session=*/1, /*numLaps=*/10);
    for (int n : {10, 22, 7, 3}) {
        char nm[8];
        snprintf(nm, sizeof(nm), "R%d", n);
        host.addEntry(n, nm);
    }
    const auto order = [&](int a, int b) {
        host.classify(1, 60000, {
            { .num = a,  .best = 92000, .laps = 2 },
            { .num = b,  .best = 93000, .laps = 2, .gap = 1200 },
            { .num = 7,  .best = 94000, .laps = 2, .gap = 2400 },
            { .num = 3,  .best = 95000, .laps = 2, .gap = 3600 },
        });
    };
    host.stSetVisible(true);
    host.setMotion(2);   // rows slide only while Motion is on (the test build starts it Off)
    order(10, 22);
    host.draw();
    host.draw();
    CHECK(host.frameClips(true).empty());   // settled: nothing to cut

    // A pass: both rows slide (Standings' own clock, 500 ms by default).
    order(22, 10);
    host.draw();
    host.draw();
    CHECK(host.frameClips(false).empty());
    const auto cuts = host.frameClips(true);
    REQUIRE(cuts.size() == 1);
    CHECK(cuts[0].s1 > cuts[0].s0);
    CHECK(cuts[0].b > cuts[0].t);

    // Over: no cut.
    std::this_thread::sleep_for(std::chrono::milliseconds(700));
    host.draw();
    host.draw();
    CHECK(host.frameClips(true).empty());
    host.setMotion(0);
}
