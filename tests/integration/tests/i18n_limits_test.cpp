// ============================================================================
// tests/integration/tests/i18n_limits_test.cpp
// HOW LONG A TRANSLATION MAY BE: the room every settings label has, measured
// in the real panel, kept in tests/fixtures/i18n_limits.json.
//
// A label is drawn in a fixed column: the control after it starts a fixed
// number of characters in (a text with nothing after it, such as a tab name
// or a heading, is held by its column's edge). English fits by construction; a translation that is
// longer runs into the control (what the pseudo-locale showed on every tab).
// The room cannot be read from the source -- it depends on which helper drew
// the row, its column run and the tab -- so it is measured here, in English,
// across every tab with developer mode on (the most rows).
//
// The fixture feeds two things: tools/i18n/pot.sh writes each limit into the
// translation template as "#. max N" (what Poedit shows the translator), and
// the plugin reads those notes back at load and shows English for a pack line
// longer than its limit (core/i18n.cpp), so a long line never overlaps.
//
// TO REGENERATE after a layout change: this test writes the measured limits to
// Z:\tmp\mxbmrp3-tests\i18n_limits.new.json when they differ; copy it over
// tests/fixtures/i18n_limits.json, then run tools/i18n/pot.sh --update.
// ============================================================================
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

#include <windows.h>

#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>

namespace {

const std::string kFixtureWine = "Z:\\tmp\\mxbmrp3-tests\\fixtures\\i18n_limits.json";
const std::string kNewWine = "Z:\\tmp\\mxbmrp3-tests\\i18n_limits.new.json";

// Texts (tab + where the text starts, which a longer translation does not
// move) where the ENGLISH already runs into
// the next text -- a layout matter, not a translation one; the pseudo pass
// below holds every other row to fitting. Filled by the first test case.
std::set<std::string> s_englishOverlaps;

// Every msgid in the shipped template (single-line msgids: labels are short).
std::map<std::string, int> templateIds(const std::string& potPath) {
    std::map<std::string, int> ids;
    std::ifstream in(potPath);
    std::string line, ctx;
    while (std::getline(in, line)) {
        if (line.rfind("msgctxt ", 0) == 0) { ctx = line; continue; }
        if (line.rfind("msgid \"", 0) == 0 && line.size() > 8) {
            if (ctx.empty()) ids[line.substr(7, line.size() - 8)] = 0;   // context entries are tooltips
            ctx.clear();
        }
    }
    return ids;
}

}  // namespace

TEST_CASE("i18n limits: every settings label's room, measured in English, matches the fixture") {
    PluginHost host(dllPath());
    const char* save = "Z:\\tmp\\mxbmrp3-tests\\i18n_limits\\";
    host.writeSettingsFile(save, "[Settings]\nversion=11\n\n[Advanced]\ndeveloperMode=1\n");
    REQUIRE(host.loaded());
    REQUIRE(host.startup(save) >= 0);
    host.showSettings(true);

    const std::map<std::string, int> ids = templateIds("Z:\\tmp\\mxbmrp3-tests\\fixtures\\template.pot");
    REQUIRE(ids.size() > 1000);
    std::map<std::string, int> limits;
    for (const std::string& tab : host.settingsAllTabNames()) {
        host.setActiveTab(tab.c_str());
        host.draw();
        for (const PluginHost::TextRoom& r : host.settingsTextRoom()) {
            if (r.overlap) s_englishOverlaps.insert(tab + "@" + r.where);
            if (!ids.count(r.text)) continue;
            // One character of air before the control; English that already
            // fills its column keeps its own length (it is the baseline). A
            // right-aligned tag (room -1) grows into the label before it, so
            // it keeps its English length.
            int max = r.room < 0 ? 0 : r.room - 1;
            if (max < static_cast<int>(r.text.size())) max = static_cast<int>(r.text.size());
            auto it = limits.find(r.text);
            if (it == limits.end() || max < it->second) limits[r.text] = max;
        }
    }
    REQUIRE(limits.size() > 200);
    MESSAGE(s_englishOverlaps.size() << " row(s) already run into the next text in English");

    std::ostringstream json;
    json << "{\n";
    size_t n = 0;
    for (const auto& kv : limits) {
        std::string key;
        for (char ch : kv.first) {
            if (ch == '"' || ch == '\\') key += '\\';
            key += ch;
        }
        json << "  \"" << key << "\": " << kv.second << (++n < limits.size() ? ",\n" : "\n");
    }
    json << "}\n";
    std::ifstream fin(kFixtureWine);
    std::stringstream have;
    have << fin.rdbuf();
    if (have.str() != json.str()) {
        std::ofstream(kNewWine, std::ios::binary) << json.str();   // LF, like the checked-in fixture
    }
    const std::string how = "settings label room changed: copy " + kNewWine +
                            " over tests/fixtures/i18n_limits.json, then run tools/i18n/pot.sh --update";
    CHECK_MESSAGE(have.str() == json.str(), how);
}

