// ============================================================================
// tests/unit/test_plugin_utils.cpp
// Unit tests for the platform-independent pure logic in core/plugin_utils.h.
//
// These functions are defined inline in the header and depend on nothing but
// the C++ standard library, so they compile and run on any host with a C++17
// compiler — no game engine, no Windows, no PluginData singleton. (The
// formatting functions in plugin_utils.CPP reach into the PluginData singleton
// and the full PluginConstants tables, which drag in the game API and can't
// build here; those are covered by the Wine integration tests instead.)
//
// Framework: doctest (single header, tests/integration/harness/doctest.h). This TU
// defines the doctest main; add more unit TUs and link them together (see
// run_tests.sh).
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "core/plugin_utils.h"
#include <cmath>
#include <string>

using PU = PluginUtils;

TEST_CASE("makeColor packs RGBA in 0xAABBGGRR order") {
    CHECK(PU::makeColor(0x11, 0x22, 0x33, 0x44) == 0x44332211UL);
    CHECK(PU::makeColor(0xFF, 0x00, 0x00) == 0xFF0000FFUL);   // default alpha opaque
    CHECK(PU::makeColor(0, 0, 0, 0) == 0x00000000UL);
    CHECK(PU::makeColor(255, 255, 255, 255) == 0xFFFFFFFFUL);
}

TEST_CASE("applyOpacity replaces alpha, keeps RGB, truncates toward zero") {
    const unsigned long base = PU::makeColor(10, 20, 30, 255);
    CHECK(PU::applyOpacity(base, 1.0f) == PU::makeColor(10, 20, 30, 255));
    CHECK(PU::applyOpacity(base, 0.0f) == PU::makeColor(10, 20, 30, 0));
    CHECK(PU::applyOpacity(base, 0.5f) == PU::makeColor(10, 20, 30, 127));  // 127.5 -> 127
    // Original alpha is ignored, not blended.
    CHECK(PU::applyOpacity(PU::makeColor(10, 20, 30, 128), 1.0f) == PU::makeColor(10, 20, 30, 255));
}

TEST_CASE("isColorDark: BT.601 luma, threshold at 128 (mirrored in overlay-util.js)") {
    SUBCASE("saturated and grayscale") {
        CHECK(PU::isColorDark(PU::makeColor(0, 0, 0)));            // black
        CHECK_FALSE(PU::isColorDark(PU::makeColor(255, 255, 255)));// white
        CHECK(PU::isColorDark(PU::makeColor(255, 0, 0)));          // red  luma 76 -> white text
        CHECK(PU::isColorDark(PU::makeColor(0, 0, 255)));          // blue luma 29
        CHECK_FALSE(PU::isColorDark(PU::makeColor(255, 255, 0)));  // yellow luma 225 -> dark text
    }
    SUBCASE("boundary is exactly 128 (gray value g has luma g)") {
        CHECK(PU::isColorDark(PU::makeColor(127, 127, 127)));      // 127 < 128
        CHECK_FALSE(PU::isColorDark(PU::makeColor(128, 128, 128)));// 128 < 128 is false
        CHECK(PU::isColorDark(PU::makeColor(127, 127, 127, 0)));   // alpha must not affect luma
        CHECK(PU::isColorDark(PU::makeColor(127, 127, 127, 255)));
    }
}

TEST_CASE("lighten/darken: exact endpoints, alpha preserved") {
    SUBCASE("lighten") {
        const unsigned long c = PU::makeColor(100, 100, 100, 200);
        CHECK(PU::lightenColor(c, 0.0f) == c);
        CHECK(PU::lightenColor(c, 1.0f) == PU::makeColor(255, 255, 255, 200));  // -> white, alpha kept
    }
    SUBCASE("darken") {
        const unsigned long c = PU::makeColor(200, 100, 50, 200);
        CHECK(PU::darkenColor(c, 1.0f) == c);
        CHECK(PU::darkenColor(c, 0.0f) == PU::makeColor(0, 0, 0, 200));         // -> black, alpha kept
        CHECK(PU::darkenColor(c, 0.5f) == PU::makeColor(100, 50, 25, 200));
    }
}

TEST_CASE("formatScore: thousands grouping, zero, negatives, buffer size") {
    char b[32];
    PU::formatScore(0, b, sizeof(b));        CHECK(std::string(b) == "0");
    PU::formatScore(7, b, sizeof(b));        CHECK(std::string(b) == "7");
    PU::formatScore(999, b, sizeof(b));      CHECK(std::string(b) == "999");
    PU::formatScore(1000, b, sizeof(b));     CHECK(std::string(b) == "1,000");
    PU::formatScore(1234567, b, sizeof(b));  CHECK(std::string(b) == "1,234,567");
    PU::formatScore(-1234567, b, sizeof(b)); CHECK(std::string(b) == "-1,234,567");
    PU::formatScore(-42, b, sizeof(b));      CHECK(std::string(b) == "-42");

    SUBCASE("bufferSize 0 is a no-op") {
        char z[8] = {'Z'};
        PU::formatScore(123, z, 0);
        CHECK(z[0] == 'Z');
    }
    SUBCASE("tight buffer still null-terminates in bounds") {
        char c[6];
        PU::formatScore(1000, c, sizeof(c));
        CHECK(std::string(c) == "1,000");
    }
}

