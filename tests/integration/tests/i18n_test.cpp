// ============================================================================
// tests/integration/tests/i18n_test.cpp
// LANGUAGE PACKS IN THE REAL PLUGIN: A PACK IN THE PLAYER'S DOCUMENTS FOLDER,
// PICKED BY NAME IN THE INI, CHANGES WHAT THE SETTINGS MENU DRAWS.
//
// The reader, the lookup and the pseudo-locale are tests/unit/test_i18n.cpp's.
// What only the real DLL can show is the path between them: the pack is copied
// out of Documents by the same sync every pack type uses (AssetManager's
// PACK_TYPES row), [Display] language= selects it by folder name at the first
// settings load, a translated TR() label reaches the row the menu draws, and
// the Find box -- which indexes what is drawn -- finds that row by the
// translation AND by its English, its translated tooltip included. A fuzzy
// entry (its English changed since it was translated) must show the English,
// a name with no folder must show English without failing the load, and an INI
// with no language at all follows Windows' display language.
//
// The rows are read through the Find index rather than pixels: it holds exactly
// the label each row drew, and the General tab's labels are the ones translated
// so far.
// ============================================================================
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include "ini.h"

#include <windows.h>

#include <algorithm>
#include <string>
#include <vector>

namespace {

void writeFile(const std::string& path, const std::string& contents) {
    HANDLE h = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    REQUIRE(h != INVALID_HANDLE_VALUE);
    DWORD written = 0;
    WriteFile(h, contents.data(), static_cast<DWORD>(contents.size()), &written, nullptr);
    CloseHandle(h);
}

// A pack the way a player installs one: Documents\...\mxbmrp3\languages\<name>\.
// The plugin copies it into plugins\mxbmrp3_data\languages, a tree every test
// of the run shares: the guard takes that copy away again, or a later test
// that writes no language key follows Windows into this test's pack (the
// system case stages one for Wine's "en").
struct SyncedPack {
    std::string dir;
    ~SyncedPack() {
        DeleteFileA((dir + "\\language.ini").c_str());
        DeleteFileA((dir + "\\strings.po").c_str());
        RemoveDirectoryA(dir.c_str());
    }
};

[[nodiscard]] SyncedPack stagePack(const std::string& save, const char* name, const std::string& ini,
                                   const std::string& po) {
    const std::string root = save + "mxbmrp3";
    CreateDirectoryA(save.c_str(), nullptr);
    CreateDirectoryA(root.c_str(), nullptr);
    CreateDirectoryA((root + "\\languages").c_str(), nullptr);
    const std::string dir = root + "\\languages\\" + name;
    CreateDirectoryA(dir.c_str(), nullptr);
    writeFile(dir + "\\language.ini", ini);
    if (!po.empty()) writeFile(dir + "\\strings.po", po);
    return SyncedPack{std::string("plugins\\mxbmrp3_data\\languages\\") + name};
}

struct Row { std::string tab, label, id; };

// The Find page's hits as tab|label|value|id|section lines.
std::vector<Row> find(PluginHost& host, const char* query) {
    std::vector<std::string> lines;
    host.settingsFind(query, &lines);
    std::vector<Row> out;
    for (const std::string& l : lines) {
        const size_t a = l.find('|'), b = l.find('|', a + 1), c = l.find('|', b + 1), d = l.find('|', c + 1);
        if (d == std::string::npos) continue;
        out.push_back({ l.substr(0, a), l.substr(a + 1, b - a - 1), l.substr(c + 1, d - c - 1) });
    }
    return out;
}

const Row* byId(const std::vector<Row>& rows, const char* id) {
    for (const Row& r : rows) {
        if (r.id == id) return &r;
    }
    return nullptr;
}

void open(PluginHost& host, const char* save) {
    REQUIRE(host.loaded());
    REQUIRE(host.startup(save) >= 0);
    host.showSettings(true);
    host.setActiveTab("General");
    host.draw();
}

// "Autospara" with the a-ring and o-umlaut as UTF-8, the way Poedit saves it.
const char* kSvPo =
    "msgid \"\"\n"
    "msgstr \"\"\n"
    "\"Content-Type: text/plain; charset=UTF-8\\n\"\n"
    "\n"
    "msgid \"Auto-save\"\n"
    "msgstr \"Spara automatiskt\"\n"
    "\n"
    "msgid \"PB scope\"\n"
    "msgstr \"PB-omf\xC3\xA5ng\"\n"
    "\n"
    "msgid \"Screen clamp\"\n"
    "msgstr \"H\xC3\xA5ll inom sk\xC3\xA4rmen\"\n"
    "\n"
    "#, fuzzy\n"
    "msgid \"Grid snap\"\n"
    "msgstr \"F\xC3\xA4st mot rutn\xC3\xA4t\"\n"
    "\n"
    "msgctxt \"tooltip\"\n"
    "msgid \"Auto-saves on leaving the track (never while riding); use Save anytime. Disable to edit INI.\"\n"
    "msgstr \"Sparar n\xC3\xA4r du l\xC3\xA4mnar banan (aldrig under k\xC3\xB6rning).\"\n";

}  // namespace

