// ============================================================================
// tests/unit/test_camera_resolve.cpp
// Spectate camera-name matching and role->index resolution
// (handlers/camera_resolve.h).
//
// This is the director's camera choice. It used to be two file-static helpers
// plus an inline switch in spectate_handler.cpp, reachable only by driving the
// real SpectateCameras callback through the DLL under Wine — and it was never
// asserted at all (API_COVERAGE.md carried it as a fuzz-only "gap": the callback
// fuzzer proved it didn't crash, nothing proved it picked the right camera).
//
// The cases that matter here are the ones a whole-race integration test is a
// clumsy way to reach: the priority ordering inside a candidate list, the
// name-based Auto fallback when a track omits a camera, and Free-Roam's
// deliberate refusal to fall back.
//
// spectate_cameras_test.cpp (integration) owns the end-to-end proof that the
// real callback is wired to this.
// ============================================================================
#include "doctest.h"
#include "handlers/camera_resolve.h"

#include <cstring>
#include <string>
#include <vector>

using Cameras::Role;

namespace {

// The blob the game hands SpectateCameras: packed null-terminated names.
//
// Sized to kMaxBytes on purpose. The walk's only stop conditions are "read
// numCameras names" and "hit the kMaxBytes cap" — a run of zeros is treated as
// padding and skipped, never as a terminator — so a buffer shorter than the cap
// lets an over-stated numCameras walk off the end. In the plugin that over-read
// is what SEH_TRY catches; here it would be a genuine heap-buffer-overflow and
// the ASan flavour of this suite would (correctly) fail it. A real blob is a
// game allocation far larger than its names, so this is also the honest model.
std::vector<unsigned char> blob(const std::vector<std::string>& names) {
    std::vector<unsigned char> b;
    for (const auto& n : names) {
        for (char c : n) b.push_back(static_cast<unsigned char>(c));
        b.push_back(0);
    }
    b.resize(static_cast<size_t>(Cameras::kMaxBytes), 0);
    return b;
}

// Resolve against a blob whose camera count is its real name count.
int resolve(const std::vector<std::string>& names, Role role) {
    auto b = blob(names);
    return Cameras::resolveIndexForRole(b.data(), static_cast<int>(names.size()), role);
}

std::string nameAt(const std::vector<std::string>& names, int idx, int cap = Cameras::kMaxName) {
    auto b = blob(names);
    std::vector<char> out(static_cast<size_t>(cap), '\0');
    if (!Cameras::nameAtIndex(b.data(), static_cast<int>(names.size()), idx, out.data(), cap)) {
        return "<none>";
    }
    return std::string(out.data());
}

// A representative real list: Auto first, a mix of track and onboard cameras.
const std::vector<std::string> kTypical = {
    "Auto", "Trackside", "Start", "Helmet 1", "Helmet 2",
    "Front Fender", "Rear Fender", "Forks", "Orbit", "Free-Roam",
};

}  // namespace

TEST_CASE("iequals folds ASCII case and requires a full match") {
    CHECK(Cameras::iequals("Auto", "auto"));
    CHECK(Cameras::iequals("FREE-ROAM", "free-roam"));
    CHECK(Cameras::iequals("", ""));
    CHECK_FALSE(Cameras::iequals("Auto", "Autos"));     // prefix is not a match
    CHECK_FALSE(Cameras::iequals("Auto", "Aut"));
    CHECK_FALSE(Cameras::iequals("Helmet 1", "Helmet 2"));
    CHECK_FALSE(Cameras::iequals(nullptr, "Auto"));
    CHECK_FALSE(Cameras::iequals("Auto", nullptr));
}

TEST_CASE("nameAtIndex reads packed names and refuses out-of-range indices") {
    CHECK(nameAt(kTypical, 0) == "Auto");
    CHECK(nameAt(kTypical, 1) == "Trackside");
    CHECK(nameAt(kTypical, 9) == "Free-Roam");

    CHECK(nameAt(kTypical, -1) == "<none>");
    CHECK(nameAt(kTypical, 10) == "<none>");            // == numCameras

    // A short output buffer truncates rather than overflowing.
    CHECK(nameAt(kTypical, 1, 5) == "Trac");

    // Null blob, and a zero-length buffer, are both refused.
    char one[1];
    CHECK_FALSE(Cameras::nameAtIndex(nullptr, 4, 0, one, 1));
    CHECK_FALSE(Cameras::nameAtIndex(blob(kTypical).data(), 4, 0, one, 0));
}

TEST_CASE("nameAtIndex sanitizes non-printable bytes to '?'") {
    // A garbage blob must not put control characters into a log line.
    std::vector<unsigned char> b = { 'O', 0x01, 'K', 0x00 };
    b.resize(static_cast<size_t>(Cameras::kMaxBytes), 0);
    char out[Cameras::kMaxName];
    REQUIRE(Cameras::nameAtIndex(b.data(), 1, 0, out, sizeof(out)));
    CHECK(std::string(out) == "O?K");
}

