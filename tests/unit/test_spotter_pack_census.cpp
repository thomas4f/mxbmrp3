// ============================================================================
// tests/unit/test_spotter_pack_census.cpp
// Walks the SHIPPED spotter pack (mxbmrp3_data/spotters/default/spotter.ini)
// against the two published namespaces - every cue key in
// SpotterCuePack::allCueKeys(), every variable in SpotterVars::bindings() -
// and requires them to agree in BOTH directions.
//
// WHY THIS EXISTS. Neither half of a pack file is checked by anything else. A
// key nobody emits parses fine and is simply never spoken; a {variable} that
// is not a variable parses fine and is printed literally on screen. Both look
// exactly like a working pack until you are on track. That is not theoretical:
// renaming the milestone cues left `ten_minutes_remaining` in the shipped ini
// while the emitter still said `time_10min`, and every gate stayed green.
//
// The directions catch different mistakes and both matter:
//   ini -> registry   a row that will never fire (typo, or a key that moved)
//   registry -> ini   a cue the plugin can speak that the shipped pack never
//                     mentions, which is the only reference most authors read
//
// COMMENTED ROWS COUNT as documentation. A default-quiet cue ships commented
// out precisely so it can be found and uncommented, so `;pit_entry_other = ...`
// satisfies the second direction while staying silent.
// ============================================================================
#include "doctest.h"

#include "core/spotter_cue_pack.h"
#include "core/spotter_phrase.h"
#include "core/spotter_vars.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <set>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>
#include <utility>

