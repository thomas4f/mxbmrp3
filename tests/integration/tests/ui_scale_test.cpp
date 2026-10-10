// tests/integration/tests/ui_scale_test.cpp
//
// Appearance's UI scale: every HUD draws at its own Scale times the UI scale, and
// grows from its origin exactly as its own Scale does. Two halves, both of which a
// regression would lose without any other test noticing:
//   - the INI keeps each HUD's OWN Scale -- writing the product would compound the
//     UI scale into every saved value on the next load;
//   - positions are untouched, so a profile switch or a per-tab Reset never has
//     a move to undo (reset_tab_test sweeps the slider and checks the file).
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

#include <cstdlib>

namespace {
constexpr int SLOP = 2;   // x1e6 quantisation of the edge hook, both sides
}

TEST_CASE("UI scale: HUDs draw at their own Scale times it, growing from their origin") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\ui_scale\\");
    REQUIRE(host.hasScreenEdges());
    REQUIRE_MESSAGE(host.setUiScale(1.0f), "MXBMRP3_Test_SetUiScale not exported (test build?)");
    host.showAllHuds(true);
    host.eventInit("Southwick", "Thomas");
    host.session(1, 0, 0);
    host.runInit(1);
    host.addEntry(4, "Thomas");

    REQUIRE(host.setHudScale("lap_widget", 1.0f));
    REQUIRE(host.setHudScale("position_widget", 1.2f));
    REQUIRE(host.setHudOffset("position_widget", 0.60f, 0.50f));
    host.draw();
    const auto lap0 = host.hudScreenEdges("lap_widget");
    const auto pos0 = host.hudScreenEdges("position_widget");
    REQUIRE(lap0.r > lap0.l);
    REQUIRE(pos0.r > pos0.l);

    REQUIRE(host.setUiScale(1.3f));
    host.draw();

    SUBCASE("own Scale stays what the INI stores; the drawn scale is the product") {
        CHECK(host.hudScales("lap_widget").own == 1000);
        CHECK(host.hudScales("lap_widget").drawn == 1300);
        CHECK(host.hudScales("position_widget").own == 1200);
        CHECK(host.hudScales("position_widget").drawn == 1560);
    }

    SUBCASE("a HUD grows from its origin, as its own Scale makes it") {
        const auto pos = host.hudScreenEdges("position_widget");
        CHECK(pos.r - pos.l > pos0.r - pos0.l);
        CHECK(pos.b - pos.t > pos0.b - pos0.t);
        CHECK(std::abs(pos.l - pos0.l) <= SLOP);
        CHECK(std::abs(pos.t - pos0.t) <= SLOP);
    }

    SUBCASE("back to 100% restores the layout exactly") {
        REQUIRE(host.setUiScale(1.0f));
        host.draw();
        const auto pos = host.hudScreenEdges("position_widget");
        CHECK(pos.l == pos0.l);
        CHECK(pos.r == pos0.r);
        CHECK(pos.b == pos0.b);
        CHECK(host.hudScales("position_widget").drawn == 1200);
    }
}