TEST_CASE("findIndexByName matches case-insensitively, or returns -1") {
    auto b = blob(kTypical);
    const int n = static_cast<int>(kTypical.size());

    const char* const wantTrackside[] = { "trackside" };
    CHECK(Cameras::findIndexByName(b.data(), n, wantTrackside, 1) == 1);

    const char* const wantAbsent[] = { "Blimp" };
    CHECK(Cameras::findIndexByName(b.data(), n, wantAbsent, 1) == -1);

    // Guard clauses.
    CHECK(Cameras::findIndexByName(nullptr, n, wantTrackside, 1) == -1);
    CHECK(Cameras::findIndexByName(b.data(), 0, wantTrackside, 1) == -1);
    CHECK(Cameras::findIndexByName(b.data(), n, nullptr, 1) == -1);
}

TEST_CASE("findIndexByName honours candidate PRIORITY, not blob order") {
    // The documented trap: "Orbit" sits at a LOWER index than "Free-Roam", but
    // Free-Roam is the higher-priority candidate and must win. A first-match
    // implementation returns Orbit's index here.
    auto b = blob(kTypical);
    const int n = static_cast<int>(kTypical.size());

    int count = 0;
    const char* const* cand = Cameras::candidatesForRole(Role::FREE_ROAM, count);
    REQUIRE(count == 5);
    CHECK(Cameras::findIndexByName(b.data(), n, cand, count) == 9);   // Free-Roam, not Orbit (8)

    // The case above is NOT sufficient on its own: it also passes a first-match
    // implementation, because the better candidate happens to sit later in the
    // blob and wins by overwriting. What actually pins the `c < bestRank` guard is
    // a better match found EARLIER than a worse one — here "Free Roam" (rank 1) at
    // index 1 must survive "Orbit" (rank 4) at index 2.
    const std::vector<std::string> betterFirst = { "Auto", "Free Roam", "Orbit" };
    CHECK(resolve(betterFirst, Role::FREE_ROAM) == 1);

    // With Free-Roam absent, the next-best candidate present wins.
    const std::vector<std::string> orbitOnly = { "Auto", "Orbit", "Trackside" };
    CHECK(resolve(orbitOnly, Role::FREE_ROAM) == 1);
}

TEST_CASE("candidatesForRole maps every role to its preferred name") {
    struct { Role role; const char* first; } cases[] = {
        { Role::AUTO,            "Auto" },
        { Role::TRACKSIDE,       "Trackside" },
        { Role::START,           "Start" },
        { Role::ONBOARD_FRONT,   "Front Fender" },
        { Role::ONBOARD_HELMET,  "Helmet 1" },
        { Role::ONBOARD_HELMET2, "Helmet 2" },
        { Role::REAR,            "Rear Fender" },
        { Role::FORKS,           "Forks" },
        { Role::FREE_ROAM,       "Free-Roam" },
    };
    for (const auto& c : cases) {
        int n = 0;
        const char* const* cand = Cameras::candidatesForRole(c.role, n);
        REQUIRE(n >= 1);
        CHECK(std::string(cand[0]) == c.first);
    }

    // The two helmet roles fall back to each other, so a track exposing only one
    // still resolves rather than dead-ending.
    int n = 0;
    CHECK(std::string(Cameras::candidatesForRole(Role::ONBOARD_HELMET, n)[1]) == "Helmet 2");
    CHECK(std::string(Cameras::candidatesForRole(Role::ONBOARD_HELMET2, n)[1]) == "Helmet 1");
}

TEST_CASE("isManualName covers every hand-flown camera spelling") {
    for (const char* n : { "Orbit", "Free", "Free-Roam", "Free Roam", "Freeroam", "FREEROAM" }) {
        CHECK(Cameras::isManualName(n));
    }
    for (const char* n : { "Auto", "Trackside", "Helmet 1", "" }) {
        CHECK_FALSE(Cameras::isManualName(n));
    }
}

TEST_CASE("resolveIndexForRole picks the named camera when present") {
    CHECK(resolve(kTypical, Role::AUTO)            == 0);
    CHECK(resolve(kTypical, Role::TRACKSIDE)       == 1);
    CHECK(resolve(kTypical, Role::START)           == 2);
    CHECK(resolve(kTypical, Role::ONBOARD_HELMET)  == 3);
    CHECK(resolve(kTypical, Role::ONBOARD_HELMET2) == 4);
    CHECK(resolve(kTypical, Role::ONBOARD_FRONT)   == 5);
    CHECK(resolve(kTypical, Role::REAR)            == 6);
    CHECK(resolve(kTypical, Role::FORKS)           == 7);
    CHECK(resolve(kTypical, Role::FREE_ROAM)       == 9);
}