TEST_CASE("i18n: no language picked draws English") {
    PluginHost host(dllPath());
    open(host, "Z:\\tmp\\mxbmrp3-tests\\i18n_english\\");
    const std::vector<Row> rows = find(host, "auto");
    const Row* r = byId(rows, "general.auto_save");
    REQUIRE(r);
    CHECK(r->label == "Auto-save");
}

TEST_CASE("i18n: a pack in Documents, picked by name, translates the rows it covers") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\i18n_sv\\";
    const SyncedPack synced = stagePack(save, "sv", "[pack]\nname = Svenska\n", kSvPo);
    PluginHost host(dllPath());
    host.writeSettingsFile(save.c_str(), "[Settings]\nversion=11\n\n[Display]\nlanguage=sv\n");
    open(host, save.c_str());

    // By the translation...
    std::vector<Row> rows = find(host, "spara");
    const Row* r = byId(rows, "general.auto_save");
    REQUIRE_MESSAGE(r, "the Swedish label did not reach the drawn row");
    CHECK(r->label == "Spara automatiskt");
    // ...and by the English a forum post would use. "scope" is in neither the
    // Swedish label nor the row's tooltip (either language), so only the
    // English-label match can find it: through the very pointer TR() returned.
    rows = find(host, "scope");
    r = byId(rows, "general.pb_scope");
    REQUIRE_MESSAGE(r, "a translated row is not found by its English name");
    CHECK(r->label == "PB-omf\xE5ng");

    // UTF-8 in the file, CP1252 on screen (the font's code page).
    rows = find(host, "skarmen");   // accent-folded: a plain keyboard finds it
    r = byId(rows, "general.screen_clamp");
    REQUIRE(r);
    CHECK(r->label == "H\xE5ll inom sk\xE4rmen");

    // A translated TOOLTIP is what Find's description match searches...
    rows = find(host, "aldrig");
    CHECK_MESSAGE(byId(rows, "general.auto_save") != nullptr,
                  "the Swedish tooltip did not reach the Find index");
    // ...and its English still finds the row too.
    rows = find(host, "anytime");
    CHECK(byId(rows, "general.auto_save") != nullptr);

    // Fuzzy = its English changed since it was translated: show the English.
    rows = find(host, "grid");
    r = byId(rows, "general.grid_snap");
    REQUIRE(r);
    CHECK(r->label == "Grid snap");
}

TEST_CASE("i18n: the pseudo-locale brackets every translatable label") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\i18n_pseudo\\";
    const SyncedPack synced = stagePack(save, "pseudo", "[pack]\nname = Pseudo (test)\npseudo = 1\n", "");
    PluginHost host(dllPath());
    host.writeSettingsFile(save.c_str(),
        "[Settings]\nversion=11\n\n[Advanced]\ndeveloperMode=1\n\n[Display]\nlanguage=pseudo\n");
    open(host, save.c_str());
    const std::vector<Row> rows = find(host, "auto save");
    const Row* r = byId(rows, "general.auto_save");
    REQUIRE(r);
    CHECK(r->label.front() == '[');
    CHECK(r->label.find('~') != std::string::npos);
}