// Every settings tab drawn with `language` active, the shipped template
// (whose limits the plugin enforces) put where the install has it. Returns
// the texts that run into the next one, apart from rows that already do in
// English; `rows` counts every text seen and `seen` collects them.
std::string overlapsUnder(const std::string& save, const char* language, int& rows, std::set<std::string>& seen) {
    // The shipped template sits beside the packs in the install; the test tree
    // only gets what Documents syncs, so put it where the plugin reads it.
    CreateDirectoryA("plugins", nullptr);
    CreateDirectoryA("plugins\\mxbmrp3_data", nullptr);
    CreateDirectoryA("plugins\\mxbmrp3_data\\languages", nullptr);
    REQUIRE(CopyFileA("Z:\\tmp\\mxbmrp3-tests\\fixtures\\template.pot",
                      "plugins\\mxbmrp3_data\\languages\\template.pot", FALSE));
    // That tree is shared by every test of the run: take it away again, or the
    // next test's own made-up pack is held to the shipped rooms.
    struct Remove { ~Remove() { DeleteFileA("plugins\\mxbmrp3_data\\languages\\template.pot"); } } remove;
    PluginHost host(dllPath());
    host.writeSettingsFile(save.c_str(), (std::string("[Settings]\nversion=11\n\n[Advanced]\ndeveloperMode=1\n\n[Display]\nlanguage=") +
                                          language + "\n").c_str());
    REQUIRE(host.loaded());
    REQUIRE(host.startup(save.c_str()) >= 0);
    host.showSettings(true);
    rows = 0;
    std::string overlaps;
    for (const std::string& tab : host.settingsAllTabNames()) {
        host.setActiveTab(tab.c_str());
        host.draw();
        for (const PluginHost::TextRoom& r : host.settingsTextRoom()) {
            ++rows;
            seen.insert(r.text);
            if (s_englishOverlaps.count(tab + "@" + r.where)) continue;
            if (r.overlap) overlaps += tab + ": " + r.text + " (room " + std::to_string(r.room) + ", then " + r.next + ")\n";
        }
    }
    return overlaps;
}

// A pack folder under the test's Documents tree.
std::string packDir(const std::string& save, const char* folder) {
    CreateDirectoryA(save.c_str(), nullptr);
    CreateDirectoryA((save + "mxbmrp3").c_str(), nullptr);
    CreateDirectoryA((save + "mxbmrp3\\languages").c_str(), nullptr);
    const std::string dir = save + "mxbmrp3\\languages\\" + folder;
    CreateDirectoryA(dir.c_str(), nullptr);
    return dir;
}

TEST_CASE("i18n limits: under the pseudo-locale no settings text runs into the next") {
    // The pseudo-locale stretches every line to the most its room allows (the
    // limits above, read back from the shipped template), so a clean pass here
    // is every tab fitting a translation at its longest.
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\i18n_limits_pseudo\\";
    std::ofstream(packDir(save, "pseudo") + "\\language.ini") << "[pack]\nname = Pseudo (test)\npseudo = 1\n";
    int rows = 0;
    std::set<std::string> seen;
    const std::string overlaps = overlapsUnder(save, "pseudo", rows, seen);
    CHECK(rows > 500);
    CHECK_MESSAGE(overlaps.empty(), overlaps);
}

// A shipped pack, as players get it: what the limits did not catch (a text
// the measure cannot see) shows here. `a` and `b` are two of its texts (CP1252,
// as the plugin draws them) that must be on screen, so an empty or unloaded
// pack - which would draw English and pass - fails.
void checkShippedPack(const char* folder, const char* a, const char* b) {
    const std::string save = std::string("Z:\\tmp\\mxbmrp3-tests\\i18n_limits_") + folder + "\\";
    const std::string dir = packDir(save, folder);
    const std::string from = std::string("Z:\\tmp\\mxbmrp3-tests\\fixtures\\") + folder + "\\";
    REQUIRE(CopyFileA((from + "language.ini").c_str(), (dir + "\\language.ini").c_str(), FALSE));
    REQUIRE(CopyFileA((from + "strings.po").c_str(), (dir + "\\strings.po").c_str(), FALSE));
    int rows = 0;
    std::set<std::string> seen;
    const std::string overlaps = overlapsUnder(save, folder, rows, seen);
    CHECK(rows > 500);
    CHECK(seen.count(a) == 1);
    CHECK(seen.count(b) == 1);
    CHECK_MESSAGE(overlaps.empty(), overlaps);
}

TEST_CASE("i18n limits: the shipped Swedish pack runs no settings text into the next") {
    // The sidebar's General tab and the Appearance tab's Language row.
    checkShippedPack("sv", "Allm\xe4nt", "Spr\xe5k");
}

TEST_CASE("i18n limits: the shipped French pack runs no settings text into the next") {
    checkShippedPack("fr", "G\xe9n\xe9ral", "Langue");
}

TEST_CASE("i18n limits: the shipped Portuguese pack runs no settings text into the next") {
    checkShippedPack("pt", "Geral", "Idioma");
}

TEST_CASE("i18n limits: the shipped German pack runs no settings text into the next") {
    checkShippedPack("de", "Allgemein", "Sprache");
}

TEST_CASE("i18n limits: the shipped Spanish pack runs no settings text into the next") {
    checkShippedPack("es", "Apariencia", "Idioma");
}