TEST_CASE("resolveIndexForRole falls back to Auto BY NAME, not by index 0") {
    // A track that omits Trackside. Auto is present but NOT first — a fallback
    // that assumed index 0 would cut to Pits here.
    const std::vector<std::string> autoNotFirst = { "Pits", "Auto", "Helmet 1" };
    CHECK(resolve(autoNotFirst, Role::TRACKSIDE) == 1);
    CHECK(resolve(autoNotFirst, Role::FORKS)     == 1);

    // Auto missing entirely: index 0 is the last resort.
    const std::vector<std::string> noAuto = { "Pits", "Helmet 1" };
    CHECK(resolve(noAuto, Role::TRACKSIDE) == 0);

    // Asking for Auto on a list without it also lands on index 0.
    CHECK(resolve(noAuto, Role::AUTO) == 0);
}

TEST_CASE("resolveIndexForRole leaves the camera alone when Free-Roam is absent") {
    // Free-Roam is the director's gamepad takeover: falling back to Auto would
    // hand the caster a camera they cannot fly, which is worse than not cutting.
    const std::vector<std::string> noManual = { "Auto", "Trackside", "Helmet 1" };
    CHECK(resolve(noManual, Role::FREE_ROAM) == -1);

    // ... whereas every other role does fall back on the same list.
    CHECK(resolve(noManual, Role::FORKS) == 0);
}

TEST_CASE("resolveIndexForRole guards degenerate inputs") {
    auto b = blob(kTypical);
    CHECK(Cameras::resolveIndexForRole(nullptr, 4, Role::AUTO) == -1);
    CHECK(Cameras::resolveIndexForRole(b.data(), 0, Role::AUTO) == -1);
    CHECK(Cameras::resolveIndexForRole(b.data(), -3, Role::AUTO) == -1);

    // An out-of-range role int (a corrupted request, or one cast from an int at
    // the DLL boundary) degrades to Auto rather than reading past the candidate
    // table. The cast is well-defined, not UB-by-luck: `Role` is a SCOPED enum,
    // whose underlying type is `int` when unspecified ([dcl.enum]/5), and an
    // enumeration with a fixed underlying type takes that type's whole value
    // range ([dcl.enum]/8). The unscoped-enum rule that would make this UB
    // doesn't apply. (Verified: `std::underlying_type_t<Role>` is `int`.)
    CHECK(Cameras::resolveIndexForRole(b.data(), 10, static_cast<Role>(99)) == 0);
}

TEST_CASE("an over-long camera name truncates without desyncing the indices") {
    // REGRESSION. The pre-extraction findCameraIndexByName() stopped advancing
    // its cursor once it had copied kMaxName-1 bytes, so it never reached the
    // name's terminator: the tail of an over-long name was then read as ANOTHER
    // camera and every index after it shifted. cameraNameAtIndex() walked the
    // same blob correctly, so the two disagreed about what index a name had.
    //
    // The failure is worse than "not found" — it silently returns the WRONG
    // index, i.e. the director cuts to a different camera than it asked for.
    // Measured against the old implementation on this exact blob: Trackside
    // (index 1) came back as 2, and Forks (index 2) came back as -1.
    //
    // Camera names are short in practice; the blob is opaque and unbounded, which
    // is why the fuzzer feeds it garbage and why this is worth pinning.
    const std::vector<std::string> longFirst = { std::string(70, 'X'), "Trackside", "Forks" };

    CHECK(resolve(longFirst, Role::TRACKSIDE) == 1);
    CHECK(resolve(longFirst, Role::FORKS)     == 2);

    // The over-long name is itself readable, just truncated to the buffer.
    const std::string got = nameAt(longFirst, 0);
    CHECK(got.size() == static_cast<size_t>(Cameras::kMaxName - 1));
    CHECK(got == std::string(Cameras::kMaxName - 1, 'X'));

    // ... and the names after it are still at their real indices.
    CHECK(nameAt(longFirst, 1) == "Trackside");
    CHECK(nameAt(longFirst, 2) == "Forks");
}

TEST_CASE("the blob walk stays bounded when numCameras overstates the names") {
    // The callback passes no element size, so numCameras is the only bound we
    // are given and it can disagree with the buffer. The walk must stop at the
    // buffer's zero padding instead of running to kMaxBytes of adjacent memory.
    const std::vector<std::string> few = { "Auto", "Trackside" };
    auto b = blob(few);
    const char* const want[] = { "Forks" };
    CHECK(Cameras::findIndexByName(b.data(), 64, want, 1) == -1);
    CHECK(Cameras::resolveIndexForRole(b.data(), 64, Role::TRACKSIDE) == 1);

    char out[Cameras::kMaxName];
    CHECK_FALSE(Cameras::nameAtIndex(b.data(), 64, 40, out, sizeof(out)));
}