// Every row any tab draws, translated: under the pseudo-locale "[" is in every
// label AND every tooltip, so the query reaches each indexed row at least
// through its description -- and a label still in plain English shows up here
// instead of hiding. (The Achievements catalogue is not indexed: its own PR.)
TEST_CASE("i18n: under the pseudo-locale every row Find indexes is translated") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\i18n_every_row\\";
    const SyncedPack synced = stagePack(save, "pseudo", "[pack]\nname = Pseudo (test)\npseudo = 1\n", "");
    PluginHost host(dllPath());
    host.writeSettingsFile(save.c_str(),
        "[Settings]\nversion=11\n\n[Advanced]\ndeveloperMode=1\n\n[Display]\nlanguage=pseudo\n");
    open(host, save.c_str());
    const std::vector<Row> rows = find(host, "[");
    CHECK(rows.size() > 300);
    for (const Row& r : rows) {
        CHECK_MESSAGE((!r.label.empty() && r.label.front() == '['),
                      r.tab << " > '" << r.label << "' (" << r.id << ") is not translated");
    }
}

// What is SAVED stays English: the tab the menu reopens on is matched by its
// registry name, so a translated name would reopen on the default tab after a
// language change -- and an INI shared on a forum would read in someone else's.
TEST_CASE("i18n: the saved active tab is the English name under any language") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\i18n_saved_tab\\";
    const SyncedPack synced = stagePack(save, "pseudo", "[pack]\nname = Pseudo (test)\npseudo = 1\n", "");
    PluginHost host(dllPath());
    host.writeSettingsFile(save.c_str(),
        "[Settings]\nversion=11\n\n[Advanced]\ndeveloperMode=1\n\n[Display]\nlanguage=pseudo\n");
    REQUIRE(host.loaded());
    REQUIRE(host.startup(save.c_str()) >= 0);
    host.showSettings(true);
    host.setActiveTab("Appearance");
    host.draw();
    host.save();
    const std::string text = ini::readFile(save + "mxbmrp3\\mxbmrp3_settings.ini");
    REQUIRE(!text.empty());
    CHECK(text.find("activeTab=Appearance\n") != std::string::npos);
    CHECK(text.find("language=pseudo") != std::string::npos);
}

TEST_CASE("i18n: no language key follows Windows' display language, and saves the choice not the pack") {
    // The pack is named after whatever language Wine reports -- the part
    // before the region, as a player's "sv" serves Windows' "sv-SE" -- so the
    // test holds on any host locale.
    wchar_t wide[LOCALE_NAME_MAX_LENGTH] = {};
    REQUIRE(LCIDToLocaleName(MAKELCID(GetUserDefaultUILanguage(), SORT_DEFAULT), wide,
                             LOCALE_NAME_MAX_LENGTH, 0) > 0);
    std::string lang;
    for (const wchar_t* c = wide; *c && *c != L'-'; ++c) lang += static_cast<char>(*c);
    REQUIRE(!lang.empty());
    INFO("Windows display language: " << lang);

    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\i18n_system\\";
    const SyncedPack synced = stagePack(save, lang.c_str(), "[pack]\nname = Systemets\n", kSvPo);
    PluginHost host(dllPath());
    host.writeSettingsFile(save.c_str(), "[Settings]\nversion=11\n");
    open(host, save.c_str());
    const std::vector<Row> rows = find(host, "spara");
    const Row* r = byId(rows, "general.auto_save");
    REQUIRE_MESSAGE(r, "the pack for Windows' language was not picked with no language key");
    CHECK(r->label == "Spara automatiskt");

    host.save();
    const std::string text = ini::readFile(save + "mxbmrp3\\mxbmrp3_settings.ini");
    CHECK(text.find("language=system\n") != std::string::npos);
}

