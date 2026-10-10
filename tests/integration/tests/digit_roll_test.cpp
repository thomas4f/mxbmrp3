// ============================================================================
// tests/integration/tests/digit_roll_test.cpp
// Motion's digit roll (hud/digit_roll.h), on the strings the widgets draw,
// with Motion's clock driven by hand:
//   - Gear: Motion Off draws one digit, the new one, at once; on, up a gear the
//     old digit drops from its place and fades while the new one comes in from
//     above (the opposite of the odometer's drum); at the end only the new one
//     is left, back in place; down a gear the directions swap
//   - Gear shifted on the red: the old digit rolls out in the colour it had,
//     not the colour of the new gear (the RPM drops the moment you shift)
//   - Speedo odometer / trip meter: a step of one rolls the last digit alone
//     (the old one rises, the new comes from below), a carry rolls the white
//     digits too, a jump of more than one switches, and so does Motion Off
// Self-contained doctest; see run_tests.sh / TESTING.md.
// ============================================================================
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include <cmath>
#include <cstdio>   // std::remove
#include <string>
#include <vector>

namespace {
struct Digit { std::string text; double y = 0.0; unsigned alpha = 0; unsigned long color = 0; };

// The gear strings the widget drew (its empty caption slot left out).
std::vector<Digit> digits(PluginHost& host) {
    host.drawWithState(0);   // on track: the telemetry is the player's own
    host.drawWithState(0);
    std::vector<Digit> out;
    const auto rows = host.hudStringRows("gear_widget");
    for (size_t i = 0; i < rows.size(); ++i) {
        if (rows[i].text.empty()) continue;
        const unsigned long c = host.stringColor("gear_widget", static_cast<int>(i));
        out.push_back({ rows[i].text, rows[i].y, static_cast<unsigned>((c >> 24) & 0xFF), c });
    }
    return out;
}
}  // namespace

TEST_CASE("digit roll: the gear switches with Motion off, rolls with it on") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.startup("Z:\\tmp\\mxbmrp3-tests\\digit_roll_gear\\") >= 0);
    REQUIRE_MESSAGE(host.hasMotion(), "Motion test hooks not exported (test build?)");
    REQUIRE(host.hasStringRows());
    host.setMotion(0);

    host.eventInit("RollTrack", "Player");
    host.drawWithState(0);
    host.telemetry(10.0f, 1);
    std::vector<Digit> d = digits(host);
    REQUIRE(d.size() == 1);
    CHECK(d[0].text == "1");
    const double baseY = d[0].y;

    // Motion off: one digit, the new one, in place.
    host.telemetry(10.0f, 2);
    d = digits(host);
    REQUIRE(d.size() == 1);
    CHECK(d[0].text == "2");
    CHECK(d[0].y == doctest::Approx(baseY));

    // Motion on, up a gear: halfway, the 2 below its place and the 3 above, both partly faded.
    host.setMotion(1);
    host.setMotionNowUs(1000000);
    host.telemetry(10.0f, 3);
    d = digits(host);
    REQUIRE(d.size() == 1);
    CHECK(d[0].text == "2");          // the roll starts on the old digit
    host.setMotionNowUs(1000000 + 100000);
    d = digits(host);
    INFO("halfway up: " << d.size() << " strings");
    REQUIRE(d.size() == 2);
    CHECK(d[0].text == "2");
    CHECK(d[0].y > baseY);
    CHECK(d[0].alpha < 255u);
    CHECK(d[1].text == "3");
    CHECK(d[1].y < baseY);
    CHECK(d[1].alpha < 255u);
    host.setMotionNowUs(1000000 + 400000);
    d = digits(host);
    REQUIRE(d.size() == 1);
    CHECK(d[0].text == "3");
    CHECK(d[0].y == doctest::Approx(baseY));

    // Down a gear: the old digit rises and the new one comes from below.
    host.setMotionNowUs(2000000);
    host.telemetry(10.0f, 2);
    digits(host);
    host.setMotionNowUs(2000000 + 100000);
    d = digits(host);
    REQUIRE(d.size() == 2);
    CHECK(d[0].text == "3");
    CHECK(d[0].y < baseY);
    CHECK(d[1].text == "2");
    CHECK(d[1].y > baseY);

    host.setMotion(0);
    host.setMotionNowUs(-1);
    host.shutdown();
}

namespace {
// The speedo's trip meter strings, split into its white digits and its last
// digit (told from the odometer row above it by height).
struct TripRow { std::vector<Digit> main, last; };

TripRow tripRow(PluginHost& host, double odoY, double tripY) {
    host.drawWithState(0);
    host.drawWithState(0);
    TripRow out;
    const auto rows = host.hudStringRows("speedo_widget");
    for (size_t i = 0; i < rows.size(); ++i) {
        if (std::fabs(rows[i].y - tripY) >= std::fabs(rows[i].y - odoY)) continue;
        const unsigned long c = host.stringColor("speedo_widget", static_cast<int>(i));
        Digit d{ rows[i].text, rows[i].y, static_cast<unsigned>((c >> 24) & 0xFF) };
        (rows[i].text.size() == 1 ? out.last : out.main).push_back(d);
    }
    return out;
}
}  // namespace

