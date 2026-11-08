// ============================================================================
// tests/integration/tests/spotter_language_test.cpp
// THE SPOTTER SPEAKS THE MENUS' LANGUAGE, IN THE REAL PLUGIN.
//
// The SHIPPED Swedish pack (language.ini, strings.po and spotter.ini, copied
// from the repo by run_tests.sh) is installed the way a player installs one -
// in Documents, synced across at startup - and picked by name in the INI. What
// only the real DLL can show:
//  1. The language's spotter.ini is found beside its strings.po and laid over
//     the spotter pack: a Swedish line is spoken.
//  2. Its [Words] reach the words the plugin assembles ("femton minuter"), and
//     a session name comes from the menus' translation, carried from the
//     font's code page into the voice's UTF-8 ("Kval", "Tr\xC3\xA4ning").
//  3. An achievement title is the translated one too.
//  4. Switching the language at runtime (the Appearance tab's Language row)
//     switches the spotter with it, words included: back to English, and to
//     the English spotter pack's line.
//  5. A voice pack the player selected keeps the spotter in ITS language:
//     no Swedish words or session names inside its English lines.
// The pure halves (the [Words] parse, the census, the shipped file's
// completeness) are tests/unit/test_spotter_language.cpp's.
// ============================================================================
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include "ini.h"

#include <windows.h>

#include <string>

namespace {

constexpr int QUALIFY = 4;
constexpr int PRACTICE = 1;

std::string slurp(const std::string& path) {
    std::string out = ini::readFile(path);
    REQUIRE_MESSAGE(!out.empty(), "fixture missing: " << path);
    return out;
}

void writeFile(const std::string& path, const std::string& contents) {
    HANDLE h = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    REQUIRE(h != INVALID_HANDLE_VALUE);
    DWORD written = 0;
    WriteFile(h, contents.data(), static_cast<DWORD>(contents.size()), &written, nullptr);
    CloseHandle(h);
}

// The synced copies live in the plugins tree every test of the run shares:
// take them away again, or a later test inherits a Swedish spotter.
struct Synced {
    ~Synced() {
        const std::string lang = "plugins\\mxbmrp3_data\\languages\\sv";
        for (const char* f : { "\\language.ini", "\\strings.po", "\\spotter.ini" }) {
            DeleteFileA((lang + f).c_str());
        }
        RemoveDirectoryA(lang.c_str());
        for (const char* name : { "default", "myvoice" }) {
            const std::string pack = std::string("plugins\\mxbmrp3_data\\spotters\\") + name;
            DeleteFileA((pack + "\\spotter.ini").c_str());
            RemoveDirectoryA(pack.c_str());
        }
    }
};

// A Documents folder holding the shipped Swedish pack, as a player would drop
// it in, and two English spotter packs: a stand-in for the shipped `default`
// (the test tree carries none) and a voice pack of the player's own.
void stage(const std::string& save) {
    const std::string root = save + "mxbmrp3";
    CreateDirectoryA(save.c_str(), nullptr);
    CreateDirectoryA(root.c_str(), nullptr);
    DeleteFileA((root + "\\mxbmrp3_stats.json").c_str());   // Tinkerer earns once
    const std::string fixtures = "Z:\\tmp\\mxbmrp3-tests\\fixtures\\sv\\";
    CreateDirectoryA((root + "\\languages").c_str(), nullptr);
    CreateDirectoryA((root + "\\languages\\sv").c_str(), nullptr);
    for (const char* f : { "language.ini", "strings.po", "spotter.ini" }) {
        writeFile(root + "\\languages\\sv\\" + f, slurp(fixtures + f));
    }
    CreateDirectoryA((root + "\\spotters").c_str(), nullptr);
    CreateDirectoryA((root + "\\spotters\\default").c_str(), nullptr);
    writeFile(root + "\\spotters\\default\\spotter.ini",
              "[Cues]\n"
              "practice_started = {session_name} underway[, {session_length}].\n"
              "quali_started = {session_name} underway[, {session_length}].\n");
    CreateDirectoryA((root + "\\spotters\\myvoice").c_str(), nullptr);
    writeFile(root + "\\spotters\\myvoice\\spotter.ini",
              "[Cues]\nquali_started = {session_name} is go[, {session_length}].\n");
}

void openSession(PluginHost& host, const std::string& save, const char* spotterIni) {
    REQUIRE(host.loaded());
    host.writeSettingsFile(save.c_str(), std::string("[Settings]\nversion=11\n\n[Display]\nlanguage=sv\n\n"
                                                     "[Spotter]\nsubtitles=1\n") + spotterIni);
    REQUIRE(host.startup(save.c_str()) >= 0);
    host.spotterSubtitles(true);
    host.spotterCategoryMask(0x1Fu | (1u << 6));   // racing + Achievements
    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
}

}  // namespace

TEST_CASE("spotter language: the menus' language speaks its own spotter, and switches with them") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\spotter_sv\\";
    stage(save);
    const Synced cleanup;
    PluginHost host(dllPath());
    openSession(host, save, "");

    // --- 1 + 2. The Swedish line, the translated session name, Swedish words.
    host.session(QUALIFY, 0, 900000);
    CHECK_MESSAGE(host.spotterCueLog().find("Kval \xC3\xA4r ig\xC3\xA5ng, femton minuter.") !=
                      std::string::npos,
                  host.spotterCueLog());

    // --- 3. An achievement: the Swedish pack's own line, with its title.
    host.configReloaded();
    CHECK_MESSAGE(host.spotterCueLog().find("Mekare.") != std::string::npos, host.spotterCueLog());

    // --- 4. Back to English at runtime: the English pack's line and words.
    REQUIRE(host.setLanguage(""));
    host.session(PRACTICE, 0, 480000);
    const std::string log = host.spotterCueLog();
    CHECK_MESSAGE(log.find("Practice underway, eight minutes.") != std::string::npos, log);
    CHECK(log.find("Tr\xC3\xA4ning") == std::string::npos);

    host.shutdown();
}

// --- 5. A voice pack of your own speaks its own words: its English line is
// not filled with Swedish numbers or a Swedish session name ("Kval is go,
// femton minuter"), whatever the menus are in.
TEST_CASE("spotter language: a selected voice pack keeps the spotter in one language") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\spotter_sv_pack\\";
    stage(save);
    const Synced cleanup;
    PluginHost host(dllPath());
    openSession(host, save, "pack=myvoice\n");

    host.session(QUALIFY, 0, 900000);
    const std::string log = host.spotterCueLog();
    CHECK_MESSAGE(log.find("Qualify is go, fifteen minutes.") != std::string::npos, log);
    CHECK(log.find("femton") == std::string::npos);
    CHECK(log.find("Kval") == std::string::npos);
    host.shutdown();
}
