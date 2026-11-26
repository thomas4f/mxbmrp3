// ============================================================================
// tests/unit/test_text_wrap.cpp
// Unit tests for hud/settings/text_wrap.h — the settings tooltip box's greedy
// word wrap, and wrapNotes (the Updates tab's release notes: whole, wrapped).
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

// ---------------------------------------------------------------------------
// wrapNotes: a release's notes for the Updates tab's scrolling list. The body
// comes from GitHub, so the cases below include text built to break a parser.
// ---------------------------------------------------------------------------

namespace {
std::vector<std::string> texts(const std::vector<TextWrap::NoteLine>& lines) {
    std::vector<std::string> out;
    for (const auto& l : lines) out.push_back(l.text);
    return out;
}
bool allFit(const std::vector<TextWrap::NoteLine>& lines, size_t w) {
    for (const auto& l : lines) if (l.text.size() > w) return false;
    return true;
}
}  // namespace

// The real 1.35.1 release body, abridged but verbatim: the CHANGELOG.md
// section (hard-wrapped at 74 columns) plus what release.yml appends.
static const char* kRealBody =
    "Slide and Wheelie widgets and a regrouped Widgets tab, plus fixes for\n"
    "the timed race clock, Now Playing and the light themes.\n"
    "\n"
    "### Added\n"
    "- **Slide** widget (Settings > Widgets > Slide): an upside-down arc\n"
    "  showing how far the bike slides sideways, filling toward the side the\n"
    "  rear steps out.\n"
    "\n"
    "### Fixed\n"
    "- In a timed race the session clock no longer counts down the pre-start:\n"
    "  it shows the full race time until the gate drops.\n"
    "\n"
    "### Downloads\n"
    "\n"
    "| File | For |\n"
    "|------|-----|\n"
    "| `mxbmrp3-Setup.exe` | Guided installer (recommended) |\n"
    "\n"
    "\xE2\x9C\x85 Built from `60618de` with the full test suite passing \xE2\x80\x94 "
    "[run log](https://github.com/x/y/actions/runs/1).\n";

TEST_CASE("wrapNotes: the real release body reads as items under headings") {
    const auto lines = TextWrap::wrapNotes(kRealBody, 40);
    CHECK(allFit(lines, 40));
    const std::vector<std::string> want = {
        "Slide and Wheelie widgets and a",
        "regrouped Widgets tab, plus fixes for",
        "the timed race clock, Now Playing and",
        "the light themes.",
        "",
        "Added",
        "- Slide widget (Settings > Widgets >",
        "  Slide): an upside-down arc showing how",
        "  far the bike slides sideways, filling",
        "  toward the side the rear steps out.",
        "",
        "Fixed",
        "- In a timed race the session clock no",
        "  longer counts down the pre-start: it",
        "  shows the full race time until the",
        "  gate drops.",
    };
    CHECK(texts(lines) == want);
    REQUIRE(lines.size() == want.size());
    CHECK(lines[5].heading);
    CHECK(lines[11].heading);
    CHECK_FALSE(lines[6].heading);
    CHECK_FALSE(lines[4].heading);
}

TEST_CASE("wrapNotes: without a Downloads heading, tables and rules drop and links keep their text") {
    const auto lines = TextWrap::wrapNotes(
        "| a | b |\n|---|---|\n---\n* * *\n"
        "\xE2\x9C\x85 Built from `abc` \xE2\x80\x94 [run log](https://e/x).\n", 80);
    REQUIRE(lines.size() == 1);
    // The emoji has no glyph and is dropped; the em dash maps to CP1252 0x97.
    CHECK(lines[0].text == "Built from abc \x97 run log.");
}

TEST_CASE("wrapNotes: the download buttons above the notes are dropped") {
    // release.yml opens the body with a line of linked badge images; read as
    // text it would be "!Download - Installer (EXE)" and the like.
    const auto lines = TextWrap::wrapNotes(
        "[![Download Installer (EXE)](https://img.shields.io/badge/a)](https://e/x.exe) "
        "[![Download ZIP](https://img.shields.io/badge/b)](https://e/x.zip)\n"
        "![a bare image](https://e/i.png)\n"
        "\n"
        "An intro line.\n"
        "\n"
        "### Added\n"
        "- A thing.\n", 80);
    CHECK(texts(lines) == std::vector<std::string>{"An intro line.", "", "Added", "- A thing."});
    // Text that merely contains an image further in is still read.
    CHECK(texts(TextWrap::wrapNotes("See ![x](u) here", 80)) == std::vector<std::string>{"See !x here"});
}

