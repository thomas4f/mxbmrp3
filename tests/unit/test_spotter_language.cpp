// ============================================================================
// tests/unit/test_spotter_language.cpp
// Pins the spotter's translation layer, the pure half:
//  - SpotterPhrase::say() and kWordKeys: the words the plugin ASSEMBLES (the
//    numbers, "rider", "quicker", a trick's name) are looked up in one place,
//    and the registry is a census both ways - a say("...") literal missing
//    from it fails, and so does a key nothing says;
//  - the [Words] section of a pack file, which is where a language's spotter
//    file translates them;
//  - every SHIPPED language spotter.ini (mxbmrp3_data/languages/<code>/): each
//    row a real cue, each {variable} a real variable, every word translated,
//    and every cue the shipped pack speaks translated too - a missing line
//    would be an English sentence in the middle of a Swedish spotter;
//  - ChatText::cp1252ToUtf8, which carries a translated session name or
//    achievement title (the menus' code page) into the voice's UTF-8.
// Which file wins and when the language switches is the real DLL's, in
// tests/integration/tests/spotter_language_test.cpp.
// ============================================================================
#include "doctest.h"

#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

#include "core/chat_text.h"
#include "core/spotter_cue_pack.h"
#include "core/spotter_phrase.h"
#include "core/spotter_tricks.h"
#include "core/spotter_vars.h"

namespace fs = std::filesystem;

