// ============================================================================
// tests/unit/layout_ini_test.cpp
// The layout/theme ini FORMAT, one line at a time.
//
// These files are hand-edited by skinners, and every rule here is a way an edit
// can silently do nothing -- which is the worst failure this file has, because a
// key that was ignored looks exactly like a key that had no visible effect. The
// format used to be exercised only through a real file loaded under Wine.
// ============================================================================
#include "doctest.h"

#include "core/layout_ini.h"

namespace {
    struct Parsed {
        LayoutIniLine kind;
        std::string section, key;
        float value = 0.0f;
    };
    // One line against a carried-over section, the way the file loop does it.
    Parsed feed(const char* line, const char* startSection = "") {
        Parsed p;
        p.section = startSection;
        p.kind = layoutParseIniLine(line, p.section, p.key, p.value);
        return p;
    }
}

TEST_CASE("ini: a property is scoped by the section above it") {
    // The whole reason sections exist: `padding-x` means one thing under [panel]
    // and another under [label], so the property name does not have to carry its
    // own scope (padHCells / bgPaddingHScale, as it was).
    Parsed p = feed("padding-x = 2", "panel");
    CHECK(p.kind == LayoutIniLine::Pair);
    CHECK(p.key == "panel.padding-x");
    CHECK(p.value == doctest::Approx(2.0f));

    p = feed("padding-x = 0.5", "label");
    CHECK(p.key == "label.padding-x");

    // No section yet: passed through bare, so a one-line file needs no header.
    p = feed("padding-x = 2");
    CHECK(p.key == "padding-x");
}

TEST_CASE("ini: a section header replaces the current section") {
    Parsed p = feed("[settings]", "panel");
    CHECK(p.kind == LayoutIniLine::Section);
    CHECK(p.section == "settings");

    // Whitespace anywhere, because a hand-aligned file has it everywhere.
    p = feed("  [  settings  ]   ", "panel");
    CHECK(p.kind == LayoutIniLine::Section);
    CHECK(p.section == "settings");

    // Unterminated: ignored, and the previous section SURVIVES. Adopting a broken
    // header as the section would silently re-scope every key below it.
    p = feed("[settings", "panel");
    CHECK(p.kind == LayoutIniLine::Blank);
    CHECK(p.section == "panel");
}

TEST_CASE("ini: comments, blanks and non-numbers are skipped") {
    CHECK(feed("").kind == LayoutIniLine::Blank);
    CHECK(feed("   \t ").kind == LayoutIniLine::Blank);
    CHECK(feed("; padding-x = 2", "panel").kind == LayoutIniLine::Blank);
    CHECK(feed("   ; commented out", "panel").kind == LayoutIniLine::Blank);
    // The shipped files ship every key commented out, so this is the common case.
    CHECK(feed(";padding-x = 2", "panel").kind == LayoutIniLine::Blank);

    CHECK(feed("padding-x", "panel").kind == LayoutIniLine::Blank);        // no '='
    CHECK(feed("= 2", "panel").kind == LayoutIniLine::Blank);              // no key
    CHECK(feed("padding-x = auto", "panel").kind == LayoutIniLine::Blank); // not a number
    CHECK(feed("padding-x =", "panel").kind == LayoutIniLine::Blank);      // empty value
}

TEST_CASE("ini: values parse the way a person types them") {
    CHECK(feed("padding-x=2", "panel").value == doctest::Approx(2.0f));
    CHECK(feed("padding-x  =  2.5  ", "panel").value == doctest::Approx(2.5f));
    CHECK(feed("border-y = -1", "panel").value == doctest::Approx(-1.0f));   // the auto sentinel
    CHECK(feed("size = 0.026", "frame").value == doctest::Approx(0.026f));
    // Trailing junk after a good number is taken as the number rather than dropped:
    // strtof stops at the first bad character, which makes "2 ; two cells" work.
    CHECK(feed("padding-x = 2 ; two cells", "panel").value == doctest::Approx(2.0f));
}

TEST_CASE("ini: a CRLF file parses identically") {
    // These files are edited on Windows. A \r left on the value would make strtof
    // stop early (harmless) but one left on a SECTION would scope every key under
    // it to "panel\r" and match nothing -- ignored keys, no error, no effect.
    Parsed p = feed("[panel]\r");
    CHECK(p.kind == LayoutIniLine::Section);
    CHECK(p.section == "panel");

    p = feed("padding-x = 2\r", "panel");
    CHECK(p.kind == LayoutIniLine::Pair);
    CHECK(p.key == "panel.padding-x");
    CHECK(p.value == doctest::Approx(2.0f));
}
