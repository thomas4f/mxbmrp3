// ============================================================================
// tests/integration/tests/gamepad_layout_test.cpp
// The gamepad widget draws its controller FRAME from fontSize (backgroundWidth/
// Height) but positioned its buttons/sticks off the global LineHeights::NORMAL.
// When NORMAL was bumped for snap-grid alignment (#256, 0.0222 -> ~0.0235) the
// interior grew relative to the frame and the buttons slid down/right off the
// controller face. The fix pins the interior to fontSize, so the content tracks the
// frame again.
//
// Guard: with a faked connected controller, the content's bottom/right extent inside
// the frame is a stable golden signature of the layout. The fix puts them at ~0.724 /
// ~0.856 of the box; the #256 regression pushed them to ~0.754 / ~0.878. A future
// change that re-detaches the interior from the frame moves them out of tolerance.
// (Headless — the layout is pure math; the fake controller comes from
// MXBMRP3_Test_FakeGamepad.)
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

TEST_CASE("gamepad widget: content stays aligned to the controller frame") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\gamepad_layout\\");

    host.fakeGamepad(true);   // force a connected controller + show the widget
    host.draw();

    auto e = host.gamepadContentExtent();
    REQUIRE_MESSAGE(e.bottom >= 0.0f, "gamepad didn't render (fake-controller hook missing?)");
    MESSAGE("gamepad content extent: bottom=" << e.bottom << " right=" << e.right);

    // Golden signature (interior pinned to the fontSize-sized frame). Tolerance is
    // tight enough to exclude the #256 broken state (0.754 / 0.878) with margin.
    CHECK(e.bottom == doctest::Approx(0.7237f).epsilon(0.01));   // broken: 0.754
    CHECK(e.right  == doctest::Approx(0.8562f).epsilon(0.01));   // broken: 0.878

    host.fakeGamepad(false);
    host.shutdown();
}