namespace {

std::string shippedPackText() {
    const std::string path =
        std::string(SPOTTERS_DIR) + "/default/spotter.ini";
    std::ifstream in(path, std::ios::binary);
    REQUIRE_MESSAGE(in.good(), "shipped pack missing: " << path);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string trim(std::string s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return std::string();
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

// Strip the suffixes that are part of the FORMAT rather than the key: a `_wav`
// row and a `_2` variant both belong to their base key.
std::string baseKey(std::string k) {
    for (const char* suffix : { "_wav", "_mix" }) {
        const size_t len = std::string(suffix).size();
        if (k.size() > len && k.compare(k.size() - len, len, suffix) == 0) {
            k.resize(k.size() - len);
        }
    }
    if (k.size() > 2 && k[k.size() - 2] == '_' && std::isdigit(static_cast<unsigned char>(k.back()))) {
        k.resize(k.size() - 2);
    }
    return k;
}

// Every cue key the shipped ini names, live or commented out. Deliberately a
// separate scan from parse(): parse() drops comments, and a commented row is
// how a default-quiet cue documents itself.
std::set<std::string> declaredKeys(const std::string& text) {
    std::set<std::string> keys;
    std::istringstream lines(text);
    std::string line;
    bool inCues = false;
    while (std::getline(lines, line)) {
        std::string s = trim(line);
        if (!s.empty() && s[0] == ';') s = trim(s.substr(1));
        if (s.empty()) continue;
        if (s[0] == '[') {
            inCues = (s.rfind("[Cues]", 0) == 0);
            continue;
        }
        if (!inCues) continue;
        const size_t eq = s.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = trim(s.substr(0, eq));
        if (key.empty()) continue;
        // Prose lines in the header ("<key> = the words") and any other
        // sentence that happens to contain '=' are not keys.
        bool keyShaped = true;
        for (char c : key) {
            if (!std::islower(static_cast<unsigned char>(c)) && c != '_' &&
                !std::isdigit(static_cast<unsigned char>(c))) {
                keyShaped = false;
                break;
            }
        }
        if (keyShaped) keys.insert(baseKey(key));
    }
    return keys;
}

// Only the VALUE side of a cue row: the header's prose says things like
// "{variables}" to describe the feature, and that is not a template.
std::set<std::string> usedVariables(const std::string& text) {
    std::string values;
    std::istringstream lines(text);
    std::string line;
    bool inCues = false;
    while (std::getline(lines, line)) {
        std::string s = trim(line);
        if (!s.empty() && s[0] == ';') s = trim(s.substr(1));
        if (s.empty()) continue;
        if (s[0] == '[') {
            inCues = (s.rfind("[Cues]", 0) == 0);
            continue;
        }
        const size_t eq = s.find('=');
        if (!inCues || eq == std::string::npos) continue;
        values += s.substr(eq + 1);
        values += '\n';
    }
    const std::string& text2 = values;
    std::set<std::string> names;
    for (size_t i = 0; i + 1 < text2.size(); ++i) {
        if (text2[i] != '{') continue;
        const size_t close = text2.find('}', i + 1);
        if (close == std::string::npos) break;
        const std::string name = text2.substr(i + 1, close - i - 1);
        bool nameShaped = !name.empty();
        for (char c : name) {
            if (!std::islower(static_cast<unsigned char>(c)) && c != '_') {
                nameShaped = false;
                break;
            }
        }
        if (nameShaped) names.insert(name);
        i = close;
    }
    return names;
}

// The live rows of the shipped pack, key -> value. This is now the only answer
// to "does this cue ship silent?" - the registry used to carry a `quiet` flag
// saying so, which was a second copy of a fact the file already stated, and
// the two could disagree.
std::map<std::string, std::string> liveRows(const std::string& text) {
    std::map<std::string, std::string> rows;
    std::istringstream lines(text);
    std::string line;
    bool inCues = false;
    while (std::getline(lines, line)) {
        const std::string s = trim(line);
        if (s.empty() || s[0] == ';') continue;          // comment: not live
        if (s[0] == '[') { inCues = (s.rfind("[Cues]", 0) == 0); continue; }
        if (!inCues) continue;
        const size_t eq = s.find('=');
        if (eq == std::string::npos) continue;
        rows[baseKey(trim(s.substr(0, eq)))] = trim(s.substr(eq + 1));
    }
    return rows;
}

// Does this cue actually SPEAK out of the box? Resolved the way emitCue
// resolves it: the cue's own row, else the row of the key it refines. An empty
// value is an explicit mute and stops there, which is why this cannot just ask
// whether a row exists - `penalty_change_other =` and a commented-out
// penalty_change_other mean the same thing to the spotter and would otherwise
// read differently here.
bool shipsSpeaking(const std::map<std::string, std::string>& rows,
                   const std::string& key) {
    auto it = rows.find(key);
    if (it == rows.end()) {
        if (const char* base = SpotterCuePack::fallbackCueKey(key)) {
            it = rows.find(base);
        }
    }
    return it != rows.end() && !it->second.empty();
}

}  // namespace

TEST_CASE("shipped pack: every row names a cue the plugin actually emits") {
    for (const std::string& key : declaredKeys(shippedPackText())) {
        CAPTURE(key);
        CHECK_MESSAGE(SpotterCuePack::isCueKey(key),
                      "row in the shipped pack names no real cue - it will "
                      "never be spoken, and nothing else would say so");
    }
}

TEST_CASE("shipped pack: every cue the plugin emits is documented in it") {
    const std::set<std::string> declared = declaredKeys(shippedPackText());
    for (const SpotterCuePack::CueKeyInfo& info : SpotterCuePack::allCueKeys()) {
        const std::string key = info.key;   // a const char* captures as a bool
        CAPTURE(key);
        CHECK_MESSAGE(declared.count(key) == 1,
                      "cue missing from the shipped pack - that file is the "
                      "reference most authors read, so an absent key is an "
                      "undiscoverable one (comment the row out if it should "
                      "stay silent)");
    }
}

TEST_CASE("shipped pack: every {variable} it uses is a real variable") {
    for (const std::string& name : usedVariables(shippedPackText())) {
        CAPTURE(name);
        SpotterVars::Vars empty;
        CHECK_MESSAGE(SpotterVars::lookup(empty, name) != nullptr,
                      "not a variable: expand() leaves an unknown name on "
                      "screen verbatim, so this ships as visible braces");
    }
}

// cueKeyFor is pure, so every event it maps can be enumerated and checked
// directly - no source scanning needed for this half.
TEST_CASE("every key cueKeyFor returns is in the registry") {
    // The enum has no COUNT terminator, so walk past its last value: an
    // unmapped index returns nullptr and is skipped, which costs nothing and
    // means a newly appended event is covered without editing this bound.
    for (int i = 0; i < 64; ++i) {
        const EventLogType type = static_cast<EventLogType>(i);
        for (bool focused : { true, false }) {
            const char* key = SpotterCuePack::cueKeyFor(type, focused);
            if (!key) continue;   // never-spoken events map to nothing
            const std::string k = key;
            CAPTURE(i);
            CAPTURE(k);
            CHECK(SpotterCuePack::isCueKey(k));
        }
    }
}

// The rest of the keys are string literals inside SpotterManager, which is a
// set of Windows TUs the unit suite cannot link. Scanning their source is the
// only way to reach them, and it is worth doing: those literals are half the
// cue vocabulary, and a typo in one produces a cue no pack can ever override -
// silently, because the built-in phrase still speaks.
//
// ALL the spotter_manager*.cpp TUs, not just the hub: the emit sites were
// split across spotter_manager_{events,compose,proximity}.cpp, and a scan
// pinned to one file would go quietly blind the next time a method moves.
// GLOBBED rather than listed, for the same reason one level up: a
// hand-pinned TU list goes quietly blind the next time a TU is carved out
// (spotter_manager_events.cpp's own header already announces the next one).
// The hub REQUIRE guards a wrong MXB_CORE_DIR; the `found >= 10` floor below
// is what catches the scan itself matching nothing.
static std::string slurpSpotterManagerTus() {
    namespace fs = std::filesystem;
    std::vector<std::string> paths;
    for (const auto& entry : fs::directory_iterator(MXB_CORE_DIR)) {
        const std::string name = entry.path().filename().string();
        if (name.rfind("spotter_manager", 0) == 0 &&
            name.size() >= 4 && name.compare(name.size() - 4, 4, ".cpp") == 0) {
            paths.push_back(entry.path().string());
        }
    }
    std::sort(paths.begin(), paths.end());
    REQUIRE_MESSAGE(std::any_of(paths.begin(), paths.end(),
                                [](const std::string& p) {
                                    return p.find("spotter_manager.cpp") != std::string::npos;
                                }),
                    "the hub spotter_manager.cpp was not found under " << MXB_CORE_DIR);
    std::string all;
    for (const std::string& path : paths) {
        std::ifstream in(path, std::ios::binary);
        REQUIRE_MESSAGE(in.good(), "cannot read " << path);
        std::ostringstream ss;
        ss << in.rdbuf();
        all += ss.str();
        all += '\n';
    }
    return all;
}

TEST_CASE("every key SpotterManager emits is in the registry") {
    const std::string src = slurpSpotterManagerTus();

    int found = 0;
    const std::string needle = "emitCue(\"";
    for (size_t i = src.find(needle); i != std::string::npos;
         i = src.find(needle, i + 1)) {
        const size_t start = i + needle.size();
        const size_t end = src.find('"', start);
        if (end == std::string::npos) break;
        const std::string key = src.substr(start, end - start);
        CAPTURE(key);
        CHECK(SpotterCuePack::isCueKey(key));
        ++found;
    }
    // A scan that matched nothing would pass every check vacuously - the
    // failure mode of every source-scanning test.
    CHECK(found >= 10);
}

// ...and the other direction, which is the one that had been missing. A cue in
// the registry that NOTHING emits is a promise the pack format cannot keep: it
// is listed in the settings, documented in docs/spotter-reference.md, and a
// pack that defines it simply never hears it.
//
// This is not hypothetical. fuel_low and fuel_critical were emitted from the
// lap handler until the block was deleted wholesale by an unrelated refactor
// (the position report moving to a deferred flush took the fuel check out with
// it). Both keys stayed in the registry, in the docs and in the shipped pack,
// dead, and every other gate passed - the census only ever checked that
// emitted keys are registered, never that registered keys are emitted.
TEST_CASE("every key in the registry is emitted by something") {
    auto slurp = [](const std::string& path) {
        std::ifstream in(path, std::ios::binary);
        REQUIRE_MESSAGE(in.good(), "cannot read " << path);
        std::ostringstream ss;
        ss << in.rdbuf();
        return ss.str();
    };
    // Two shapes, because a key reaches emitCue() two ways: as a literal in
    // the manager (directly, or assigned to `key` for a session-kind variant),
    // or RETURNED by one of the pure headers the manager asks for a key - the
    // event-type mapping, and the milestone state machine.
    const std::string mgr = slurpSpotterManagerTus();
    const std::string pack = slurp(std::string(MXB_CORE_DIR) + "/spotter_cue_pack.h");
    const std::string miles = slurp(std::string(MXB_CORE_DIR) + "/spotter_milestones.h");

    // In spotter_cue_pack.h, only RETURN lines count, and every literal on one
    // (the focused/other pairs come back out of a ternary). The fallback table
    // there pairs keys as plain literals, so accepting any quoted occurrence
    // would let a dead key vouch for itself.
    //
    // spotter_milestones.h has no such table - every string in it IS a cue it
    // hands back - and its state machine has to mark the skipped milestones
    // spent after choosing, so it assigns to a local and returns that at the
    // end. Any literal counts there.
    std::vector<std::string> returned;
    {
        std::istringstream ls(pack);
        std::string line;
        while (std::getline(ls, line)) {
            if (line.find("return") == std::string::npos) continue;
            for (size_t q = line.find('"'); q != std::string::npos;
                 q = line.find('"', q + 1)) {
                const size_t end = line.find('"', q + 1);
                if (end == std::string::npos) break;
                returned.push_back(line.substr(q + 1, end - q - 1));
                q = end;
            }
        }
    }
    for (size_t q = miles.find('"'); q != std::string::npos;
         q = miles.find('"', q + 1)) {
        const size_t end = miles.find('"', q + 1);
        if (end == std::string::npos) break;
        returned.push_back(miles.substr(q + 1, end - q - 1));
        q = end;
    }
    REQUIRE(returned.size() >= 10);   // a scan that matched nothing proves nothing

    for (const SpotterCuePack::CueKeyInfo& info : SpotterCuePack::allCueKeys()) {
        const std::string key = info.key;
        CAPTURE(key);
        const bool inManager =
            mgr.find("\"" + key + "\"") != std::string::npos;
        const bool inMapping =
            std::find(returned.begin(), returned.end(), key) != returned.end();
        const bool emitted = inManager || inMapping;
        CHECK_MESSAGE(emitted,
                      "cue key '" << key << "' is registered but nothing emits "
                      "it - either wire it up or drop it from the registry");
    }
}

// The shipped pack's four headings ARE the four switches in the settings
// menu, so a cue filed under the wrong one tells the reader that the wrong
// switch controls it. Nothing about that is visible in game - the cue still
// fires, still mutes, just not from where the file says - so the grouping is
// checked here against the category the registry records.
TEST_CASE("shipped pack: every cue sits under the switch that mutes it") {
    const std::string text = shippedPackText();
    std::istringstream lines(text);
    std::string line;
    SpotterPhrase::Category section = SpotterPhrase::Category::COUNT;
    int checked = 0;
    while (std::getline(lines, line)) {
        const std::string s = trim(line);
        if (s.rfind("; GENERAL", 0) == 0) section = SpotterPhrase::Category::General;
        else if (s.rfind("; TIMING", 0) == 0) section = SpotterPhrase::Category::Timing;
        else if (s.rfind("; OPPONENTS", 0) == 0) section = SpotterPhrase::Category::Opponents;
        else if (s.rfind("; PROXIMITY", 0) == 0) section = SpotterPhrase::Category::Proximity;
        else if (s.rfind("; HAZARDS", 0) == 0) section = SpotterPhrase::Category::Hazard;
        if (section == SpotterPhrase::Category::COUNT) continue;

        std::string row = s;
        if (!row.empty() && row[0] == ';') row = trim(row.substr(1));
        const size_t eq = row.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = baseKey(trim(row.substr(0, eq)));
        if (!SpotterCuePack::isCueKey(key)) continue;

        for (const SpotterCuePack::CueKeyInfo& info : SpotterCuePack::allCueKeys()) {
            if (key != info.key) continue;
            CAPTURE(key);
            CHECK_MESSAGE(info.category == section,
                          "cue is under the wrong heading - the headings are "
                          "the settings-menu switches, so this one names the "
                          "wrong switch as its mute");
            ++checked;
            break;
        }
    }
    // A scan that matched no rows would pass every check vacuously.
    CHECK(checked >= 40);
}

// ============================================================================
// ALTERNATES. The shipped pack carries `<key>_2..` rows on its most-heard
// cues, and both ways of getting them wrong are invisible in game: the cue
// still fires, still says something, just not what the file appears to
// promise. Neither is caught by the two censuses above, which work in base
// keys and cannot see the numbering at all.
// ============================================================================
namespace {

// Every declared row's LITERAL key (no suffix stripping), live or commented -
// the numbering is the thing under test, so it has to survive the scan.
std::vector<std::pair<std::string, bool>> declaredRows(const std::string& text) {
    std::vector<std::pair<std::string, bool>> rows;   // key, live
    std::istringstream lines(text);
    std::string line;
    bool inCues = false;
    while (std::getline(lines, line)) {
        std::string s = trim(line);
        const bool commented = !s.empty() && s[0] == ';';
        if (commented) s = trim(s.substr(1));
        if (s.empty()) continue;
        if (s[0] == '[') { inCues = (s.rfind("[Cues]", 0) == 0); continue; }
        if (!inCues) continue;
        const size_t eq = s.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = trim(s.substr(0, eq));
        if (key.empty()) continue;
        bool keyShaped = true;
        for (char c : key) {
            if (!std::islower(static_cast<unsigned char>(c)) && c != '_' &&
                !std::isdigit(static_cast<unsigned char>(c))) {
                keyShaped = false;
                break;
            }
        }
        // The row must also name a cue, or the header's prose examples count.
        if (keyShaped && SpotterCuePack::isCueKey(baseKey(key))) {
            rows.emplace_back(key, !commented);
        }
    }
    return rows;
}

int variantNumber(const std::string& key) {
    if (key.size() > 2 && key[key.size() - 2] == '_' &&
        key.back() >= '2' && key.back() <= '9') {
        return key.back() - '0';
    }
    return 1;   // the base row is variant 1
}

}  // namespace

// variantKeys() stops at the first missing number, so a `_4` written without a
// `_3` is dead text: it parses, it sits in the file looking like a line the
// spotter says, and it is never once selected. Exactly the mistake an author
// makes by deleting an alternate they went off.
TEST_CASE("shipped pack: alternates are numbered contiguously from _2") {
    std::map<std::string, std::set<int>> byBase;
    for (const auto& row : declaredRows(shippedPackText())) {
        byBase[baseKey(row.first)].insert(variantNumber(row.first));
    }
    int withAlternates = 0;
    for (const auto& kv : byBase) {
        const std::string key = kv.first;
        if (kv.second.size() < 2) continue;
        ++withAlternates;
        int expected = 1;
        for (int n : kv.second) {
            CAPTURE(key);
            CAPTURE(n);
            CAPTURE(expected);
            CHECK_MESSAGE(n == expected,
                          "gap in the alternate numbering - variantKeys() stops "
                          "at the first missing number, so this row and every "
                          "one above it is never spoken");
            ++expected;
        }
    }
    // A scan that found no alternates would pass every check vacuously, and
    // this file is where the worked examples of the feature live.
    CHECK(withAlternates >= 10);
}

// An alternate under a base that ships SILENT is half a callout: the base row
// is always in the selection set (it can come from a pack layered underneath),
// so a live `_2` over a commented base speaks its line some firings and
// nothing the others - which reads as an intermittent bug, not as a choice.
// Muting a cue means commenting out its alternates too.
TEST_CASE("shipped pack: no live alternate sits under a silent base") {
    std::map<std::string, bool> baseLive;
    std::vector<std::pair<std::string, bool>> alternates;
    for (const auto& row : declaredRows(shippedPackText())) {
        if (variantNumber(row.first) == 1) baseLive[row.first] = row.second;
        else alternates.emplace_back(row.first, row.second);
    }
    for (const auto& alt : alternates) {
        if (!alt.second) continue;             // commented out: fine
        const std::string key = alt.first;
        CAPTURE(key);
        auto it = baseLive.find(baseKey(key));
        const bool baseSpeaks = it != baseLive.end() && it->second;
        CHECK_MESSAGE(baseSpeaks,
                      "live alternate whose base row is commented out or "
                      "missing - the base is always in the selection set, so "
                      "this cue would speak on some firings and be silent on "
                      "the rest");
    }
}

// The reverse for variables is NOT asserted. A variable that no shipped line
// happens to use is perfectly fine - the shipped pack is one set of phrasings,
// not an exercise of the whole vocabulary - and requiring coverage would push
// contrived lines into a file whose job is to read well. The variable list is
// documented by spotter_vars.h and the README instead, and pinned by
// test_spotter_vars.cpp's census.

// ============================================================================
// docs/spotter-reference.md is GENERATED - from allCueKeys() and
// SpotterVars::bindings(), which are the only places those lists exist.
//
// WHY GENERATED RATHER THAN CHECKED. A census can only ask "do these two agree
// about the names?", and that is not what a pack author needs: they need to
// know WHEN a cue fires, which switch mutes it, whether it ships silent, and
// what a variable means. Prose carrying that drifts the moment somebody adds a
// row - and the shipped ini used to carry all of it, at 259 comment lines to
// 41 editable ones, which is its own kind of unreadable.
//
// Generating it moves the failure from "nobody noticed" to "the build says
// so": a new cue changes the generated text, the diff below fails, and
// committing the regenerated file is the fix. There is no path where the
// reference silently falls behind, because nothing else writes it.
// ============================================================================
namespace {

// The heading each category prints under. Deliberately the same four words the
// settings tab shows and the shipped ini groups by, so a reader moving between
// the three never has to translate.
const char* categoryName(SpotterPhrase::Category c) {
    switch (c) {
        case SpotterPhrase::Category::General:   return "General";
        case SpotterPhrase::Category::Timing:    return "Timing";
        case SpotterPhrase::Category::Opponents: return "Opponents";
        case SpotterPhrase::Category::Proximity: return "Proximity";
        case SpotterPhrase::Category::Hazard:    return "Hazards";
        default:                                 return "Other";
    }
}

std::string generateReference() {
    const std::map<std::string, std::string> rows = liveRows(shippedPackText());
    std::ostringstream md;
    md << "# Spotter reference\n\n"
       << "GENERATED from `mxbmrp3/core/spotter_cue_pack.h` (`allCueKeys()`)\n"
          "and `mxbmrp3/core/spotter_vars.h` (`bindings()`).\n"
          "Do not edit by hand - `test_spotter_pack_census.cpp` rewrites it and\n"
          "fails if this copy is stale.\n\n"
       << "This is the lookup table. The two files it goes with:\n\n"
       << "- `mxbmrp3_data/spotters/default/spotter.ini` - the shipped pack,\n"
          "  which is the wording itself. Every key below has a row in it, so it\n"
          "  is also the worked example; copy that folder to edit it.\n"
       << "- `docs/spotter.md` - the guide: what it calls and how to set it up,\n"
          "  then the authoring half (optional groups, alternates, fallbacks,\n"
          "  recorded packs, the chunk mixer and `[Mix] gap_ms`).\n\n"
       << "## Cues\n\n"
       << "A cue is a MOMENT. Each row is a key you can define in a pack's\n"
          "`[Cues]` section; the phrase you write against it is what gets said.\n\n"
       << "**Ships** is whether the shipped pack speaks it out of the box -\n"
          "its own row, or the row of the key it refines. There is no wording\n"
          "anywhere else, so a cue nothing defines is simply not spoken, and\n"
          "uncommenting its row is how you turn a *silent* one on. The heading\n"
          "a cue sits under names the switch in\n"
          "Settings > Spotter that mutes it, and the section it lives in in the\n"
          "shipped ini - the three deliberately agree.\n\n";

    for (int c = 0; c < static_cast<int>(SpotterPhrase::Category::COUNT); ++c) {
        const auto cat = static_cast<SpotterPhrase::Category>(c);
        md << "### " << categoryName(cat) << "\n\n"
           << "| Cue | Ships | When it fires |\n|---|---|---|\n";
        for (const SpotterCuePack::CueKeyInfo& info : SpotterCuePack::allCueKeys()) {
            if (info.category != cat) continue;
            md << "| `" << info.key << "` | "
               << (shipsSpeaking(rows, info.key) ? "on" : "silent")
               << " | " << info.what << " |\n";
        }
        md << "\n";
    }

    md << "## Variables\n\n"
       << "A variable is a NUMBER - it does not happen, it just is.\n\n"
          "**Reads as** is a sample expansion, not a format string: values\n"
          "arrive as words, and some of them ALREADY CARRY their direction\n"
          "(`{gap_to_best_lap}` says \"quicker\", `{gap_to_ahead}` does not),\n"
          "so a template that adds the word itself reads \"up three tenths\n"
          "quicker\". These are the values `spotter-pack-render.md` uses.\n\n"
          "**Every variable works in every cue.** They are read from the live\n"
          "race at the moment a cue fires, not carried by the event that fired\n"
          "it, so any callout can ask for your position or the gap ahead. One\n"
          "with no value right now expands to nothing, and a `[bracketed\n"
          "group]` containing it drops whole - which is what lets a single\n"
          "template read correctly whether or not the value is there.\n\n";

    const char* lastGroup = nullptr;
    for (const SpotterVars::Binding& b : SpotterVars::bindings()) {
        if (!lastGroup || std::string(lastGroup) != b.group) {
            if (lastGroup) md << "\n";
            md << "### " << b.group
               << "\n\n| Variable | Meaning | Reads as |\n|---|---|---|\n";
            lastGroup = b.group;
        }
        md << "| `{" << b.name << "}` | " << b.what << " | "
           << b.example << " |\n";
    }
    md << "\n";
    return md.str();
}

}  // namespace

// ============================================================================
// THE RENDERED PACK. Everything above checks the shipped file's STRUCTURE -
// that its keys exist, its variables exist, its alternates are numbered. None
// of that can see what a line SAYS, and the mistakes that survived every gate
// were all in the saying:
//
//   "up {sector_delta_best_lap}"          -> "up three tenths QUICKER"
//   "{rider_behind} is {gap_to_behind} back."  -> "is back."   (leading, no gap)
//   "{event_rider} goes quickest"         -> lowercase in the subtitle
//
// A predicate for those is either fuzzy or needs a per-row opt-out: the third
// is now impossible (expand() capitalises), but the first two are judgements
// about English, and "P {position}" bare is CORRECT on cues whose emitter
// guarantees a position. So this generates the answer instead of asserting it.
// Every live row is expanded twice - with every variable filled from the
// bindings table's examples, and with every one of them empty - into a
// committed file. A bad line is then a line you can read, in a diff, next to
// the change that caused it. Same mechanism as the reference below, for the
// same reason: a check that cannot be written is not an excuse for prose.
// ============================================================================
namespace {

// Every variable at its sample value (SpotterVars::Binding::example), which is
// the same table docs/spotter-reference.md prints - one set of samples, so the
// reference and this file cannot show different things.
SpotterVars::Vars filledVars() {
    SpotterVars::Vars v;
    for (const SpotterVars::Binding& b : SpotterVars::bindings()) {
        v.*(b.value) = b.example;
    }
    return v;
}

std::string generatePackRender() {
    const std::string text = shippedPackText();
    const SpotterVars::Vars full = filledVars();
    const SpotterVars::Vars none;   // every variable empty

    std::ostringstream md;
    md << "# The shipped spotter pack, rendered\n\n"
       << "GENERATED from `mxbmrp3_data/spotters/default/spotter.ini` by\n"
          "`test_spotter_pack_census.cpp`, which rewrites it and fails if this\n"
          "copy is stale. Do not edit by hand - edit the pack.\n\n"
       << "Every live row of the shipped pack, as the subtitle shows it and\n"
          "text-to-speech reads it. **Filled** is every variable at its sample\n"
          "value from `spotter-reference.md`; **empty** is the same line with\n"
          "every value missing - leading the race, lap one, nothing measured\n"
          "yet. Both matter: a template is written against the first and\n"
          "shipped against the second.\n\n"
       << "A line reading as a fragment in the **empty** column has words\n"
          "outside its `[optional groups]` that only make sense with a value.\n"
          "That is not always a bug - a cue whose emitter cannot fire without\n"
          "a position may say `P {position}` bare - but it is always worth a\n"
          "look, which is the point of generating this rather than asserting\n"
          "a rule about it.\n\n"
       << "| Cue | Filled | Empty |\n|---|---|---|\n";

    // Source order, not sorted: the pack groups cues the way the settings menu
    // does, and a reader comparing the two wants the same order in both.
    std::istringstream lines(text);
    std::string line;
    bool inCues = false;
    int rows = 0;
    while (std::getline(lines, line)) {
        const std::string s = trim(line);
        if (s.empty() || s[0] == ';') continue;
        if (s[0] == '[') { inCues = (s.rfind("[Cues]", 0) == 0); continue; }
        if (!inCues) continue;
        const size_t eq = s.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = trim(s.substr(0, eq));
        const std::string tmpl = trim(s.substr(eq + 1));
        if (tmpl.empty()) continue;          // an explicit mute says nothing
        md << "| `" << key << "` | " << SpotterCuePack::expand(tmpl, full)
           << " | " << SpotterCuePack::expand(tmpl, none) << " |\n";
        ++rows;
    }
    md << "\n" << rows << " rows.\n";
    return md.str();
}

}  // namespace

TEST_CASE("docs/spotter-pack-render.md is current") {
    const std::string generated = generatePackRender();
    const std::string path = std::string(MXB_DOCS_DIR) + "/spotter-pack-render.md";

    const std::string fresh = "/tmp/spotter-pack-render.new.md";
    {
        std::ofstream out(fresh, std::ios::binary);
        if (out.good()) out << generated;
    }

    std::ifstream in(path, std::ios::binary);
    REQUIRE_MESSAGE(in.good(),
                    "missing " << path << " - seed it with: cp " << fresh
                               << " " << path);
    std::ostringstream ss;
    ss << in.rdbuf();
    CHECK_MESSAGE(ss.str() == generated,
                  "docs/spotter-pack-render.md is stale. READ THE DIFF - it is "
                  "what the spotter now says, in both columns, and a reworded "
                  "row that reads as a fragment with its values missing shows "
                  "up there and nowhere else. Then: cp " << fresh << " " << path);
}

TEST_CASE("docs/spotter-reference.md is current") {
    const std::string generated = generateReference();
    const std::string path = std::string(MXB_DOCS_DIR) + "/spotter-reference.md";

    // Always write what this run produced - it is the input to the regen step
    // and costs nothing when the test passes.
    const std::string fresh = "/tmp/spotter-reference.new.md";
    {
        std::ofstream out(fresh, std::ios::binary);
        if (out.good()) out << generated;
    }

    std::ifstream in(path, std::ios::binary);
    REQUIRE_MESSAGE(in.good(),
                    "missing " << path << " - seed it with: cp " << fresh
                               << " " << path);
    std::ostringstream ss;
    ss << in.rdbuf();

    CHECK_MESSAGE(ss.str() == generated,
                  "docs/spotter-reference.md is stale. A cue or variable was "
                  "added, renamed, recategorised or re-described and the "
                  "generated reference no longer matches. Review the diff, "
                  "then: cp " << fresh << " " << path);
}

// Rows sharing a group must be ADJACENT, or the reference prints that heading
// twice and reads as two unrelated sections. Cheap to get wrong when adding a
// variable next to a related one rather than at the end of its block.
TEST_CASE("variable groups are contiguous") {
    std::set<std::string> closed;
    std::string current;
    for (const SpotterVars::Binding& b : SpotterVars::bindings()) {
        if (b.group != current) {
            // As std::string: a const char* captures as a bool, which prints
            // the name of every offending row as "1".
            const std::string name = b.name;
            const std::string group = b.group;
            CAPTURE(name);
            CAPTURE(group);
            CHECK_MESSAGE(closed.find(b.group) == closed.end(),
                          "group reopened after another one intervened - the "
                          "reference would print this heading twice");
            if (!current.empty()) closed.insert(current);
            current = b.group;
        }
    }
}

// A field on Vars with no row in bindings() is UNREACHABLE: no template can
// name it, and nothing else notices. The "every row reaches a distinct field"
// case above cannot see it - it walks the table, so a field the table never
// mentions is invisible to it, and its comment claimed otherwise.
//
// C++ cannot enumerate members, so this scans the header's text the same way
// the emitCue case scans the manager's. The declarations are uniform
// (`std::string name;`) which is what makes that tractable.
TEST_CASE("every field on Vars is reachable from bindings()") {
    const std::string path = std::string(MXB_CORE_DIR) + "/spotter_vars.h";
    std::ifstream in(path, std::ios::binary);
    REQUIRE_MESSAGE(in.good(), "cannot read " << path);
    std::ostringstream ss;
    ss << in.rdbuf();
    const std::string src = ss.str();

    // Only the struct body: bindings() below it mentions every member again as
    // &Vars::name, which would make the check circular.
    const size_t bodyStart = src.find("struct Vars {");
    REQUIRE(bodyStart != std::string::npos);
    const size_t bodyEnd = src.find("\n};", bodyStart);
    REQUIRE(bodyEnd != std::string::npos);
    const std::string body = src.substr(bodyStart, bodyEnd - bodyStart);

    std::set<std::string> bound;
    for (const SpotterVars::Binding& b : SpotterVars::bindings()) {
        // Recover the member name from the declaration order instead of the
        // pointer (which has no name): match by writing a marker and reading
        // the struct back is not possible textually, so compare against the
        // &Vars::<member> spellings in the table's own source.
        bound.insert(b.name);
    }

    int found = 0;
    const std::string needle = "std::string ";
    for (size_t i = body.find(needle); i != std::string::npos;
         i = body.find(needle, i + 1)) {
        const size_t start = i + needle.size();
        const size_t semi = body.find(';', start);
        if (semi == std::string::npos) break;
        const std::string member = body.substr(start, semi - start);
        // Every field carries its template name as a trailing comment; that is
        // the string bindings() must contain, and it is the one a pack author
        // types. A field without that comment is itself the mistake.
        const size_t eol = body.find('\n', semi);
        const std::string rest = body.substr(semi, eol == std::string::npos
                                                        ? std::string::npos
                                                        : eol - semi);
        const size_t brace = rest.find('{');
        CAPTURE(member);
        REQUIRE_MESSAGE(brace != std::string::npos,
                        "field has no `// {name}` comment naming the variable "
                        "it backs, so neither a reader nor this check can tell "
                        "what template word reaches it");
        const size_t close = rest.find('}', brace);
        REQUIRE(close != std::string::npos);
        const std::string name = rest.substr(brace + 1, close - brace - 1);
        CAPTURE(name);
        CHECK_MESSAGE(bound.count(name) == 1,
                      "field is not in bindings() - no template can ever name "
                      "it, and the reference will not list it either");
        ++found;
    }
    // A scan that matched nothing would pass vacuously.
    CHECK(found >= 40);
}
