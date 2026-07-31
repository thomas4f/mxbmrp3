// ============================================================================
// tests/integration/tests/overlay_snapshot_test.cpp
// The C++ half of the overlay's DATA CONTRACT: the plugin still emits the
// snapshot shape the web overlay is written against.
//
// THE GAP THIS CLOSES. The two sides of that boundary were each tested alone
// and neither noticed the other. The C++ tests assert /api/state by reading the
// fields they expect; the Playwright suite drives `?demo`, whose snapshot is
// hand-written in overlay-demo.js. Rename a field in buildJsonSnapshot() and
// BOTH suites stay green while the live overlay silently renders nothing — the
// demo keeps producing the old shape, and no test ever feeds real plugin output
// to the real client. cpp_js_parity.json mechanises the mirrored HELPERS across
// that boundary; nothing mechanised the snapshot itself.
//
// HOW IT WORKS. This test drives a small but representative race and compares
// the live snapshot against the committed fixture — by KEY PATH AND JSON TYPE,
// never by value. Values move with timing and lap data (and would make the
// fixture flap); the paths and their types are the contract. Its twin,
// tests/web/tests/overlay_snapshot.spec.js, feeds the SAME committed file
// through the real render() and asserts the overlay draws it. So:
//
//   * a field renamed/removed in the plugin      -> THIS test fails
//   * regenerate the fixture, and if the overlay still reads the old name
//     -> the WEB test fails
//
// One file, two suites, and they can only pass together — the shape
// cpp_js_parity.json already uses.
//
// TO REGENERATE after a deliberate snapshot change: this test writes the live
// snapshot to Z:\tmp\mxbmrp3-tests\overlay_snapshot.new.json whenever it does
// not match, and prints the copy command. Review the diff — a key that vanished
// is exactly what this is for — then copy it over tests/fixtures/ and rerun.
// Self-contained doctest; see run_tests.sh.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include "assertions.h"

#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>

namespace {

constexpr int RACE = 6;
// std::string, not const char*: doctest's message builder streams a bare
// `const char*` through the integral promotion path and prints "1" instead of
// the text, which turned the regeneration instructions below into nonsense.
const std::string kFixtureWine = "Z:\\tmp\\mxbmrp3-tests\\fixtures\\overlay_snapshot.json";
const std::string kRegenWine = "Z:\\tmp\\mxbmrp3-tests\\overlay_snapshot.new.json";
const std::string kFixtureRepo = "tests/fixtures/overlay_snapshot.json";

// Every key path in the document, with the JSON type found there. Arrays
// contribute one "<path>[]" entry and the UNION of their elements' paths, so a
// field present on only some rider rows (chips, sector times) still counts.
// Types are recorded because an int that becomes a string is a contract break
// the client would render as garbage, and the path alone would not see it.
void collect(const nlohmann::json& v, const std::string& path,
             std::map<std::string, std::string>& out) {
    auto typeName = [](const nlohmann::json& j) -> std::string {
        if (j.is_boolean()) return "bool";
        if (j.is_number_integer() || j.is_number_unsigned()) return "int";
        if (j.is_number_float()) return "float";
        if (j.is_string()) return "string";
        if (j.is_array()) return "array";
        if (j.is_object()) return "object";
        return "null";
    };
    if (v.is_object()) {
        for (auto it = v.begin(); it != v.end(); ++it) {
            const std::string child = path.empty() ? it.key() : path + "." + it.key();
            // A null is a VALUE, not a shape: the same field can be null on one
            // run and populated on another. Record the path, keep the type of
            // whichever run saw real data, and never let null overwrite it.
            const std::string t = typeName(it.value());
            auto ins = out.emplace(child, t);
            if (!ins.second && ins.first->second == "null" && t != "null")
                ins.first->second = t;
            collect(it.value(), child, out);
        }
    } else if (v.is_array()) {
        out.emplace(path + "[]", "element");
        for (const auto& e : v) collect(e, path + "[]", out);
    }
}

std::map<std::string, std::string> shapeOf(const nlohmann::json& doc) {
    std::map<std::string, std::string> m;
    collect(doc, "", m);
    return m;
}

// Drives a race with enough going on that the fixture exercises the parts of the
// snapshot the overlay actually renders: a full grid, gaps and laps, split
// times, live gaps from track position, a spectate target, and event-log rows.
void driveRace(PluginHost& host) {
    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(RACE, /*numLaps=*/8, /*lengthMs=*/0);

    // Alice gets a bike name from PluginUtils' brand map so `brand`/`brandColor`
    // are emitted; the others keep the harness default, which resolves to no
    // brand — a fixture where every row is identical would not notice a field
    // that only appears on some.
    host.addEntry(10, "Alice", "FACTORY CRF450R");
    host.addEntry(22, "Bob");
    host.addEntry(7, "Carol");
    host.addEntry(46, "Dave");

    const std::vector<ClassRow> grid = {
        { .num = 10, .laps = 1, .gap = 0 },
        { .num = 22, .laps = 1, .gap = 900 },
        { .num = 7,  .laps = 1, .gap = 2400 },
        { .num = 46, .laps = 1, .gap = 5100 },
    };
    host.classify(RACE, 60000, grid);
    host.raceTrackPosition({ { 10, 0.30f }, { 22, 0.26f }, { 7, 0.20f }, { 46, 0.10f } });

    // Laps + splits: populates lap times, personal bests, sector data and the
    // fastest-lap panel the overlay renders from.
    host.raceSplit(RACE, 10, 1, 0, 31000);
    host.raceSplit(RACE, 10, 1, 1, 62000);
    host.raceLap(RACE, 10, 1, 94000, /*best=*/94000);
    host.raceSplit(RACE, 22, 1, 0, 31500);
    host.raceSplit(RACE, 22, 1, 1, 63000);
    host.raceLap(RACE, 22, 1, 95200, /*best=*/95200);
    host.raceLap(RACE, 7, 1, 96800, /*best=*/96800);
    // One INVALID lap: the per-lap "v" array is emitted only when some lap in the
    // set is invalid, so without this the whole field is absent from the fixture
    // and a rename of it would pass both halves.
    host.raceLap(RACE, 46, 1, 99100, /*best=*/99100, /*split0=*/-1, /*split1=*/-1,
                 /*invalid=*/true);

    const std::vector<ClassRow> lap2 = {
        { .num = 10, .laps = 2, .gap = 0 },
        { .num = 22, .laps = 2, .gap = 1200 },
        { .num = 7,  .laps = 2, .gap = 3000 },
        { .num = 46, .laps = 2, .gap = 6400 },
    };
    host.classify(RACE, 160000, lap2);
    host.raceTrackPosition({ { 10, 0.62f }, { 22, 0.55f }, { 7, 0.44f }, { 46, 0.30f } });

    // A spectate target, so the spectate/director-facing fields are populated
    // rather than defaulted away.
    host.spectateVehicles({ { 10, "Alice" }, { 22, "Bob" } }, /*selected=*/1);
    host.draw();
}

// `plateColor` is emitted only for a rider TrackedRidersManager knows with a
// non-zero colour, and that manager loads its JSON from the save dir during
// Startup — so the file has to exist before the plugin loads, not after.
void seedTrackedRider(const std::string& saveDir, const std::string& riderName) {
    // Under the plugin's own `mxbmrp3\` subdirectory, not the save root — that is
    // where TrackedRidersManager::getFilePath() looks. Writing it one level up
    // silently loads nothing, and the only symptom is the field quietly missing
    // from the snapshot, which is the failure this whole file exists to prevent.
    const std::string dir = saveDir + "mxbmrp3\\";
    std::filesystem::create_directories(dir);
    std::ofstream f(dir + "mxbmrp3_tracked_riders.json", std::ios::binary);
    REQUIRE(f.good());
    // The stored hex is the low 24 bits of the game's ABGR word, i.e. BGR order
    // (TrackedRidersManager writes `color & 0xFFFFFF` and parses it straight
    // back, so the file round-trips) — despite the loader's "#RRGGBB" comment.
    // "#ff3300" therefore surfaces as plateColor "#0033ff" in the snapshot. The
    // value is irrelevant to this test, which compares paths and types; it is
    // called out so the fixture's blue plate does not read as a defect.
    f << "{\"version\":1,\"riders\":[{\"name\":\"" << riderName
      << "\",\"color\":\"#ff3300\"}]}\n";
}

}  // namespace