TEST_CASE("digit roll: the trip meter rolls its last digit, the rest on a carry, and switches on a jump") {
    const char* saveWin = "Z:\\tmp\\mxbmrp3-tests\\digit_roll_trip\\";
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.startup(saveWin) >= 0);
    REQUIRE(host.hasMotion());
    REQUIRE(host.hasStringRows());
    REQUIRE(host.hasStatsOdometer());
    std::remove("Z:\\tmp\\mxbmrp3-tests\\digit_roll_trip\\mxbmrp3\\mxbmrp3_stats.json");
    // The trip meter is off by default.
    host.writeSettingsFile(saveWin, "[Settings]\nversion=9\n\n[SpeedoWidget]\nvisible=1\nshowTripMeter=1\n");
    host.loadSettings(saveWin);
    host.setMotion(0);

    host.eventInit("RollTrack", "Player");
    host.raceEvent("RollTrack");
    host.session(1, 0, 480000);
    host.addEntry(10, "Player");
    host.runInit(1);

    // 50 m/s, 100 ms apart: 5 m a tick. The speedo counts in miles by default
    // (the Speed widget's unit), so a trip meter step is 0.1 mi, about 161 m.
    long long t = 1000000;
    host.statsSetNowUs(t);
    host.telemetry(50.0f);
    auto ride = [&](int ticks) {
        for (int i = 0; i < ticks; ++i) { t += 100000; host.statsSetNowUs(t); host.telemetry(50.0f); }
    };

    host.drawWithState(0);
    host.drawWithState(0);
    auto rows = host.hudStringRows("speedo_widget");
    REQUIRE(rows.size() == 4);   // odometer white + last, trip white + last
    CHECK(rows[2].text == "000");
    CHECK(rows[3].text == "0");
    const double odoY = rows[0].y, tripY = rows[2].y;

    // One step (0.0 -> 0.1): the last digit rolls up, the white digits stay.
    host.setMotion(1);
    host.setMotionNowUs(10000000);
    tripRow(host, odoY, tripY);
    ride(20);   // 100 m = 0.06 mi
    tripRow(host, odoY, tripY);   // the roll starts here
    host.setMotionNowUs(10000000 + 100000);
    TripRow r = tripRow(host, odoY, tripY);
    REQUIRE(r.main.size() == 1);
    CHECK(r.main[0].text == "000");
    CHECK(r.main[0].y == doctest::Approx(tripY));
    REQUIRE(r.last.size() == 2);
    CHECK(r.last[0].text == "0");
    CHECK(r.last[0].y < tripY);       // rising out
    CHECK(r.last[1].text == "1");
    CHECK(r.last[1].y > tripY);       // coming from below
    CHECK(r.last[1].alpha < 255u);

    // A jump (0.1 -> 0.9) switches.
    host.setMotionNowUs(20000000);
    ride(270);   // 1450 m = 0.90 mi
    r = tripRow(host, odoY, tripY);
    REQUIRE(r.last.size() == 1);
    CHECK(r.last[0].text == "9");
    CHECK(r.last[0].y == doctest::Approx(tripY));

    // A carry (0.9 -> 1.0): the white digits roll with the last one.
    host.setMotionNowUs(30000000);
    tripRow(host, odoY, tripY);
    ride(30);   // 1600 m = 0.99 mi
    tripRow(host, odoY, tripY);
    host.setMotionNowUs(30000000 + 100000);
    r = tripRow(host, odoY, tripY);
    REQUIRE(r.main.size() == 2);
    CHECK(r.main[0].text == "000");
    CHECK(r.main[1].text == "001");
    CHECK(r.main[1].y > tripY);
    REQUIRE(r.last.size() == 2);
    CHECK(r.last[0].text == "9");
    CHECK(r.last[1].text == "0");

    // Motion Off: a step switches.
    host.setMotion(0);
    host.setMotionNowUs(40000000);
    ride(30);   // 1750 m = 1.09 mi
    r = tripRow(host, odoY, tripY);
    REQUIRE(r.main.size() == 1);
    REQUIRE(r.last.size() == 1);
    CHECK(r.last[0].text == "1");
    CHECK(r.last[0].alpha == 255u);

    host.setMotionNowUs(-1);
    host.statsSetNowUs(-1);
    host.shutdown();
}

TEST_CASE("digit roll: a gear shifted on the red rolls out red") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.startup("Z:\\tmp\\mxbmrp3-tests\\digit_roll_red\\") >= 0);
    REQUIRE(host.hasMotion());
    REQUIRE(host.hasStringRows());
    host.setMotion(0);
    host.eventInit("RollTrack", "Player", 1600.0f, 2, "Test 450", "MX1", "", 0, "", 0.0f, 9000);
    host.drawWithState(0);

    TelemetryRow row;
    row.gear = 3;
    row.rpm = 5000;
    host.telemetryFrame(row);
    std::vector<Digit> d = digits(host);
    REQUIRE(d.size() == 1);
    const unsigned long white = d[0].color & 0x00FFFFFFul;
    row.rpm = 9500;                    // past the shift point: the gear turns red
    host.telemetryFrame(row);
    d = digits(host);
    REQUIRE(d.size() == 1);
    CHECK(d[0].text == "3");
    const unsigned long red = d[0].color & 0x00FFFFFFul;
    REQUIRE(red != white);

    // Shift up: the RPM drops back below the shift point at once.
    host.setMotion(1);
    host.setMotionNowUs(1000000);
    row.gear = 4;
    row.rpm = 7000;
    host.telemetryFrame(row);
    digits(host);
    host.setMotionNowUs(1000000 + 100000);
    d = digits(host);
    REQUIRE(d.size() == 2);
    CHECK(d[0].text == "3");
    CHECK((d[0].color & 0x00FFFFFFul) == red);     // leaves in the colour it had
    CHECK(d[1].text == "4");
    CHECK((d[1].color & 0x00FFFFFFul) == white);

    host.setMotion(0);
    host.setMotionNowUs(-1);
    host.shutdown();
}
