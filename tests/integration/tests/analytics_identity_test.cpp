// ============================================================================
// tests/integration/tests/analytics_identity_test.cpp
// A corrupt analytics file is repaired, not carried for life.
//
// THE BUG. mxbmrp3_analytics.json holds the anonymous install id and the launch
// counter. A file that existed but did not parse was treated like a locked one:
// a throwaway id for this launch and the file left alone. A lock clears; a
// corrupt file does not, and the plugin never rewrote it -- so every later
// launch of that install carried a fresh id and reached the usage survey as a
// brand-new install that never returned. The survey drops such launches, and
// their share was rising week over week: a population that only grows.
//
// Case 1: a file truncated mid-way still holds the id, so the load recovers it,
// counts this launch as the first it can vouch for, reports the repair once,
// and rewrites the file -- the NEXT load parses cleanly, same id, count 2, no
// repair flag. Case 2: a file with no id in it gets a new one, once. Case 3: a
// missing file is a first run and reports no repair; a whole file is read as
// before. The identity load is driven directly through a hook, since the
// harness's analytics seam otherwise fakes the identity (testPrime).
// ============================================================================
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {
constexpr const char* SAVE = "Z:\\tmp\\mxbmrp3-tests\\analytics_identity\\";
constexpr const char* FILE_PATH = "Z:\\tmp\\mxbmrp3-tests\\analytics_identity\\mxbmrp3\\mxbmrp3_analytics.json";
const std::string kId = "3f2504e0-4f89-11d3-9a0c-0305e82c3301";

void writeFile(const std::string& text) {
    std::ofstream f(FILE_PATH, std::ios::binary | std::ios::trunc);
    REQUIRE(f.is_open());
    f << text;
}

std::string readFile() {
    std::ifstream f(FILE_PATH, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

// "<installId>|<launchCount>|<repair>|<prevVersion>"
std::vector<std::string> fields(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string part;
    while (std::getline(ss, part, '|')) out.push_back(part);
    while (out.size() < 4) out.push_back("");
    return out;
}
}  // namespace

TEST_CASE("a truncated analytics file: the id is recovered, the file rewritten, the next load is clean") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(SAVE);   // creates <save>\mxbmrp3\ and a fresh analytics file of its own
    // Damage it: cut INSIDE firstSeen (1751068800), so the id and version survive
    // and the counters do not -- "17" is a prefix of an epoch, not an epoch.
    writeFile("{\n  \"installId\": \"" + kId + "\",\n  \"lastVersion\": \"1.2.3.4\",\n  \"firstSeen\": 17");

    auto f = fields(host.analyticsLoadIdentity(SAVE));
    CHECK(f[0] == kId);
    CHECK(f[1] == "1");          // this launch is the first the file can vouch for
    CHECK(f[2] == "1");          // recovered
    CHECK(f[3] == "1.2.3.4");    // the previous version survived too
    // The file is whole again, with the recovered id, and the cut-short epoch was
    // NOT adopted: the repair used to persist "firstSeen": 17 for the life of the
    // install, reporting an install age of some twenty thousand days ever after.
    const std::string text = readFile();
    CHECK(text.find("\"installId\": \"" + kId + "\"") != std::string::npos);
    CHECK(text.find("\"launchCount\": 1") != std::string::npos);
    const size_t fs = text.find("\"firstSeen\": ");
    REQUIRE(fs != std::string::npos);
    const unsigned long long firstSeen = std::stoull(text.substr(fs + 13));
    CHECK_MESSAGE(firstSeen > 1700000000ULL, "firstSeen persisted as the cut-short " << firstSeen);

    // The next launch parses it: same id, count 2, nothing to repair.
    f = fields(host.analyticsLoadIdentity(SAVE));
    CHECK(f[0] == kId);
    CHECK(f[1] == "2");
    CHECK(f[2] == "0");
    host.shutdown();
}

TEST_CASE("a file with no recoverable id gets a new one, once") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(SAVE);
    writeFile("this is not json and holds no id");

    auto f = fields(host.analyticsLoadIdentity(SAVE));
    REQUIRE(f[0].size() == 36);   // a minted UUID
    CHECK(f[1] == "1");
    CHECK(f[2] == "2");           // new id
    const std::string minted = f[0];
    CHECK(readFile().find("\"installId\": \"" + minted + "\"") != std::string::npos);

    // ...and it sticks: the next load reads it back, no repair.
    f = fields(host.analyticsLoadIdentity(SAVE));
    CHECK(f[0] == minted);
    CHECK(f[1] == "2");
    CHECK(f[2] == "0");
    host.shutdown();
}

TEST_CASE("a whole file and a missing file are read as before, with no repair") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(SAVE);
    writeFile("{\"installId\": \"" + kId + "\", \"lastVersion\": \"9.9.9.9\", \"firstSeen\": 1751068800, "
              "\"launchCount\": 41, \"sessionStart\": 1758900000}");
    auto f = fields(host.analyticsLoadIdentity(SAVE));
    CHECK(f[0] == kId);
    CHECK(f[1] == "42");
    CHECK(f[2] == "0");
    CHECK(f[3] == "9.9.9.9");

    std::remove(FILE_PATH);
    f = fields(host.analyticsLoadIdentity(SAVE));
    CHECK(f[0].size() == 36);
    CHECK(f[0] != kId);
    CHECK(f[1] == "1");
    CHECK(f[2] == "0");           // a first run is not a repair
    host.shutdown();
}
