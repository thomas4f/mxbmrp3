// ============================================================================
// tests/unit/test_i18n.cpp
// Unit tests for core/i18n.h and core/i18n_catalog.h -- translating what the
// plugin shows. Pins the four things a language pack depends on:
//   - the .po reader takes what Poedit and msgmerge write (contexts, multi-line
//     strings, escapes, a BOM) and leaves out what must not show: the header,
//     untranslated, obsolete and FUZZY entries (a fuzzy one is a translation of
//     English that has since changed);
//   - a translation the CP1252 font cannot draw is dropped whole, so the player
//     sees English rather than a line with holes in it;
//   - TR() re-resolves after a language change and costs a compare otherwise,
//     falling back to English per string;
//   - the pseudo-locale keeps printf conversions and {placeholders} intact, so a
//     developer can run the whole plugin in it.
// Plus the settings Find box's side: accent-folded matching, the English label
// indexed beside the translated one, and the Visible row found by its ENGLISH
// label (it was found by text, which a translation would have broken).
// The shipped data is checked too: template.pot parses, holds every tooltip
// (a tooltip added without NC_() would stay English forever), and every
// shipped pack's strings are ones the template still has, with each translated
// tooltip fitting the same two-line box the English must, and each translated
// write-failure notice fitting the toast's detail line with its file name in
// (a review of #635 found eight packs cut mid-word). The loader's file I/O
// (i18n.cpp) is Windows-only and is the integration layer's.
// ============================================================================
#include "doctest.h"

#include "core/i18n.h"
#include "core/achievement_text.h"
#include "core/event_log_types.h"
#include "hud/settings/settings_find.h"
#include "core/tooltip_manager.h"
#include "core/layout_metrics.h"
#include "core/system_messages.h"
#include "hud/settings/text_wrap.h"

#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

#ifndef LANGUAGES_DIR
#error "LANGUAGES_DIR must be defined by the build (see tests/unit/CMakeLists.txt)"
#endif