TEST_CASE("i18n: toasts and the HUDs' session clock and format follow the language; the web's stay English") {
    // The HUD side of PR 5: a toast posted in game and the overtime labels a
    // HUD draws come out bracketed under the pseudo-locale, while the copy the
    // web overlay's JSON gets is the English one. (The Event Log HUD's shown
    // text is pinned by the unit test of EventText, not drawn here.)
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\i18n_toasts\\";
    const SyncedPack synced = stagePack(save, "pseudo", "[pack]\nname = Pseudo (test)\npseudo = 1\n", "");
    PluginHost host(dllPath());
    host.writeSettingsFile(save.c_str(),
        "[Settings]\nversion=11\n\n[Advanced]\ndeveloperMode=1\n\n[Display]\nlanguage=pseudo\n");
    REQUIRE(host.loaded());
    REQUIRE(host.startup(save.c_str()) >= 0);
    REQUIRE(host.hasMessages());

    CHECK(host.formatSessionClock(3, 0, false) == "3 TO GO");
    CHECK(host.formatSessionClock(1, 0, false) == "FINAL LAP");
    const std::string hudToGo = host.formatSessionClock(3, 0, true);
    CHECK(hudToGo.rfind("[", 0) == 0);
    CHECK(hudToGo.find('3') != std::string::npos);
    CHECK(host.formatSessionClock(0, 0, true).rfind("[", 0) == 0);
    CHECK(host.formatSessionClock(-1, 65000, true) == "01:05");   // a clock is not a word
    // A time+laps race's laps after the clock: English on the web, the pack's
    // whole phrase on a HUD, the clock and the count kept.
    CHECK(host.formatSessionClock(-1, 65000, false, 2) == "01:05 + 2 laps");
    CHECK(host.formatSessionClock(-1, 65000, false, 1) == "01:05 + 1 lap");
    const std::string hudLaps = host.formatSessionClock(-1, 65000, true, 2);
    CHECK(hudLaps.rfind("[", 0) == 0);
    CHECK(hudLaps.find("01:05") != std::string::npos);
    CHECK(hudLaps.find('2') != std::string::npos);
    // The race format beside it on the Session and Timing HUDs reads the same
    // phrase, from the same translation; the Event Log's copy stays English.
    CHECK(host.formatSessionFormatWords(900000, 2, false) == "15:00 + 2 laps");
    CHECK(host.formatSessionFormatWords(0, 10, false) == "10 laps");
    const std::string hudFormat = host.formatSessionFormatWords(900000, 2, true);
    CHECK(hudFormat.rfind("[", 0) == 0);
    CHECK(hudFormat.find("15:00") != std::string::npos);
    const std::string hudLapRace = host.formatSessionFormatWords(0, 10, true);
    CHECK(hudLapRace.rfind("[", 0) == 0);
    CHECK(hudLapRace.find("10") != std::string::npos);

    host.draw();
    const unsigned int before = host.messagesPosted();
    REQUIRE(host.injectHotkey("all_elements"));
    host.draw();
    REQUIRE(host.messagesPosted() > before);
    const std::string last = host.lastMessage();
    INFO("last toast: " << last);
    CHECK(last.rfind("[", 0) == 0);   // "All HUDs hidden", bracketed
}

TEST_CASE("i18n: a language whose folder is gone shows English and loads the rest") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\i18n_missing\\";
    PluginHost host(dllPath());
    host.writeSettingsFile(save.c_str(), "[Settings]\nversion=11\n\n[Display]\nlanguage=xx\n");
    open(host, save.c_str());
    const std::vector<Row> rows = find(host, "auto");
    const Row* r = byId(rows, "general.auto_save");
    REQUIRE(r);
    CHECK(r->label == "Auto-save");
}

