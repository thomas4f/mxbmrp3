// ============================================================================
// tests/unit/test_text_wrap.cpp
// Unit tests for hud/settings/text_wrap.h — the settings tooltip box's greedy
// word wrap.
//
// Extracted from a lambda inside SettingsHud::rebuildRenderData(), where it
// wrapped and emitted in the same loop and so could only be exercised by
// building the DLL. The cases below are the ones that loop had no way to state:
// a word longer than the line, the off-by-one that decides whether a line can
// use its full width, and the fact that truncation and an ellipsis are NOT the
// same event (a narrow box drops text without marking it).
//
// test_plugin_utils.cpp provides the doctest impl + main.
// ============================================================================
#include "doctest.h"

#include "hud/settings/text_wrap.h"

using TextWrap::wrap;

TEST_CASE("text shorter than one line comes back untouched") {
    auto w = wrap("Short text", 40, 2);
    REQUIRE(w.lines.size() == 1);
    CHECK(w.lines[0] == "Short text");
    CHECK_FALSE(w.truncated);
}

TEST_CASE("empty text produces no lines") {
    auto w = wrap("", 40, 2);
    CHECK(w.lines.empty());
    CHECK_FALSE(w.truncated);
}

TEST_CASE("wraps on the last space that fits") {
    // Width 10: "The quick" is 9 chars, adding " brown" would overflow.
    auto w = wrap("The quick brown fox", 10, 2);
    REQUIRE(w.lines.size() == 2);
    CHECK(w.lines[0] == "The quick");
    CHECK(w.lines[1] == "brown fox");
    CHECK_FALSE(w.truncated);
}

TEST_CASE("the space consumed by a break is not re-emitted") {
    auto w = wrap("aaa bbb", 3, 2);
    REQUIRE(w.lines.size() == 2);
    CHECK(w.lines[0] == "aaa");
    CHECK(w.lines[1] == "bbb");  // not " bbb"
    CHECK_FALSE(w.truncated);
}

TEST_CASE("a line may use its full width when the break space sits just past it") {
    // The rfind is inclusive of the boundary, so a space at exactly `width`
    // still counts as a clean break and the line keeps all `width` characters.
    auto w = wrap("abcde fgh", 5, 2);
    REQUIRE(w.lines.size() == 2);
    CHECK(w.lines[0] == "abcde");
    CHECK(w.lines[1] == "fgh");
}

TEST_CASE("a word longer than the line is hard-broken") {
    auto w = wrap("supercalifragilistic", 8, 2);
    REQUIRE(w.lines.size() == 2);
    CHECK(w.lines[0] == "supercal");
    CHECK(w.lines[1].size() <= 8);
    CHECK(w.truncated);  // 20 chars cannot fit in 2x8
}

TEST_CASE("no line ever exceeds the requested width") {
    const char* samples[] = {
        "The quick brown fox jumps over the lazy dog",
        "supercalifragilisticexpialidocious",
        "a b c d e f g h i j k l m n o p",
        "  leading and trailing spaces  ",
        "one-really-long-hyphenated-token and then words",
    };
    for (const char* s : samples) {
        for (int width = 1; width <= 30; ++width) {
            auto w = wrap(s, width, 2);
            for (const auto& line : w.lines) {
                INFO("width=" << width << " line=\"" << line << "\" from \"" << s << "\"");
                CHECK(line.size() <= static_cast<size_t>(width));
            }
        }
    }
}

TEST_CASE("overflow past the last line is reported as truncated") {
    auto w = wrap("one two three four five six seven eight nine ten", 10, 2);
    CHECK(w.lines.size() == 2);
    CHECK(w.truncated);
}

TEST_CASE("an overflowing last line ends in an ellipsis") {
    auto w = wrap("one two three four five six seven eight nine ten", 10, 2);
    REQUIRE(w.lines.size() == 2);
    const std::string& last = w.lines[1];
    REQUIRE(last.size() >= 3);
    CHECK(last.compare(last.size() - 3, 3, "...") == 0);
}

TEST_CASE("truncation without an ellipsis is still reported") {
    // The ellipsis needs a line with more than 3 characters to give up. A box
    // this narrow drops text silently — which is exactly why `truncated` is a
    // separate flag rather than something callers sniff for by looking for "...".
    auto w = wrap("ab cd ef gh ij", 2, 2);
    REQUIRE(w.truncated);
    for (const auto& line : w.lines) {
        CHECK(line.find("...") == std::string::npos);
    }
}

TEST_CASE("text that exactly fills the available lines is not truncated") {
    auto w = wrap("aaaa bbbb", 4, 2);
    REQUIRE(w.lines.size() == 2);
    CHECK(w.lines[0] == "aaaa");
    CHECK(w.lines[1] == "bbbb");
    CHECK_FALSE(w.truncated);
}

TEST_CASE("more lines available than needed leaves the extras unused") {
    auto w = wrap("one two three", 5, 8);
    CHECK(w.lines.size() == 3);
    CHECK_FALSE(w.truncated);
}

TEST_CASE("degenerate widths terminate instead of looping") {
    // maxCharsPerLine <= 0 makes the hard-break path consume nothing; only the
    // line budget stops it. Assert it returns rather than hangs.
    for (int width : {0, -1, -100}) {
        auto w = wrap("some text here", width, 2);
        INFO("width=" << width);
        CHECK(w.lines.size() <= 2);
        CHECK(w.truncated);
    }
}

TEST_CASE("a zero line budget yields nothing but reports the loss") {
    auto w = wrap("text", 10, 0);
    CHECK(w.lines.empty());
    CHECK(w.truncated);

    auto empty = wrap("", 10, 0);
    CHECK(empty.lines.empty());
    CHECK_FALSE(empty.truncated);
}
