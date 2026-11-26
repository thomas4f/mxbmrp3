// ============================================================================
// tests/unit/test_rider_stance.cpp
// Adapter::readRiderStance (game/adapters/mxbikes_adapter.h): the rider
// sit/stand int (m_iRiderStanding) MX Bikes Beta 21f appends to the
// RunTelemetry payload, without a published header.
//
// The plugin must keep working unchanged on older game builds, which send
// the plain 188-byte struct. So the contract pinned here is mostly about what
// is NOT the field: a short payload, a null pointer and any value other than
// 0/1 all read as unknown (-1). Reading them as 0 would show every older-build
// player as seated. rider_stance_test.cpp (integration) proves the plugin wires
// this up off the real export.
// ============================================================================
#include "doctest.h"
#include "game/adapters/mxbikes_adapter.h"

#include <cstring>

using Adapters::MXBikes::Adapter;

namespace {

// The payload as Beta 21f lays it out: the struct, then the int.
struct Payload {
    SPluginsBikeData_t bike;
    int stance;
};
static_assert(sizeof(Payload) == sizeof(SPluginsBikeData_t) + sizeof(int),
              "the stance int sits directly after the struct");

int read(int raw, int dataSize) {
    Payload p{};
    p.stance = raw;
    return Adapter::readRiderStance(&p, dataSize);
}

}  // namespace

TEST_CASE("readRiderStance: the offset is the end of the published struct") {
    // Measured in the Beta 21f exe: 0xbc, right after m_fSteerTorque.
    CHECK(Adapter::RIDER_STANCE_OFFSET == 0xbc);
    CHECK(Adapter::RIDER_STANCE_OFFSET == static_cast<int>(sizeof(SPluginsBikeData_t)));
}

TEST_CASE("readRiderStance: a build that sends it reads seated and standing") {
    const int full = static_cast<int>(sizeof(Payload));
    CHECK(read(1, full) == 1);   // standing
    CHECK(read(0, full) == 0);   // seated
}

TEST_CASE("readRiderStance: an older build's payload is unknown, never seated") {
    // 188 bytes: the int isn't there. The bytes past the struct belong to the
    // caller's buffer, not the game, so they must not be read at all - planting
    // a 1 there proves the size check, not luck, decides.
    CHECK(read(1, static_cast<int>(sizeof(SPluginsBikeData_t))) == -1);
    CHECK(read(0, static_cast<int>(sizeof(SPluginsBikeData_t))) == -1);
    // One to three bytes of a partial int: still not a whole field.
    CHECK(read(1, static_cast<int>(sizeof(SPluginsBikeData_t)) + 3) == -1);
    CHECK(read(1, 0) == -1);
    CHECK(read(1, -1) == -1);
    CHECK(Adapter::readRiderStance(nullptr, static_cast<int>(sizeof(Payload))) == -1);
}

TEST_CASE("readRiderStance: a value it doesn't know is unknown") {
    // If PiBoSo turns this into an enum or a blend, anything outside the two
    // measured values must not be mistaken for one of them.
    const int full = static_cast<int>(sizeof(Payload));
    CHECK(read(2, full) == -1);
    CHECK(read(-1, full) == -1);
    CHECK(read(0x3f800000, full) == -1);   // 1.0f's bits, if it ever became a float
}

TEST_CASE("readRiderStance: a longer future payload still reads the int") {
    // A later build may append more after it; the stance stays at its offset.
    struct Longer { Payload p; int more[4]; } l{};
    l.p.stance = 1;
    l.more[0] = 7;
    CHECK(Adapter::readRiderStance(&l, static_cast<int>(sizeof(l))) == 1);
}
