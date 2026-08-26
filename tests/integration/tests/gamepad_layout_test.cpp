// ============================================================================
// tests/integration/tests/gamepad_layout_test.cpp
// The gamepad widget is a PICTURE of a controller. Its frame is sized from the
// type; its buttons, sticks and triggers are ~30 hand-placed offsets measured
// against the artwork. Anything that moves one without the other slides the
// buttons off the controller face, and that has now happened twice:
//
//  1. #256 bumped the global row pitch for snap-grid alignment (0.0222 -> ~0.0235).
//     The interior was positioned off that pitch and the frame off fontSize, so the
//     content grew relative to the frame and slid down and right.
//  2. The offsets were then scaled by the widget's own SCALE SLIDER alone, while the
//     frame kept growing with [Advanced] uiFontSize (and, later, a theme's content
//     inset). Raise the font size and the picture got bigger with the buttons
//     standing still — reported from the game.
//
// Both are the same fault: two references for one drawing. The interior's em is
// derived from the frame's own width now (hud/gamepad_geometry.h), so there is one.
//
// Two cases, and the second is the one that matters. The golden extent below is a
// SIGNATURE — it pins the shipped look and catches a change that moves the content,
// but a signature taken at one font size is exactly what missed bug 2. The
// invariance case asks the question the user actually asked.
//
// (Headless — the layout is pure math; the fake controller comes from
// MXBMRP3_Test_FakeGamepad. The unit suite pins the arithmetic underneath, in
// tests/unit/test_gamepad_geometry.cpp.)
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

#include <cmath>

TEST_CASE("gamepad widget: content stays aligned to the controller frame") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\gamepad_layout\\");

    host.fakeGamepad(true);   // force a connected controller + show the widget
    host.draw();

    auto e = host.gamepadContentExtent();
    REQUIRE_MESSAGE(e.bottom >= 0.0f, "gamepad didn't render (fake-controller hook missing?)");
    MESSAGE("gamepad content extent: bottom=" << e.bottom << " right=" << e.right);

    // Golden signature (interior derived from the frame). Tolerance is tight enough
    // to exclude the #256 broken state (0.754 / 0.878) with margin.
    CHECK(e.bottom == doctest::Approx(0.7237f).epsilon(0.01));   // broken: 0.754
    CHECK(e.right  == doctest::Approx(0.8562f).epsilon(0.01));   // broken: 0.878

    host.fakeGamepad(false);
    host.shutdown();
}

// The buttons stay on the controller when the base type size changes.
//
// The extent is measured as a FRACTION of the frame, so if the whole widget scales
// together the reading does not move at all — whatever uiFontSize is. That is the
// property, and it is stronger than the golden above precisely because it does not
// depend on knowing the right number: any drift between interior and frame shows up
// as the fraction changing, in whichever direction.
//
// Before the fix, going 0.020 -> 0.025 held the offsets still while the frame grew
// 25%, pulling the content's extent toward the frame's top-left — bottom fell to
// ~0.66 against 0.7237, roughly 12 px of the sticks and face buttons off their
// sockets at 1080p. Two sizes each side of the default, so a fix that merely
// happened to work at one is not enough.
TEST_CASE("gamepad widget: the content extent does not move with uiFontSize") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE_MESSAGE(host.hasUiFontSize(), "MXBMRP3_Test_SetUiFontSize missing (test build?)");
    host.startup("Z:\\tmp\\mxbmrp3-tests\\gamepad_fontsize\\");

    host.fakeGamepad(true);
    host.draw();
    const auto ref = host.gamepadContentExtent();
    REQUIRE_MESSAGE(ref.bottom >= 0.0f, "gamepad didn't render at the default font size");

    for (float fs : { 0.014f, 0.025f, 0.032f }) {
        host.setUiFontSize(fs);
        host.draw();
        const auto e = host.gamepadContentExtent();
        CAPTURE(fs);
        REQUIRE_MESSAGE(e.bottom >= 0.0f, "gamepad didn't render at this font size");
        // 0.005 of the frame -- ~3px of a 1080p-tall controller. Tighter than the
        // golden's tolerance because this compares the widget against ITSELF, so
        // there is no calibration slack to absorb.
        CHECK_MESSAGE(std::fabs(e.bottom - ref.bottom) < 0.005f,
                      "the content's bottom extent moved from " << ref.bottom << " to "
                      << e.bottom << " when uiFontSize changed to " << fs
                      << " -- the interior is being sized from something other than "
                      "the frame again");
        CHECK_MESSAGE(std::fabs(e.right - ref.right) < 0.005f,
                      "the content's right extent moved from " << ref.right << " to "
                      << e.right << " at uiFontSize " << fs);
    }

    host.setUiFontSize(0.0200f);   // leave the global metrics as found
    host.fakeGamepad(false);
    host.shutdown();
}
