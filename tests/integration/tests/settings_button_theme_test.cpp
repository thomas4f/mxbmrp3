// ============================================================================
// tests/integration/tests/settings_button_theme_test.cpp
// EVERY BUTTON IN THE SETTINGS FOOTER IS A THEMED BUTTON.
//
// The bug this pins has shipped twice, both times identically: a footer button
// hand-rolled its own SOLID_COLOR quad instead of calling addButtonQuad(), so with
// a theme installed it stayed a flat rectangle while the buttons beside it drew the
// theme's nine-slice. First the Reset button; then the "vX.Y.Z available!" chip,
// reported by a user as "the notice does not use the 9-slice button graphic like
// other buttons". Neither is visible without a theme, which is why both shipped.
//
// It is COUNTED rather than looked at: a themed button is nine quads and a flat one
// is one, so "did this button go through the helper" has an integer answer that
// needs no screenshot.
//
// SUBJECT CHANGED, INVARIANT KEPT. This measured the update chip, and the chip is
// gone: the footer's version string became the About button, and the update notice
// moved to a tag on the Updates row. The old measurement exploited the chip
// APPEARING -- panel quads with an update pending, minus without -- which is not
// available for a button that is always there.
//
// So the About button is measured directly instead, by counting the quads inside
// its own click region. That is a better probe than the delta it replaces: it
// isolates one named button rather than inferring it from a panel total, and it
// cannot be satisfied by quads appearing anywhere else on the panel.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

namespace {

// Frame / card insets in grid cells, matching the shipped themes' proportions.
constexpr float FRAME_INSET = 2.0f;
constexpr float CARD_INSET  = 1.0f;

struct Counts {
    int baseline = 0;   // settings-panel quads in total
    int about    = 0;   // quads lying inside the About button's own rect
};

// A DISTINCT NAME per install: installSyntheticTheme APPENDS and getThemeByName
// returns the FIRST match, so reusing one name silently keeps measuring the earlier
// theme -- which is how this read "1 and 1" before the names were split, a vacuous
// pass that looked like a real failure of the fix.
Counts measure(PluginHost& host, bool buttonSprites) {
    host.installTheme(buttonSprites ? "btn_sliced" : "btn_flat",
                      FRAME_INSET, CARD_INSET, /*titleBand=*/1, /*card=*/1,
                      /*cardSprites=*/true, buttonSprites);
    host.showSettings(true);
    host.setActiveTab("General");
    host.draw();

    Counts c;
    c.baseline = static_cast<int>(host.hudQuadRects(PluginHost::HUD_SETTINGS).size());

    // The button's rect from its own click region, not re-derived here: a test that
    // recomputed the footer geometry would assert against its own copy of the layout.
    int rl = 0, rt = 0, rr = 0, rb = 0;
    REQUIRE(host.aboutButtonRect(rl, rt, rr, rb));
    const double bl = rl / 1e6, bt = rt / 1e6, br = rr / 1e6, bb = rb / 1e6;
    // Half a pixel of slack at 1080p: the nine-slice's corner and edge pieces tile
    // the rect exactly, but each is snapped independently.
    constexpr double EPS = 5e-4;
    for (const auto& q : host.hudQuadRects(PluginHost::HUD_SETTINGS)) {
        if (q.l >= bl - EPS && q.t >= bt - EPS && q.r <= br + EPS && q.b <= bb + EPS) {
            ++c.about;
        }
    }
    return c;
}

}  // namespace

TEST_CASE("the About button is a themed button, not a hand-rolled rectangle") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\settings_button_theme\\");
    REQUIRE_MESSAGE(host.hasThemeGeometry(),
                    "MXBMRP3_Test_InstallTheme not exported (test build?)");
    REQUIRE_MESSAGE(host.hasQuadRects(),
                    "MXBMRP3_Test_HudQuadRects not exported (test build?)");
    REQUIRE_MESSAGE(host.hasAboutRect(),
                    "MXBMRP3_Test_AboutButtonRect not exported (test build?)");

    // A theme WITHOUT button art: addButtonQuad falls back to a flat quad, so the
    // button is one. This is the baseline the themed count is measured against --
    // without it a "9" could just as well be nine quads from somewhere else.
    const Counts flat = measure(host, /*buttonSprites=*/false);
    INFO("flat theme: " << flat.baseline << " panel quads, About = " << flat.about);
    CHECK(flat.about == 1);

    const Counts sliced = measure(host, /*buttonSprites=*/true);
    INFO("sliced theme: " << sliced.baseline << " panel quads, About = " << sliced.about);

    // THE THEME ACTUALLY TOOK. Every other footer and tab-list button goes through
    // the same helper, so switching on the button set has to grow the panel by eight
    // quads per button before About is even measured. Without this the case would
    // pass just as happily against an unthemed panel, which is precisely the state
    // the first run of it was accidentally measuring.
    CHECK(sliced.baseline > flat.baseline);

    // ...and the button itself is a nine-slice. This is the assertion that fails if
    // anyone hand-rolls its quad again: it would read 1 here and 1 above.
    CHECK(sliced.about == 9);
}