TEST_CASE("formatColorHex is eight lowercase digits") {
    CHECK(PU::formatColorHex(0x00000000u) == "0x00000000");
    CHECK(PU::formatColorHex(0x000000FFu) == "0x000000ff");
    CHECK(PU::formatColorHex(0xDEADBEEFu) == "0xdeadbeef");
    CHECK(PU::formatColorHex(0xFFFFFFFFu) == "0xffffffff");
}

TEST_CASE("parseColorHex accepts hex + decimal, and never throws") {
    SUBCASE("valid") {
        CHECK(PU::parseColorHex("0xFFFFFFFF") == 0xFFFFFFFFu);
        CHECK(PU::parseColorHex("0x00ff0000") == 0x00ff0000u);
        CHECK(PU::parseColorHex("4294967295") == 0xFFFFFFFFu);  // base 0 auto-detects decimal
        CHECK(PU::parseColorHex("0") == 0u);
    }
    SUBCASE("malformed returns fallback, never throws (a bad INI value must not abort the load)") {
        CHECK(PU::parseColorHex("not-a-number", 42u) == 42u);
        CHECK(PU::parseColorHex("", 7u) == 7u);
        CHECK(PU::parseColorHex("   ", 7u) == 7u);
        CHECK(PU::parseColorHex("ZZZZ", 9u) == 9u);
        CHECK(PU::parseColorHex("0xFFFFFFFFFFFFFFFFFFFF", 3u) == 3u);  // overflow -> caught -> fallback
        CHECK(PU::parseColorHex("garbage") == 0u);                    // default fallback
        // QUIRK (documented): "0xZZZZ" is NOT an error — strtoul(base 0) consumes
        // the leading "0", stops at 'x', returns 0. Pinned so a parse-path change
        // is a conscious decision.
        CHECK(PU::parseColorHex("0xZZZZ", 9u) == 0u);
    }
    SUBCASE("round trips") {
        for (uint32_t v : {0x00000000u, 0x12345678u, 0xDEADBEEFu, 0xFFFFFFFFu, 0x000000FFu})
            CHECK(PU::parseColorHex(PU::formatColorHex(v)) == v);
    }
}

TEST_CASE("getRelativePositionColor: ahead / behind / lapped matrix") {
    const unsigned long NEUTRAL  = PU::makeColor(0, 200, 0);
    const unsigned long WARNING  = PU::makeColor(200, 0, 0);
    const unsigned long FALLBACK = PU::makeColor(50, 50, 50);
    auto f = &PU::getRelativePositionColor;

    CHECK(f(0, 3, 5, 5, NEUTRAL, WARNING, FALLBACK) == FALLBACK);  // invalid positions
    CHECK(f(3, 0, 5, 5, NEUTRAL, WARNING, FALLBACK) == FALLBACK);
    CHECK(f(5, 2, 3, 3, NEUTRAL, WARNING, FALLBACK) == NEUTRAL);   // ahead, same lap
    CHECK(f(5, 2, 3, 4, NEUTRAL, WARNING, FALLBACK) == PU::lightenColor(NEUTRAL, 0.5f)); // ahead, lap up
    CHECK(f(2, 5, 3, 3, NEUTRAL, WARNING, FALLBACK) == WARNING);   // behind, same lap
    CHECK(f(2, 5, 4, 3, NEUTRAL, WARNING, FALLBACK) == PU::darkenColor(WARNING, 0.7f));  // behind, lapped
}