TEST_CASE("overlay data contract: the live snapshot keeps the committed fixture's shape") {
    const std::string saveDir = "Z:\\tmp\\mxbmrp3-tests\\overlaysnap\\";
    seedTrackedRider(saveDir, "Bob");

    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(saveDir.c_str());
    driveRace(host);

    const nlohmann::json live = host.snapshot();
    REQUIRE(live.is_object());

    // Sanity floor, so a snapshot that came back structurally empty fails HERE
    // with a clear reason rather than as a thousand missing paths below.
    REQUIRE(live.contains("standings"));
    REQUIRE(live["standings"].size() == 4);

    std::ifstream in(kFixtureWine.c_str());
    const bool haveFixture = in.good();
    nlohmann::json fixture;
    if (haveFixture) {
        try {
            in >> fixture;
        } catch (const std::exception& e) {
            FAIL("fixture " << kFixtureRepo << " is not valid JSON: " << e.what());
        }
    }

    // Always write what this run produced; it is the input to the regen step and
    // costs nothing when the test passes.
    {
        // Binary mode on purpose: this file runs under Wine, where text mode
        // writes CRLF. The regenerated fixture would then differ from the
        // committed (LF) one on every single line, burying the ONE key that
        // actually changed — the diff a regenerator is told to review.
        std::ofstream out(kRegenWine.c_str(), std::ios::binary);
        if (out.good()) out << live.dump(2) << "\n";
    }

    REQUIRE_MESSAGE(haveFixture,
                    "missing fixture " << kFixtureRepo << " — run_tests.sh copies it to "
                    << kFixtureWine << ". Seed it with: cp /tmp/mxbmrp3-tests/overlay_snapshot.new.json "
                    << kFixtureRepo);

    const auto want = shapeOf(fixture);
    const auto got = shapeOf(live);

    std::ostringstream drift;
    for (const auto& [path, type] : want) {
        auto it = got.find(path);
        if (it == got.end())
            drift << "\n  MISSING from the live snapshot: " << path << " (" << type << ")";
        else if (it->second != type && it->second != "null" && type != "null")
            drift << "\n  TYPE CHANGED: " << path << " was " << type << ", now " << it->second;
    }
    for (const auto& [path, type] : got)
        if (!want.count(path))
            drift << "\n  NEW in the live snapshot: " << path << " (" << type << ")";

    const std::string report = drift.str();
    CHECK_MESSAGE(report.empty(),
                  "the /api/state shape drifted from the committed fixture:" << report
                  << "\n\nA MISSING or renamed path is a field the web overlay may still be "
                     "reading — check mxbmrp3_data/web/js before regenerating.\n"
                     "To accept this shape:  cp /tmp/mxbmrp3-tests/overlay_snapshot.new.json "
                  << kFixtureRepo
                  << "\nthen rerun the Playwright suite, which renders that same file.");
}