TEST_CASE("wrapNotes: markup that never closes stays as typed") {
    CHECK(texts(TextWrap::wrapNotes("a [b c", 80)) == std::vector<std::string>{"a [b c"});
    CHECK(texts(TextWrap::wrapNotes("[x](no close", 80)) == std::vector<std::string>{"[x](no close"});
    CHECK(texts(TextWrap::wrapNotes("[a [b](u) c", 80)) == std::vector<std::string>{"a [b c"});
    CHECK(texts(TextWrap::wrapNotes("]( ) [ ] ( )", 80)) == std::vector<std::string>{"]( ) [ ] ( )"});
    // A lone "*" is not bold; an unpaired "**" just goes.
    CHECK(texts(TextWrap::wrapNotes("2 * 3 **odd", 80)) == std::vector<std::string>{"2 * 3 odd"});
    // A link longer than the search span is left alone rather than searched for.
    const std::string longUrl = "[t](" + std::string(TextWrap::NOTES_LINK_SPAN + 10, 'u') + ")";
    const auto l = TextWrap::wrapNotes(longUrl, 4000);
    REQUIRE(l.size() == 1);
    CHECK(l[0].text == longUrl);
    const std::string longText = "[" + std::string(TextWrap::NOTES_LINK_SPAN + 10, 't') + "](u)";
    const auto t = TextWrap::wrapNotes(longText, 4000);
    REQUIRE(t.size() == 1);
    CHECK(t[0].text == longText);
}

TEST_CASE("wrapNotes: accents and CRLF, control bytes and bad UTF-8") {
    const auto lines = TextWrap::wrapNotes("### Corrig\xC3\xA9\r\n- caf\xC3\xA9\r\n\x01\x02 \xFF\xFE ok\r\n", 80);
    REQUIRE(lines.size() == 2);
    CHECK(lines[0].text == "Corrig\xE9");
    CHECK(lines[0].heading);
    CHECK(lines[1].text == "- caf\xE9 ok");   // the item's next line joins it
    for (const auto& l : lines)
        for (char c : l.text) CHECK(static_cast<unsigned char>(c) >= 0x20);
}

TEST_CASE("wrapNotes: empty, blank-only, heading-only and too narrow") {
    CHECK(TextWrap::wrapNotes("", 40).empty());
    CHECK(TextWrap::wrapNotes("\n\r\n   \n\t\n", 40).empty());
    CHECK(TextWrap::wrapNotes("#\n###\n### \xF0\x9F\x94\x8F\n", 40).empty());   // nothing to title
    CHECK(TextWrap::wrapNotes("### Downloads\n- after", 40).empty());
    CHECK(TextWrap::wrapNotes("- x", 2).empty());
    // The first heading has no empty line above it.
    const auto h = TextWrap::wrapNotes("## [1.36.0]\n### Added\n- y", 40);
    CHECK(texts(h) == std::vector<std::string>{"[1.36.0]", "", "Added", "- y"});
}

TEST_CASE("wrapNotes: a huge body is bounded in bytes and lines, and nothing is cut") {
    // One 200 KB word: hard-broken, never past the line or byte cap.
    const auto word = TextWrap::wrapNotes(std::string(200 * 1024, 'x'), 50);
    CHECK(word.size() == TextWrap::NOTES_MAX_LINES);
    CHECK(allFit(word, 50));
    // A body of nothing but "[": linear, kept as typed.
    const auto brackets = TextWrap::wrapNotes(std::string(100 * 1024, '['), 1000);
    CHECK(brackets.size() == TextWrap::NOTES_MAX_BYTES / 1000 + 1);
    // Thousands of items: capped at NOTES_MAX_LINES.
    std::string many;
    for (int i = 0; i < 5000; ++i) many += "- item\n";
    CHECK(TextWrap::wrapNotes(many, 40).size() == TextWrap::NOTES_MAX_LINES);
    // A short body comes back whole.
    const std::string text = "one two three four five six seven eight nine ten";
    std::string joined;
    for (const auto& l : TextWrap::wrapNotes(text, 12)) {
        CHECK(l.text.size() <= 12u);
        joined += (joined.empty() ? "" : " ") + l.text;
    }
    CHECK(joined == text);
}

TEST_CASE("NotesCache: one parse per release and width, not per rebuild") {
    TextWrap::NotesCache cache;
    int fetches = 0;
    auto fetch = [&fetches]() { ++fetches; return std::string("### Added\n- one\n"); };
    const auto& a = cache.lines(1, 40, fetch);
    REQUIRE(a.size() == 2);
    for (int i = 0; i < 100; ++i) cache.lines(1, 40, fetch);   // a glide's rebuilds
    CHECK(cache.parses() == 1);
    CHECK(fetches == 1);
    cache.lines(1, 30, fetch);                                  // the panel width changed
    CHECK(cache.parses() == 2);
    cache.lines(2, 30, fetch);                                  // new notes were stored
    CHECK(cache.parses() == 3);
    CHECK(fetches == 3);
    // Generation 0 is a real key too: the first call parses even at 0.
    TextWrap::NotesCache fresh;
    CHECK(fresh.lines(0, 40, fetch).size() == 2);
    CHECK(fresh.parses() == 1);
}

TEST_CASE("wrap: a long text with no spaces breaks at the width, in linear time") {
    const std::string word(200 * 1024, 'x');
    const auto w = wrap(word, 50, static_cast<int>(word.size()) + 1);
    REQUIRE(w.lines.size() == word.size() / 50);
    CHECK(w.lines.front().size() == 50u);
    CHECK_FALSE(w.truncated);
    // A space at the line's very start is not a break (the old lastSpace > lineStart).
    const auto lead = wrap(" abcdef", 3, 10);
    CHECK(lead.lines.front() == " ab");
}
