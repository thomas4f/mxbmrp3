// ============================================================================
// tests/unit/test_cpp_js_parity.cpp
// C++ side of the cross-boundary mirror parity tests.
//
// Several helpers exist twice — once in the plugin, once in the web overlay's
// JS — with "keep them in step" comments: PluginUtils::isColorDark ↔
// overlay-util.js isColorDark, and session_charts_math.h formatSecs ↔
// overlay-charts.js fmtChartSecs. This TU asserts the C++ implementations
// against the shared golden vectors in tests/fixtures/cpp_js_parity.json;
// tests/web/tests/parity.spec.js asserts the JS implementations against the
// SAME file (evaluated in the real overlay page), so a one-sided behavior
// change fails one of the two suites instead of silently diverging renderers.
//
// PARITY_FIXTURE (the fixture's absolute path) is defined by run_tests.sh.
// ============================================================================
#include "doctest.h"

#include <cstdint>
#include <fstream>

#include "core/plugin_utils.h"
#include "hud/session_charts_math.h"
#include "vendor/nlohmann/json.hpp"

static nlohmann::json loadFixture() {
    std::ifstream in(PARITY_FIXTURE);
    REQUIRE_MESSAGE(in.good(), "fixture not readable: " << PARITY_FIXTURE);
    return nlohmann::json::parse(in);
}

TEST_CASE("parity: PluginUtils::isColorDark matches the shared golden vectors") {
    const auto fx = loadFixture();
    for (const auto& c : fx.at("isColorDark")) {
        const std::string hex = c.at("hex").get<std::string>();
        REQUIRE(hex.size() == 7);
        // "#rrggbb" (the overlay's form) -> the plugin's 0xBBGGRR color word.
        const unsigned long r = std::stoul(hex.substr(1, 2), nullptr, 16);
        const unsigned long g = std::stoul(hex.substr(3, 2), nullptr, 16);
        const unsigned long b = std::stoul(hex.substr(5, 2), nullptr, 16);
        const unsigned long color = r | (g << 8) | (b << 16);
        INFO("hex " << hex);
        CHECK(PluginUtils::isColorDark(color) == c.at("dark").get<bool>());
    }
}

TEST_CASE("parity: sector-resolution chart helpers match the shared golden vectors") {
    const auto fx = loadFixture();
    for (const auto& c : fx.at("cumulativeBySector")) {
        const auto sectors = c.at("sectors").get<std::vector<int>>();
        const auto expected = c.at("out").get<std::vector<long long>>();
        INFO(c.value("_why", ""));
        CHECK(SessionChartsMath::cumulativeBySector(sectors, c.at("sectorsPerLap").get<int>())
              == expected);
    }
    for (const auto& c : fx.at("lapsAtSectorIndex")) {
        INFO("index " << c.at("index").get<int>()
             << " sectorsPerLap " << c.at("sectorsPerLap").get<int>());
        CHECK(SessionChartsMath::lapsAtSectorIndex(c.at("index").get<int>(),
                                                   c.at("sectorsPerLap").get<int>())
              == doctest::Approx(c.at("out").get<double>()));
    }
    for (const auto& c : fx.at("latestPositionExtent")) {
        INFO(c.value("_why", ""));
        const auto e = SessionChartsMath::latestPositionExtent(
            c.at("positions").get<std::vector<std::vector<int>>>());
        CHECK(e.top == c.at("top").get<int>());
        CHECK(e.bottom == c.at("bottom").get<int>());
    }
    for (const auto& c : fx.at("traceValueAtSector")) {
        INFO(c.value("_why", ""));
        CHECK(SessionChartsMath::traceValueAtSector(c.at("refPaceMs").get<long long>(),
                                                    c.at("index").get<int>(),
                                                    c.at("sectorsPerLap").get<int>(),
                                                    c.at("cumulativeMs").get<long long>())
              == c.at("out").get<long long>());
    }
    for (const auto& c : fx.at("xFracForLaps")) {
        INFO(c.value("_why", ""));
        CHECK(SessionChartsMath::xFracForLaps(c.at("laps").get<float>(),
                                              c.at("firstLaps").get<float>(),
                                              c.at("maxLaps").get<float>())
              == doctest::Approx(c.at("out").get<double>()));
    }
}

TEST_CASE("parity: session_charts_math formatSecs matches the shared golden vectors") {
    const auto fx = loadFixture();
    for (const auto& c : fx.at("formatSecs")) {
        char buf[32];
        SessionChartsMath::formatSecs(buf, sizeof(buf),
                                      c.at("ms").get<long long>(),
                                      c.at("showSign").get<bool>());
        INFO("ms " << c.at("ms").get<long long>()
             << " showSign " << c.at("showSign").get<bool>());
        CHECK(std::string(buf) == c.at("out").get<std::string>());
    }
}