TEST_CASE("plugin utils: RGB hex in, ABGR out") {
    // The one conversion in the theme pipeline. A skinner writes #rrggbb, the game
    // wants ABGR, and getting the byte order wrong is invisible in code review and
    // obvious on screen only if you happen to pick an asymmetric colour.
    unsigned long c = 0;
    REQUIRE(PluginUtils::parseRgbHex("#ff8800", c));
    CHECK(c == PluginUtils::makeColor(0xff, 0x88, 0x00));
    CHECK((c & 0xFF) == 0xff);           // R in the low byte, which is what ABGR means
    CHECK(((c >> 16) & 0xFF) == 0x00);   // B in the high one

    // The three spellings a skinner might type, all equal.
    unsigned long bare = 0, prefixed = 0;
    REQUIRE(PluginUtils::parseRgbHex("ff8800", bare));
    REQUIRE(PluginUtils::parseRgbHex("0xFF8800", prefixed));
    CHECK(bare == c);
    CHECK(prefixed == c);

    // Rejected rather than half-parsed: strtol-style leniency here would turn a typo
    // into a plausible wrong colour instead of a logged warning.
    unsigned long junk = 0xdeadbeef;
    CHECK_FALSE(PluginUtils::parseRgbHex("#ff88", junk));      // too short
    CHECK_FALSE(PluginUtils::parseRgbHex("#ff88000", junk));   // too long
    CHECK_FALSE(PluginUtils::parseRgbHex("#gg8800", junk));    // not hex
    CHECK_FALSE(PluginUtils::parseRgbHex("#f80", junk));       // no 3-digit shorthand
    CHECK_FALSE(PluginUtils::parseRgbHex("", junk));
    CHECK(junk == 0xdeadbeef);                                  // and out is untouched
}

// Three subsystems compute along-track distance — teleport detection, the
// radar's proximity ring, the spotter's rider-behind/alongside scan — and each
// used to carry its own copy of the wrap. The wrap is the whole point: track
// positions are 0..1 and reset at start/finish, so a rider 0.2 of a lap behind
// you reads as 0.8 ahead the moment either of you crosses the line. That is
// the difference between "on your tail" and "most of a lap back", and it is a
// sign convention nobody notices going wrong.
TEST_CASE("alongTrackDelta: wraps at start/finish and keeps its sign") {
    // No wrap involved: plain difference, positive when `ahead` is further on.
    CHECK(alongTrackDelta(0.60f, 0.50f) == doctest::Approx(0.10f));
    CHECK(alongTrackDelta(0.50f, 0.60f) == doctest::Approx(-0.10f));
    CHECK(alongTrackDelta(0.50f, 0.50f) == doctest::Approx(0.0f));

    // Across the line, which is where a plain subtraction lies: 0.05 vs 0.95
    // is a twentieth of a lap apart, not nineteen twentieths.
    CHECK(alongTrackDelta(0.05f, 0.95f) == doctest::Approx(0.10f));
    CHECK(alongTrackDelta(0.95f, 0.05f) == doctest::Approx(-0.10f));

    // Exactly half a lap is the boundary, and the comparisons are strict, so
    // it is left alone in both directions rather than flipped. At the opposite
    // side of the track "ahead by half" and "behind by half" are equally true;
    // what the callers need is that the answer is STABLE, not that it picks a
    // favourite — a boundary that flipped sign frame to frame would make a
    // proximity threshold chatter.
    CHECK(alongTrackDelta(0.50f, 0.00f) == doctest::Approx(0.5f));
    CHECK(alongTrackDelta(0.00f, 0.50f) == doctest::Approx(-0.5f));

    // The radar takes the absolute value of this to get "how far apart",
    // which must agree with the signed answer in both directions.
    CHECK(std::abs(alongTrackDelta(0.05f, 0.95f)) ==
          doctest::Approx(std::abs(alongTrackDelta(0.95f, 0.05f))));
}

// The drop shadow behind a string is the configured shadow colour, and it used to be
// written verbatim -- so a HUD that faded a string (the radar fades a rider label out
// as the rider leaves proximity range) got a fully-opaque shadow behind half-visible
// text. That is the one thing that kept the radar on a hand-rolled outline instead of
// the shared shadow every other label uses.
TEST_CASE("modulateAlpha scales alpha by another colour's, keeping RGB") {
    const unsigned long shadow = PU::makeColor(0, 0, 0, 200);

    // Opaque text: the shadow is untouched. This is nearly every string in the plugin,
    // so the shared path has to be a no-op here or the change is not safe to make.
    CHECK(PU::modulateAlpha(shadow, PU::makeColor(1, 2, 3, 255)) == shadow);

    // Faded text: the shadow fades with it, and keeps its own RGB rather than the
    // string's.
    CHECK(PU::modulateAlpha(shadow, PU::makeColor(255, 128, 0, 128))
          == PU::makeColor(0, 0, 0, 200 * 128 / 255));

    // Fully transparent text: no shadow at all, rather than a shadow with nothing on
    // top of it -- which is what a marker just past the radar's range would draw.
    CHECK(PU::modulateAlpha(shadow, PU::makeColor(9, 9, 9, 0)) == PU::makeColor(0, 0, 0, 0));

    // A transparent SHADOW colour stays transparent whatever the text does: the
    // modulation can only ever reduce.
    CHECK(PU::modulateAlpha(PU::makeColor(0, 0, 0, 0), PU::makeColor(1, 1, 1, 255))
          == PU::makeColor(0, 0, 0, 0));
}