// A language changed while the game runs reaches every dropdown's list, not only
// the value drawn in its box. Friends' Show mode kept its names in a static table
// filled with TR() results on the tab's first draw, so after Swedish -> English
// the box read "Always" and the list still offered "Alltid / Vanner / Vid entre"
// (1.35 pre-release). Every tab is drawn under the pseudo-locale first, so a table
// like that is filled with bracketed text; after the switch to English no list
// may still offer any.
TEST_CASE("i18n: a language change reaches every dropdown's list") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\i18n_switch_lists\\";
    const SyncedPack synced = stagePack(save, "pseudo", "[pack]\nname = Pseudo (test)\npseudo = 1\n", "");
    PluginHost host(dllPath());
    host.writeSettingsFile(save.c_str(),
        "[Settings]\nversion=11\n\n[Advanced]\ndeveloperMode=1\n\n[Display]\nlanguage=pseudo\n");
    open(host, save.c_str());
    const std::vector<std::string> tabs = host.settingsAllTabNames();
    REQUIRE(tabs.size() > 20);
    std::vector<std::string> names;
    for (const std::string& tab : tabs) {
        host.setActiveTab(tab.c_str());
        host.draw();
    }
    host.setActiveTab("Friends");
    host.draw();
    REQUIRE(host.settingsListNames(&names) > 0);
    bool bracketed = false;
    for (const std::string& n : names) bracketed |= n.rfind("[", 0) == 0;
    CHECK(bracketed);   // the case is looking at translated lists at all

    REQUIRE(host.setLanguage(""));
    int listed = 0;
    for (const std::string& tab : tabs) {
        host.setActiveTab(tab.c_str());
        host.draw();
        listed += host.settingsListNames(&names);
        for (const std::string& n : names) {
            CHECK_MESSAGE(n.find('[') == std::string::npos,
                          tab << " lists '" << n << "' after the switch to English");
        }
    }
    CHECK(listed > 100);
    host.setActiveTab("Friends");
    host.draw();
    host.settingsListNames(&names);
    CHECK(std::find(names.begin(), names.end(), "Always") != names.end());
}

// The Session HUD's Format row is cut to the panel like its other rows. In
// English the longest real line fits whole (38 characters), but a pack's
// sighting-lap line ran 12+ characters past the panel's right edge.
TEST_CASE("i18n: the Session HUD's Format row is cut to the panel in a long language") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\i18n_session_format\\";
    const char* po =
        "msgid \"\"\nmsgstr \"\"\n\"Content-Type: text/plain; charset=UTF-8\\n\"\n\n"
        "msgid \"Race 2\"\nmsgstr \"Carrera 2\"\n\n"
        "msgid \"Sighting Lap\"\nmsgstr \"Vuelta de Reconocimiento\"\n\n"
        "#, c-format\nmsgid \"%s + %d laps\"\nmsgstr \"%s + %d vueltas\"\n";
    const SyncedPack synced = stagePack(save, "es", "[pack]\nname = Espanol\n", po);
    PluginHost host(dllPath());
    host.writeSettingsFile(save.c_str(), "[Settings]\nversion=11\n\n[Display]\nlanguage=es\n");
    REQUIRE(host.loaded());
    REQUIRE(host.startup(save.c_str()) >= 0);
    host.setHudVisible("session_widget", true);   // off by default
    // Online, so the row leads with the session name.
    host.eventInit("TestTrack", "Alice", 1600.0f, 2, "Test 450", "MX1", "", /*serverType=*/1, "Server");
    host.raceEvent("TestTrack");
    host.addEntry(10, "Alice");
    host.addEntry(22, "Bob");
    host.session(/*Race 2*/ 7, /*numLaps=*/10, /*lengthMs=*/900000, /*sighting lap*/ 64);
    host.draw();

    std::string row;
    for (const auto& r : host.hudStringRows(PluginHost::HUD_SESSION)) {
        if (r.text.rfind("Carrera 2", 0) == 0) row = r.text;
    }
    // Whole: "Carrera 2 (15:00 + 10 vueltas), Vuelta de Reconocimiento" (55).
    // Cut at the panel's 40, plainly (no "...").
    CHECK(row == "Carrera 2 (15:00 + 10 vueltas), Vuelta d");
}