namespace {

std::string readAll(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// TR() from one call site, so a test can call the SAME slot twice.
const char* trShowGap() { return TR("Show gap"); }
const char* trcCut() { return TRC("director", "Cut"); }

// Every test that activates a catalogue puts English back.
struct EnglishAfter {
    ~EnglishAfter() { I18n::activate(nullptr); }
};

}  // namespace

TEST_CASE("parsePo: entries, contexts, continuation lines and escapes") {
    const std::string po =
        "# translator comment\n"
        "msgid \"\"\n"
        "msgstr \"\"\n"
        "\"Content-Type: text/plain; charset=UTF-8\\n\"\n"
        "\n"
        "#. TRANSLATORS: max 9 characters\n"
        "msgid \"Off\"\n"
        "msgstr \"Av\"\n"
        "\n"
        "msgctxt \"director\"\n"
        "msgid \"Cut\"\n"
        "msgstr \"Klipp\"\n"
        "\n"
        "msgid \"\"\n"
        "\"Two \"\n"
        "\"lines\"\n"
        "msgstr \"\"\n"
        "\"Deux\\n\"\n"
        "\"lignes \\\"ici\\\" \\\\ \\t.\"\n"
        "\n"
        "msgid \"Untranslated\"\n"
        "msgstr \"\"\n";
    const auto entries = I18n::parsePo(po);
    REQUIRE(entries.size() == 3);
    CHECK(entries[0].english == "Off");
    CHECK(entries[0].text == "Av");
    CHECK(entries[0].context.empty());
    CHECK(entries[1].context == "director");
    CHECK(entries[1].text == "Klipp");
    CHECK(entries[2].english == "Two lines");
    CHECK(entries[2].text == "Deux\nlignes \"ici\" \\ \t.");
}

TEST_CASE("parsePo: fuzzy is flagged, obsolete and plural extras are left out, a BOM is skipped") {
    const std::string po =
        "\xEF\xBB\xBFmsgid \"First\"\n"
        "msgstr \"Premier\"\n"
        "\n"
        "#, fuzzy, c-format\n"
        "msgid \"%d laps\"\n"
        "msgstr \"%d tours\"\n"
        "\n"
        "#~ msgid \"Gone\"\n"
        "#~ msgstr \"Parti\"\n"
        "\n"
        "msgid \"%d rider\"\n"
        "msgid_plural \"%d riders\"\n"
        "msgstr[0] \"%d pilote\"\n"
        "msgstr[1] \"%d pilotes\"\n"
        "   \n"
        "msgid \"Trailing\"   \r\n"
        "msgstr \"Fin\"";   // no final newline
    const auto entries = I18n::parsePo(po);
    REQUIRE(entries.size() == 4);
    CHECK(entries[0].english == "First");
    CHECK_FALSE(entries[0].fuzzy);
    CHECK(entries[1].fuzzy);
    CHECK(entries[2].english == "%d rider");
    CHECK(entries[2].text == "%d pilote");
    CHECK(entries[3].text == "Fin");
}

TEST_CASE("utf8ToCp1252Exact: Western European converts, anything else refuses") {
    std::string out;
    CHECK(I18n::utf8ToCp1252Exact("\xC3\xA9" "cart \xC3\xA5" "\xC3\xA4" "\xC3\xB6\n\xE2\x82\xAC", out));
    CHECK(out == "\xE9" "cart \xE5\xE4\xF6\n\x80");
    // Polish l-stroke has no CP1252 byte: the entry must fall back, not lose it.
    CHECK_FALSE(I18n::utf8ToCp1252Exact("Pr\xC4\x99" "dko\xC5\x9B\xC4\x87", out));
    // Spacing is the translator's: kept as written (ChatText's chat filter trims).
    CHECK(I18n::utf8ToCp1252Exact("  a  b ", out));
    CHECK(out == "  a  b ");
}

TEST_CASE("loadPo: counts what it loaded, what was fuzzy and what the font cannot draw") {
    I18n::Catalog cat;
    const I18n::LoadStats s = I18n::loadPo(
        "msgid \"Off\"\nmsgstr \"D\xC3\xA9sactiv\xC3\xA9\"\n\n"
        "#, fuzzy\nmsgid \"On\"\nmsgstr \"Activ\xC3\xA9\"\n\n"
        "msgid \"Gap\"\nmsgstr \"Odst\xC4\x99p\"\n", cat);
    CHECK(s.loaded == 1);
    CHECK(s.fuzzy == 1);
    CHECK(s.unrenderable == 1);
    REQUIRE(cat.find(nullptr, "Off") != nullptr);
    CHECK(std::string(cat.find(nullptr, "Off")) == "D\xE9sactiv\xE9");
    CHECK(cat.find(nullptr, "On") == nullptr);
    CHECK(cat.find(nullptr, "Gap") == nullptr);
}

// A player's pack in Documents never passes msgfmt --check, and a translation is
// handed to snprintf with the ENGLISH's arguments: one "%s" for a "%d" reads an
// int as a pointer. Each wrong shape must fall back to the English at load.
TEST_CASE("loadPo: a translation whose printf codes differ from the English is dropped") {
    I18n::Catalog cat;
    const I18n::LoadStats s = I18n::loadPo(
        "msgid \"%d laps\"\nmsgstr \"%s varv\"\n\n"                 // wrong conversion
        "msgid \"Lap %d of %d\"\nmsgstr \"Varv %d\"\n\n"            // one missing
        "msgid \"%d riders\"\nmsgstr \"%d f\xC3\xB6rare %d\"\n\n"   // one extra
        "msgid \"%d%% done\"\nmsgstr \"%d %% klart\"\n\n"           // %% is no argument
        "msgid \"%.1f s\"\nmsgstr \"%4.1f sek\"\n\n"                // width/precision are free
        "msgid \"%ld ms\"\nmsgstr \"%d ms\"\n", cat);               // length modifier is not
    CHECK(s.badFormat == 4);
    CHECK(s.loaded == 2);
    CHECK(cat.find(nullptr, "%d laps") == nullptr);
    CHECK(cat.find(nullptr, "Lap %d of %d") == nullptr);
    CHECK(cat.find(nullptr, "%d riders") == nullptr);
    CHECK(cat.find(nullptr, "%ld ms") == nullptr);
    REQUIRE(cat.find(nullptr, "%d%% done") != nullptr);
    CHECK(std::string(cat.find(nullptr, "%d%% done")) == "%d %% klart");
    CHECK(cat.find(nullptr, "%.1f s") != nullptr);
}

// A pack is a shared file, so the check has to hold against bytes chosen to get
// past it. C0 A5 is an OVERLONG UTF-8 '%': the check used to read the UTF-8,
// saw no '%', and the CP1252 text handed to snprintf had three extra "%s" -- a
// crash on the first draw, from a pack the System language picks up by itself.
TEST_CASE("loadPo: an overlong '%' cannot smuggle printf codes past the check") {
    I18n::Catalog cat;
    const I18n::LoadStats s = I18n::loadPo(
        "msgid \"%s fastest lap\"\nmsgstr \"%s \xC0\xA5s\xC0\xA5s\xE0\x80\xA5n\"\n", cat);
    CHECK(s.loaded == 0);
    CHECK(cat.find(nullptr, "%s fastest lap") == nullptr);
}

TEST_CASE("decodeUtf8: overlong forms, surrogates and code points past U+10FFFF are invalid") {
    using ChatText::decodeUtf8;
    CHECK(decodeUtf8("\xC0\xA5") == std::vector<uint32_t>{ 0xFFFD, 0xFFFD });          // overlong '%'
    CHECK(decodeUtf8("\xC1\xBF")[0] == 0xFFFD);                                        // overlong DEL
    CHECK(decodeUtf8("\xE0\x80\xA5")[0] == 0xFFFD);                                    // 3-byte overlong
    CHECK(decodeUtf8("\xF0\x80\x80\xA5")[0] == 0xFFFD);                                // 4-byte overlong
    CHECK(decodeUtf8("\xED\xA0\x80")[0] == 0xFFFD);                                    // surrogate
    CHECK(decodeUtf8("\xF4\x90\x80\x80")[0] == 0xFFFD);                                // past U+10FFFF
    // The smallest legal form of each length still decodes.
    CHECK(decodeUtf8("\xC2\x80\xE0\xA0\x80\xF0\x90\x80\x80") ==
          std::vector<uint32_t>{ 0x80, 0x800, 0x10000 });
}

// snprintf truncates its OUTPUT but generates every character first, so a field
// of a billion freezes the game on the draw. Two digits is all a label needs.
TEST_CASE("loadPo: a width or precision of three digits or more is dropped") {
    I18n::Catalog cat;
    const I18n::LoadStats s = I18n::loadPo(
        "msgid \"%.1f laps\"\nmsgstr \"%.999999999f varv\"\n\n"
        "msgid \"%d riders\"\nmsgstr \"%999999999d f\xC3\xB6rare\"\n\n"
        "msgid \"%s ended\"\nmsgstr \"%-100s slut\"\n\n"
        "msgid \"Lap %d\"\nmsgstr \"Varv %-12d\"\n", cat);
    CHECK(s.badFormat == 3);
    CHECK(cat.find(nullptr, "%.1f laps") == nullptr);
    CHECK(cat.find(nullptr, "%d riders") == nullptr);
    CHECK(cat.find(nullptr, "%s ended") == nullptr);
    CHECK(cat.find(nullptr, "Lap %d") != nullptr);
}

TEST_CASE("limits: '#. max N' notes are read, a longer line shows English, the pseudo-locale stops at the room") {
    const std::string pot =
        "#. max 9 characters\n"
        "msgid \"Auto-save\"\n"
        "msgstr \"\"\n"
        "\n"
        "#. TRANSLATORS: unrelated\n"
        "msgid \"Grid snap\"\n"
        "msgstr \"\"\n"
        "\n"
        "#. max 4 characters\n"
        "msgctxt \"tooltip\"\n"
        "msgid \"Ignored\"\n"
        "msgstr \"\"\n";
    const auto limits = I18n::parseLimits(pot);
    CHECK(limits.size() == 1);
    CHECK(limits.at("Auto-save") == 9);

    I18n::Catalog cat;
    cat.setLimits(limits);
    const std::string po =
        "msgid \"Auto-save\"\nmsgstr \"Spara automatiskt\"\n\n"
        "msgid \"Grid snap\"\nmsgstr \"Rutn\xC3\xA4t\"\n";
    const I18n::LoadStats st = I18n::loadPo(po, cat);
    CHECK(st.tooLong == 1);
    CHECK(st.loaded == 1);
    CHECK(cat.find(nullptr, "Auto-save") == nullptr);   // English shows
    CHECK(cat.find(nullptr, "Grid snap") != nullptr);

    // CP1252 bytes are what is counted: one accented letter is one character.
    I18n::Catalog fits;
    fits.setLimits({ { "Auto-save", 9 } });
    CHECK(I18n::loadPo("msgid \"Auto-save\"\nmsgstr \"Autospar\xC3\xA5\"\n", fits).tooLong == 0);

    I18n::Catalog pseudo(true);
    pseudo.setLimits(limits);
    CHECK(std::strlen(pseudo.find(nullptr, "Auto-save")) == 9);
    CHECK(std::strlen(pseudo.find(nullptr, "Grid snap")) > 9);   // no limit: stretched in full
}

TEST_CASE("shipped: template.pot carries a '#. max N' room for every settings label measured") {
    const std::string pot = readAll(std::filesystem::path(LANGUAGES_DIR) / "template.pot");
    const auto limits = I18n::parseLimits(pot);
    CHECK(limits.size() > 200);
    CHECK(limits.count("Auto-save") == 1);
}

TEST_CASE("Catalog: a pack layered over its base wins; contexts are separate entries") {
    I18n::Catalog cat;
    cat.add("", "Off", "Av");
    cat.add("director", "Cut", "Klipp");
    cat.add("", "Off", "Avst\xE4ngd");   // the pack over its base
    CHECK(std::string(cat.find(nullptr, "Off")) == "Avst\xE4ngd");
    CHECK(cat.find(nullptr, "Cut") == nullptr);
    CHECK(std::string(cat.find("director", "Cut")) == "Klipp");
    CHECK(cat.size() == 2);
    CHECK_FALSE(cat.isPseudo());
}

TEST_CASE("TR: English with no language, the translation after a switch, per string fallback") {
    EnglishAfter restore;
    I18n::activate(nullptr);
    CHECK(std::string(trShowGap()) == "Show gap");
    CHECK(std::string(trcCut()) == "Cut");

    I18n::Catalog sv;
    sv.add("", "Show gap", "Visa gap");
    I18n::activate(&sv);
    // The same call site re-resolves because the generation moved.
    CHECK(std::string(trShowGap()) == "Visa gap");
    // Not in the pack: that string alone stays English.
    CHECK(std::string(trcCut()) == "Cut");
    CHECK(std::string(I18n::tr("Something else")) == "Something else");
    CHECK(std::string(I18n::trc("director", "Cut")) == "Cut");

    // Cached: a later edit to the catalogue is not seen until the next switch.
    // That is the whole cost model -- one compare, no lookup -- pinned here.
    const char* first = trShowGap();
    CHECK(trShowGap() == first);

    I18n::activate(nullptr);
    CHECK(std::string(trShowGap()) == "Show gap");
    CHECK(I18n::tr(nullptr) == nullptr);
}

// A HUD walking a table of N_() names (Timing's rows, the Dash's pages, the
// reference notes) keeps one Slot per entry: translated after a switch, then a
// compare per rebuild -- the same cost model as TR() at a call site.
TEST_CASE("trEntry: a table entry's slot follows the language and caches between switches") {
    EnglishAfter restore;
    static const char* const kNames[] = { N_("Last lap"), N_("Server best") };
    I18n::Slot slots[2] = {};
    I18n::activate(nullptr);
    CHECK(std::string(I18n::trEntry(slots[0], kNames[0])) == "Last lap");

    I18n::Catalog sv;
    sv.add("", "Last lap", "Senaste varv");
    I18n::activate(&sv);
    CHECK(std::string(I18n::trEntry(slots[0], kNames[0])) == "Senaste varv");
    CHECK(std::string(I18n::trEntry(slots[1], kNames[1])) == "Server best");   // per entry
    // Cached in the slot: within one language a second call answers from it
    // without a lookup (a planted answer comes back), so a frame costs no hash.
    CHECK(slots[0].generation == I18n::g_generation);
    static const char kPlanted[] = "planted";
    slots[0].text = kPlanted;
    CHECK(I18n::trEntry(slots[0], kNames[0]) == kPlanted);

    I18n::activate(nullptr);   // a switch moves the generation: looked up again
    CHECK(std::string(I18n::trEntry(slots[0], kNames[0])) == "Last lap");
}

TEST_CASE("trCached: English passes through; a language is looked up once per switch") {
    EnglishAfter restore;
    static const char* const kTesting = N_("Testing");
    static const char* const kOnline = N_("Online");
    I18n::activate(nullptr);
    CHECK(I18n::trCached(kTesting) == kTesting);   // the very pointer, no lookup

    I18n::Catalog sv;
    sv.add("", "Testing", "Testk\xF6rning");
    I18n::activate(&sv);
    CHECK(std::string(I18n::trCached(kTesting)) == "Testk\xF6rning");
    CHECK(I18n::trCached(kOnline) == kOnline);     // untranslated: its English

    // Cached by pointer: a translation added after the first sight is not seen
    // until the next switch, which is what proves the steady state skips tr().
    sv.add("", "Online", "Uppkopplad");
    CHECK(I18n::trCached(kOnline) == kOnline);
    I18n::activate(&sv);
    CHECK(std::string(I18n::trCached(kOnline)) == "Uppkopplad");

    I18n::activate(nullptr);
    CHECK(I18n::trCached(kTesting) == kTesting);
}

TEST_CASE("matchSystemLanguage: exact folder, else the language part, never the pseudo-locale") {
    const std::vector<I18n::LanguageInfo> packs = {
        { "sv", "Svenska", false }, { "fr", "Fran\xE7" "ais", false },
        { "fr-CA", "Qu\xE9" "bec", false }, { "pseudo", "Pseudo (test)", true },
    };
    CHECK(I18n::matchSystemLanguage("sv-SE", packs) == "sv");
    CHECK(I18n::matchSystemLanguage("sv-FI", packs) == "sv");
    CHECK(I18n::matchSystemLanguage("fr-ca", packs) == "fr-CA");   // case-insensitive, exact wins
    CHECK(I18n::matchSystemLanguage("fr-FR", packs) == "fr");
    CHECK(I18n::matchSystemLanguage("en-US", packs).empty());      // English
    CHECK(I18n::matchSystemLanguage("pseudo", packs).empty());
    CHECK(I18n::matchSystemLanguage("", packs).empty());
    CHECK(I18n::matchSystemLanguage("sv", {}).empty());
}

// `base` is a pack NAME. Joined onto the languages folder as written, it read
// any strings.po a relative path could reach.
TEST_CASE("baseFolder: only an installed pack's folder, never a path or the pack itself") {
    std::vector<I18n::LanguageInfo> packs(2);
    packs[0].folder = "pt";
    packs[1].folder = "sv";
    CHECK(I18n::baseFolder("pt", "pt-pt", packs) == "pt");
    CHECK(I18n::baseFolder("..\\..\\..\\Users\\x", "sv", packs).empty());
    CHECK(I18n::baseFolder("../sv", "pt", packs).empty());
    CHECK(I18n::baseFolder("sv\\..\\..\\x", "pt", packs).empty());
    CHECK(I18n::baseFolder("sv", "sv", packs).empty());
    CHECK(I18n::baseFolder("de", "sv", packs).empty());
    CHECK(I18n::baseFolder("", "sv", packs).empty());
}

TEST_CASE("EventText: English for the web overlay, the language shown for the HUDs") {
    EnglishAfter restore;
    I18n::activate(nullptr);
    EventText plain(N_("Final lap"));
    CHECK(std::string(plain.english) == "Final lap");
    CHECK(std::string(plain.shown) == "Final lap");

    I18n::Catalog sv;
    sv.add("", "Final lap", "Sista varvet");
    sv.add("", "%s retired", "%s bröt");
    I18n::activate(&sv);
    EventText lap(N_("Final lap"));
    CHECK(std::string(lap.english) == "Final lap");
    CHECK(std::string(lap.shown) == "Sista varvet");
    EventText out;
    out.format(N_("%s retired"), "#42");
    CHECK(std::string(out.english) == "#42 retired");
    CHECK(std::string(out.shown) == "#42 bröt");
    EventText other;   // not in the pack: its English, both times
    other.format(N_("%s finished"), "#7");
    CHECK(std::string(other.shown) == "#7 finished");
}

TEST_CASE("achievements: the toast and row sentences follow the language, English unchanged") {
    EnglishAfter restore;
    const Achievements::Entry* e = Achievements::findById("distance");
    REQUIRE(e);
    char buf[96];
    I18n::activate(nullptr);
    Achievements::formatDescription(*e, 1, buf, sizeof(buf));
    const std::string english = buf;
    CHECK(english.find("Ride ") == 0);

    I18n::Catalog sv;
    sv.add("", "Ride %s in total", "Kör %s totalt");
    sv.add("", "Long Hauler", "Långfärdare");
    I18n::activate(&sv);
    Achievements::formatDescription(*e, 1, buf, sizeof(buf));
    CHECK(std::string(buf) == "Kör " + english.substr(5, english.size() - 5 - 9) + " totalt");
    CHECK(std::string(I18n::trCached(e->title)) == "Långfärdare");
    CHECK(std::string(I18n::trCached(Achievements::tierName(1))) == "Bronze");   // untranslated: English
}

TEST_CASE("englishOf: the English a shown string came from, by pointer") {
    EnglishAfter restore;
    const char* plain = "Not ours";
    CHECK(I18n::englishOf(plain) == plain);   // English active: itself
    I18n::Catalog fr;
    fr.add("", "Show gap", "Afficher l'\xE9" "cart");
    I18n::activate(&fr);
    const char* shown = trShowGap();
    REQUIRE(std::string(shown) != "Show gap");
    CHECK(std::string(I18n::englishOf(shown)) == "Show gap");
    CHECK(I18n::englishOf(plain) == plain);   // not one of ours: itself
}

TEST_CASE("pseudoLocalize: bracketed, accented, longer; printf and {names} untouched") {
    CHECK(I18n::pseudoLocalize("Show gap") == "[\x8Ah\xF6w g\xE5p~~~]");
    CHECK(I18n::pseudoLocalize("%d laps") == "[%d l\xE5p\x9A~~]");
    CHECK(I18n::pseudoLocalize("%-5.1f%% done") == "[%-5.1f%% d\xF6\xF1\xE9~~]");
    CHECK(I18n::pseudoLocalize("{rider} ahead") == "[{rider} \xE5h\xE9\xE5" "d~~]");
    CHECK(I18n::pseudoLocalize("") == "[]");
    // An unterminated placeholder ends the string rather than reading past it.
    CHECK(I18n::pseudoLocalize("Lap {n") == "[L\xE5p {n~~]");
    // A trailing lone % is copied, not run past.
    CHECK(I18n::pseudoLocalize("50%") == "[50%~]");
}

TEST_CASE("the pseudo catalogue translates everything, through TR too") {
    EnglishAfter restore;
    I18n::Catalog pseudo(true);
    CHECK(pseudo.isPseudo());
    I18n::activate(&pseudo);
    const std::string shown = trShowGap();
    CHECK(shown == I18n::pseudoLocalize("Show gap"));
    CHECK(std::string(I18n::englishOf(trShowGap())) == "Show gap");
    // Asked twice, one stored string (pointers handed out must stay put).
    CHECK(pseudo.find(nullptr, "Off") == pseudo.find(nullptr, "Off"));
}

TEST_CASE("Find: case- and accent-insensitive, so a plain keyboard finds an accented label") {
    using SettingsFind::containsWord;
    CHECK(containsWord("\xC9" "cart", "ecart", 5));     // Écart
    CHECK(containsWord("R\xE9" "f\xE9rence", "REFERENCE", 9));
    CHECK(containsWord("Inst\xE4llningar", "installningar", 13));
    CHECK(containsWord("\x8A" "how", "show", 4));       // Š
    CHECK(SettingsFind::foldChar(0xD7) == '*');          // × is not a letter
    CHECK(SettingsFind::foldChar(0x9F) == 'y');
    CHECK(SettingsFind::foldChar(0x8E) == 'z');
    CHECK(SettingsFind::foldChar(0x8C) == 'o');
    CHECK(SettingsFind::foldChar(0x80) == 0x80);         // € is left alone
    CHECK_FALSE(containsWord("Gap", "gip", 3));
}

TEST_CASE("Find: a translated row is found by its English too, and Visible by its English label") {
    EnglishAfter restore;
    I18n::Catalog sv;
    sv.add("", "Show gap", "Visa gap");
    sv.add("", "Visible", "Synlig");
    I18n::activate(&sv);

    SettingsFind::State st;
    st.indexing = true;
    st.addTab(3, "Gap Bar", "gap_bar", "");
    st.addRow(3, TR("Visible"), "common.visible", "");
    st.noteValue("On");
    st.addRow(3, trShowGap(), "gap_bar.show_gap", "");
    char formatted[16];
    std::snprintf(formatted, sizeof(formatted), "%s", "Width");
    st.addRow(3, formatted, "gap_bar.width", "");   // drawn untranslated

    REQUIRE(st.index.size() == 4);
    CHECK(std::string(st.index[1].label) == "Synlig");
    CHECK(std::string(st.index[1].labelEnglish) == "Visible");
    CHECK(st.index[3].labelEnglish == nullptr);      // never a pointer into a stack buffer

    st.tabVisible(0);
    CHECK(std::string(st.index[0].value) == "On");

    std::vector<int> hits;
    SettingsFind::match(st.index, "show gap", hits);
    REQUIRE(hits.size() == 1);
    CHECK(hits[0] == 2);
    SettingsFind::match(st.index, "visa", hits);
    REQUIRE(hits.size() == 1);
    CHECK(hits[0] == 2);
}

namespace {
// The template's entries as context + EOT + English. Its entries are all
// untranslated, so parsePo would return none: fill each msgstr first.
std::set<std::string> templateIds() {
    const std::filesystem::path pot = std::filesystem::path(LANGUAGES_DIR) / "template.pot";
    REQUIRE(std::filesystem::exists(pot));
    std::string filled;
    std::istringstream in(readAll(pot));
    std::string line;
    while (std::getline(in, line)) {
        filled += (line == "msgstr \"\"") ? "msgstr \"x\"" : line;
        filled += '\n';
    }
    std::set<std::string> ids;
    for (const I18n::PoEntry& e : I18n::parsePo(filled)) ids.insert(e.context + '\x04' + e.english);
    return ids;
}
}  // namespace

TEST_CASE("shipped: every tooltip is in the translation template, under the tooltip context") {
    const std::set<std::string> ids = templateIds();
    int checked = 0;
    for (const auto& tip : TooltipManager::allTooltips()) {
        CHECK_MESSAGE(ids.count("tooltip\x04" + tip.second) == 1,
                      "tooltip '" << tip.first << "' is not in template.pot: mark it NC_(\"tooltip\", ...) "
                      "and run tools/i18n/pot.sh --update");
        ++checked;
    }
    CHECK(checked > 100);
}

TEST_CASE("tooltips: the getters translate, under the tooltip context only") {
    EnglishAfter restore;
    const char* english = TooltipManager::getInstance().getControlTooltip("general.auto_save");
    REQUIRE(english[0] != '\0');
    I18n::Catalog sv;
    sv.add("tooltip", english, "Sparar automatiskt.");
    sv.add("", english, "Wrong context");
    I18n::activate(&sv);
    CHECK(std::string(TooltipManager::getInstance().getControlTooltip("general.auto_save")) ==
          "Sparar automatiskt.");
    // A tab's tooltip untranslated: its English.
    const std::string generalTab = TooltipManager::getInstance().getTabTooltip("general");
    CHECK_FALSE(generalTab.empty());
    I18n::activate(nullptr);
    CHECK(generalTab == TooltipManager::getInstance().getTabTooltip("general"));
    CHECK(std::string(TooltipManager::getInstance().getControlTooltip("no.such.id")).empty());
}

TEST_CASE("Find: a translated tooltip is matched in both languages") {
    EnglishAfter restore;
    const char* english = TooltipManager::getInstance().getControlTooltip("general.auto_save");
    I18n::Catalog sv;
    sv.add("tooltip", english, "Sparar aldrig under k\xF6rning.");
    I18n::activate(&sv);
    SettingsFind::State st;
    st.indexing = true;
    st.addRow(0, "Auto-save", "general.auto_save",
              TooltipManager::getInstance().getControlTooltip("general.auto_save"));
    std::vector<int> hits;
    SettingsFind::match(st.index, "aldrig korning", hits);
    CHECK(hits.size() == 1);
    SettingsFind::match(st.index, "riding", hits);   // a word of the English tooltip
    CHECK(hits.size() == 1);
    SettingsFind::match(st.index, "zebra", hits);
    CHECK(hits.empty());
}

// The same two-line box the English is held to (test_tooltip_length.cpp).
static bool tooltipFits(const std::string& cp1252) {
    const int perLine = LayoutMetrics().settingsTooltipCharsPerLine(/*themedCard=*/true);
    return !TextWrap::wrap(cp1252, perLine, TextWrap::TOOLTIP_LINES).truncated;
}

// The shipped-pack loop below runs this check only once a pack has strings, and
// the pseudo pack has none: pin that it can fail at all.
TEST_CASE("shipped: the tooltip box check rejects a translation that needs a third line") {
    const std::string shipped = I18n::trc("tooltip", "Language of the menus and HUDs. Anything a pack does not translate stays English.");
    CHECK(tooltipFits(shipped));
    std::string longer;
    for (int i = 0; i < 4; ++i) longer += shipped + " ";
    CHECK_FALSE(tooltipFits(longer));
}

TEST_CASE("shipped: template.pot parses and every shipped pack translates strings it still has") {
    namespace fs = std::filesystem;
    const fs::path root(LANGUAGES_DIR);
    const std::set<std::string> ids = templateIds();
    CHECK(ids.size() > 10);
    CHECK(ids.count(std::string("\x04") + "Language") == 1);

    const auto limits = I18n::parseLimits(readAll(root / "template.pot"));
    int packs = 0;
    for (const fs::directory_entry& d : fs::directory_iterator(root)) {
        if (!d.is_directory()) continue;
        ++packs;
        REQUIRE_MESSAGE(fs::exists(d.path() / "language.ini"), d.path().string() << " has no language.ini");
        const fs::path po = d.path() / "strings.po";
        if (!fs::exists(po)) {
            // Only the pseudo-locale may ship without strings: it makes its own.
            CHECK_MESSAGE(readAll(d.path() / "language.ini").find("pseudo = 1") != std::string::npos,
                          d.path().string() << " has no strings.po");
            continue;
        }
        for (const I18n::PoEntry& e : I18n::parsePo(readAll(po))) {
            CHECK_MESSAGE(ids.count(e.context + '\x04' + e.english) == 1,
                          po.string() << " translates '" << e.english
                          << "', which the source no longer has (run msgmerge against template.pot)");
            std::string cp;
            CHECK_MESSAGE(I18n::utf8ToCp1252Exact(e.text, cp),
                          po.string() << ": '" << e.text << "' holds a character the font cannot draw");
            // What loadPo would drop, so a shipped line that shows English is caught here.
            CHECK_MESSAGE(I18n::formatSignature(e.text) == I18n::formatSignature(e.english),
                          po.string() << ": '" << e.text << "' has other printf codes than '" << e.english << "'");
            if (e.context.empty()) {
                const auto lim = limits.find(e.english);
                CHECK_MESSAGE((lim == limits.end() || static_cast<int>(cp.size()) <= lim->second),
                              po.string() << ": '" << e.text << "' is longer than the "
                              << (lim == limits.end() ? 0 : lim->second) << " characters '" << e.english << "' has");
            }
            if (e.context == "tooltip") {
                CHECK_MESSAGE(tooltipFits(cp),
                              po.string() << ": the tooltip '" << e.text << "' does not fit two lines");
            }
        }
    }
    CHECK(packs >= 1);
}

// The notices that put a file name into the toast's detail line
// (SystemMessages::formatWriteFailure, the settings/stats save notices). The
// detail is a fixed char array and snprintf cuts what does not fit, mid-word, so
// every pack's line must fit it with the longest name it is given filled in. The
// name is what the line is about: a cut that loses it loses the notice.
TEST_CASE("shipped: every pack's write-failure notice fits the toast with its file name") {
    namespace fs = std::filesystem;
    const char* const formats[] = {
        "%s is read-only. Untick it in Properties.",
        "Windows blocked %s. Antivirus or OneDrive?",
        "Could not write %s. Details in the log.",
    };
    const std::string longestName = "mxbmrp3_settings.ini";   // vs mxbmrp3_stats.json
    constexpr size_t room = sizeof(SystemMessages::Toast{}.detail) - 1;
    const auto filled = [&](const std::string& text) {
        std::string out = text;
        const size_t at = out.find("%s");
        if (at != std::string::npos) out.replace(at, 2, longestName);
        return out;
    };
    for (const char* f : formats) CHECK(filled(f).size() <= room);   // the English too

    const std::set<std::string> wanted(std::begin(formats), std::end(formats));
    int checked = 0;
    for (const fs::directory_entry& d : fs::directory_iterator(fs::path(LANGUAGES_DIR))) {
        const fs::path po = d.path() / "strings.po";
        if (!d.is_directory() || !fs::exists(po)) continue;
        for (const I18n::PoEntry& e : I18n::parsePo(readAll(po))) {
            if (!e.context.empty() || wanted.count(e.english) == 0) continue;
            std::string cp;
            REQUIRE(I18n::utf8ToCp1252Exact(e.text, cp));
            ++checked;
            CHECK_MESSAGE(filled(cp).size() <= room,
                          po.string() << ": '" << e.text << "' is " << filled(cp).size()
                          << " characters with the file name in; the toast holds " << room);
        }
    }
    CHECK(checked >= 3);   // a pack translates them: the check is not vacuous
}

// Labels whose room is FIXED IN CODE, not measured in the panel, so the limits
// fixture (i18n_limits.json, from the drawn settings labels) never sees them:
// a value cut to its cell by formatValue, a button box sized by its idle label
// while the armed "Confirm?" takes the same box, a char array a HUD formats
// into. Each site is named, so a wider box or buffer moves the number here too.
TEST_CASE("shipped: every pack's code-sized labels fit their boxes") {
    namespace fs = std::filesystem;
    struct Room { const char* english; int chars; const char* where; };
    const Room rooms[] = {
        { "On",  3, "Widgets tab inline cycles, formatValue(value, 3)" },
        { "Off", 3, "Widgets tab inline cycles, formatValue(value, 3)" },
        { "Confirm?", 8, "Copy button box (settings_tab_general.cpp, labelChars 8)" },
        { "Rebind in Settings > Hotkeys", 63, "Dash hint, char where[64] (dash_hud_pages.cpp)" },
        { "in Settings > Hotkeys", 63, "Dash hint, char where[64] (dash_hud_pages.cpp)" },
        { "UPDATE AVAILABLE", 31, "Dash startup card, char name[32] (dash_hud_pages.cpp)" },
    };
    int checked = 0;
    for (const fs::directory_entry& d : fs::directory_iterator(fs::path(LANGUAGES_DIR))) {
        const fs::path po = d.path() / "strings.po";
        if (!d.is_directory() || !fs::exists(po)) continue;
        for (const I18n::PoEntry& e : I18n::parsePo(readAll(po))) {
            if (!e.context.empty()) continue;
            for (const Room& r : rooms) {
                if (e.english != r.english) continue;
                std::string cp;
                REQUIRE(I18n::utf8ToCp1252Exact(e.text, cp));
                ++checked;
                CHECK_MESSAGE(static_cast<int>(cp.size()) <= r.chars,
                              po.string() << ": '" << e.text << "' is " << cp.size()
                              << " characters; " << std::string(r.where) << " holds " << r.chars);
            }
        }
    }
    CHECK(checked >= 6);   // a pack translates them: the check is not vacuous
}