namespace {

std::string slurp(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Words installed for one scope, and cleared again whatever happens, so no
// other test case hears Swedish numbers.
struct WordsScope {
    explicit WordsScope(const std::map<std::string, std::string>& words) {
        SpotterPhrase::spokenWords().clear();
        for (const auto& kv : words) SpotterPhrase::spokenWords()[kv.first] = kv.second;
    }
    ~WordsScope() { SpotterPhrase::spokenWords().clear(); }
};

std::set<std::string> wordKeys() {
    std::set<std::string> keys;
    for (const char* k : SpotterPhrase::kWordKeys) keys.insert(k);
    return keys;
}

// Every say("...") literal in the spotter's sources.
std::set<std::string> saidLiterals() {
    std::set<std::string> said;
    // A scan, not std::regex: GCC 13's <regex> trips -Wmaybe-uninitialized
    // under the ASan build's flags.
    const std::string open = "say(\"";
    int files = 0;
    for (const auto& entry : fs::directory_iterator(MXB_CORE_DIR)) {
        const std::string name = entry.path().filename().string();
        if (name.rfind("spotter_", 0) != 0) continue;
        ++files;
        const std::string text = slurp(entry.path().string());
        for (size_t at = text.find(open); at != std::string::npos; at = text.find(open, at + 1)) {
            const size_t from = at + open.size();
            const size_t close = text.find('"', from);
            if (close != std::string::npos && text.compare(close, 2, "\")") == 0) {
                said.insert(text.substr(from, close - from));
            }
        }
    }
    REQUIRE(files >= 10);   // a wrong MXB_CORE_DIR would pass vacuously
    return said;
}

// The cue roots a pack speaks: live, non-empty rows, variants folded.
std::set<std::string> spokenRoots(const SpotterCuePack::Pack& pack) {
    std::set<std::string> roots;
    for (const auto& kv : pack.phrases) {
        if (!kv.second.empty()) roots.insert(SpotterCuePack::stripVariantSuffix(kv.first));
    }
    return roots;
}

std::set<std::string> variablesIn(const std::string& line) {
    std::set<std::string> names;
    for (size_t at = line.find('{'); at != std::string::npos; at = line.find('{', at + 1)) {
        const size_t close = line.find('}', at);
        if (close == std::string::npos) break;
        names.insert(line.substr(at + 1, close - at - 1));
    }
    return names;
}

}  // namespace

TEST_CASE("spotter words: every say() literal is a registered word") {
    const std::set<std::string> keys = wordKeys();
    const std::set<std::string> said = saidLiterals();
    CHECK(said.size() >= 10);
    for (const std::string& w : said) {
        CAPTURE(w);
        CHECK_MESSAGE(keys.count(w) == 1,
                      "say() is asked for a word kWordKeys does not list - a "
                      "language could never translate it, and nothing says so");
    }
}

TEST_CASE("spotter words: every registered word is said by something") {
    // Not every word reaches say() as a literal: countWords and paceWords take
    // theirs as arguments, the numbers come from a table and the trick names
    // from a switch. So a word counts as said when its quoted literal appears
    // anywhere in the spotter's sources outside the registry itself.
    std::string sources;
    for (const auto& entry : fs::directory_iterator(MXB_CORE_DIR)) {
        const std::string name = entry.path().filename().string();
        if (name.rfind("spotter_", 0) != 0) continue;
        std::string text = slurp(entry.path().string());
        const size_t from = text.find("kWordKeys[] = {");
        if (from != std::string::npos) text.erase(from, text.find("};", from) - from);
        sources += text;
    }
    for (const char* k : SpotterPhrase::kWordKeys) {
        const std::string key = k;
        CAPTURE(key);
        CHECK_MESSAGE(sources.find("\"" + key + "\"") != std::string::npos,
                      "registered word nothing says - translators would be "
                      "asked for a word the spotter never speaks");
    }
}

TEST_CASE("spotter words: English with no table, the translation with one") {
    SpotterPhrase::spokenWords().clear();
    CHECK(SpotterPhrase::numberWords(476) == "four seventy six");
    CHECK(SpotterPhrase::numberWords(206) == "two oh six");
    CHECK(SpotterPhrase::lapsWords(1) == "one lap");
    CHECK(SpotterPhrase::paceWords(-400) == "zero point four quicker");
    CHECK(SpotterPhrase::riderRef(12, -1) == "rider twelve");
    {
        const WordsScope sv({ { "four", "fyra" }, { "seventy", "sjuttio" }, { "six", "sex" },
                              { "two", "två" }, { "oh", "noll" }, { "one lap", "ett varv" },
                              { "laps", "varv" }, { "zero", "noll" }, { "point", "komma" },
                              { "quicker", "snabbare" }, { "rider", "förare" },
                              { "twelve", "tolv" }, { "backflip", "bakåtvolt" },
                              { "double", "dubbel" } });
        CHECK(SpotterPhrase::numberWords(476) == "fyra sjuttio sex");
        CHECK(SpotterPhrase::numberWords(206) == "två noll sex");
        // The singular is its own key, so "one" can agree with the noun.
        CHECK(SpotterPhrase::lapsWords(1) == "ett varv");
        CHECK(SpotterPhrase::lapsWords(2) == "två varv");
        CHECK(SpotterPhrase::paceWords(-400) == "noll komma fyra snabbare");
        CHECK(SpotterPhrase::riderRef(12, -1) == "förare tolv");
        CHECK(SpotterTricks::spokenTrick(Fmx::TrickType::BACKFLIP, 2) == "dubbel bakåtvolt");
        // A word the table leaves out stays English rather than going silent.
        CHECK(SpotterPhrase::numberWords(3) == "three");
    }
    {
        // A language whose 21..99 do not compose lists them whole.
        const WordsScope fr({ { "four", "quatre" }, { "seventy", "soixante-dix" }, { "six", "six" },
                              { "76", "soixante-seize" } });
        CHECK(SpotterPhrase::numberWords(476) == "quatre soixante-seize");
        CHECK(SpotterPhrase::numberWords(70) == "soixante-dix");    // a round ten composes
        CHECK(SpotterPhrase::numberWords(406) == "quatre oh six"); // "06" is never a whole number
    }
    CHECK(SpotterPhrase::numberWords(476) == "four seventy six");
}

TEST_CASE("spotter words: a pack's [Words] section parses, inner spaces kept") {
    const SpotterCuePack::Pack p = SpotterCuePack::parse(
        "[Cues]\nrider_behind = Bakom.\n"
        "[words]\n"
        "  dropped back  =  tappade  \n"
        "one lap = ett varv\n"
        "empty =\n"
        "= orphan\n"
        "; comment = no\n");
    CHECK(p.phrases.at("rider_behind") == "Bakom.");
    CHECK(p.words.at("dropped back") == "tappade");
    CHECK(p.words.at("one lap") == "ett varv");
    CHECK(p.words.count("empty") == 0);     // a blank translation is a typo, not a mute
    CHECK(p.words.size() == 2);
    // [Words] is not [Cues]: its rows are never cues.
    CHECK(p.phrases.count("one lap") == 0);
}

TEST_CASE("shipped language spotter files: complete, and every row real") {
    const SpotterCuePack::Pack shipped = SpotterCuePack::parse(
        slurp(std::string(MXB_DATA_DIR) + "/spotters/default/spotter.ini"));
    const std::set<std::string> shippedSpeaks = spokenRoots(shipped);
    REQUIRE(shippedSpeaks.size() >= 100);
    int languages = 0;
    for (const auto& entry : fs::directory_iterator(LANGUAGES_DIR)) {
        const fs::path file = entry.path() / "spotter.ini";
        if (!entry.is_directory() || !fs::exists(file)) continue;
        ++languages;
        const std::string lang = entry.path().filename().string();
        CAPTURE(lang);
        const SpotterCuePack::Pack pack = SpotterCuePack::parse(slurp(file.string()));
        CHECK_MESSAGE(pack.wavs.empty(), "a language file carries words, not audio");
        for (const auto& kv : pack.phrases) {
            const std::string key = kv.first;
            CAPTURE(key);
            CHECK(SpotterCuePack::isCueKey(SpotterCuePack::stripVariantSuffix(key)));
            for (const std::string& v : variablesIn(kv.second)) {
                CAPTURE(v);
                SpotterVars::Vars empty;
                CHECK_MESSAGE(SpotterVars::lookup(empty, v) != nullptr,
                              "not a variable: it would be spoken as braces");
            }
        }
        const std::set<std::string> speaks = spokenRoots(pack);
        for (const std::string& root : shippedSpeaks) {
            CAPTURE(root);
            CHECK_MESSAGE(speaks.count(root) == 1,
                          "the shipped pack speaks this cue and the language "
                          "file does not translate it - it would be the one "
                          "English sentence in a translated spotter");
        }
        for (const char* w : SpotterPhrase::kWordKeys) {
            const std::string word = w;
            CAPTURE(word);
            CHECK_MESSAGE(pack.words.count(word) == 1,
                          "word left untranslated in [Words]");
        }
        for (const auto& kv : pack.words) {
            const std::string word = kv.first;
            CAPTURE(word);
            // 21..99 by their digits are the optional whole numbers.
            const bool wholeNumber = word.size() == 2 && word[0] >= '2' && word[0] <= '9' &&
                                     word[1] >= '1' && word[1] <= '9';
            CHECK_MESSAGE((wordKeys().count(word) == 1 || wholeNumber),
                          "[Words] row for a word nothing says");
        }
    }
    CHECK(languages >= 1);   // Swedish ships one
}

TEST_CASE("cp1252ToUtf8: the menus' code page back to the voice's UTF-8") {
    CHECK(ChatText::cp1252ToUtf8("Mekare") == "Mekare");
    CHECK(ChatText::cp1252ToUtf8("Tr\xE4n\xE4r") == "Tr\xC3\xA4n\xC3\xA4r");    // Tränär
    CHECK(ChatText::cp1252ToUtf8("\x80") == "\xE2\x82\xAC");                     // the euro
    CHECK(ChatText::cp1252ToUtf8("\x81") == "\xC4\x9F");                         // the Turkish slot: ğ
    CHECK(ChatText::cp1252ToUtf8(nullptr).empty());
    // And back: the two directions agree.
    const std::string sv = "Sl\xC3\xA4pp g\xC3\xA5s, \xC3\xB6ver";
    CHECK(ChatText::cp1252ToUtf8(ChatText::utf8ToCp1252(sv).c_str()) == sv);
}
