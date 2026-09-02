// ============================================================================
// tests/unit/test_gl_state_fingerprint.cpp
// core/gl_state_fingerprint.h — the comparison half of the Phase 0 GL probe.
//
// WHY THIS TEST EARNS ITS PLACE. The probe's whole safety claim is "we drew in
// the game's context and left it exactly as we found it", and the evidence for
// that claim is diff() returning 0. A diff() that fails to notice a changed
// value would print "state restored clean" for a renderer that leaks a bound
// program into the game's next draw — a false green in the one place the
// project is trusting most. So every case here plants a difference and demands
// it be seen; the clean case alone would pass against a function that returns 0
// unconditionally.
//
// The GL side (a real context, a real draw) cannot be reached headlessly at
// all; that is the manual half of Phase 0, and gl_probe.h says so.
// ============================================================================
#include "doctest.h"
#include "core/gl_state_fingerprint.h"

#include <string>
#include <vector>

using namespace glprobe;

namespace {

// A reader that answers every token with a fixed value, and can be told to
// answer ONE token differently — which is how a leak is planted.
struct FakeGl {
    int base = 7;
    unsigned int poisonToken = 0;   // 0 = nothing poisoned
    int poisonValue = 0;
    int poisonSlot = 0;             // which of a multi-value token to change
    int reads = 0;

    void operator()(unsigned int token, int count, int* out) {
        ++reads;
        for (int i = 0; i < count; ++i) out[i] = base + static_cast<int>(token % 13) + i;
        if (token == poisonToken && poisonSlot < count) out[poisonSlot] = poisonValue;
    }
};

// A GL 4.6 compatibility context: every token in the table is supported, which
// is the configuration the probe most likely meets in the field.
constexpr int kVer46 = 46;

}  // namespace

TEST_CASE("fingerprint: a compat 4.6 context samples the whole table") {
    FakeGl gl;
    Fingerprint fp = capture(kVer46, /*compat=*/true, gl);
    CHECK(gl.reads == kStateTokenCount);
    CHECK(fp.used > kStateTokenCount);        // multi-value tokens contribute extra
    CHECK(fp.used <= kMaxStateValues);
    // Every recorded value knows which token it came from — that is what lets a
    // difference be named in the log rather than reported as an offset.
    for (int i = 0; i < fp.used; ++i) CHECK(fp.owner[i] >= 0);
}

TEST_CASE("fingerprint: identical samples differ in nothing") {
    FakeGl gl;
    Fingerprint a = capture(kVer46, true, gl);
    Fingerprint b = capture(kVer46, true, gl);
    std::vector<std::string> lines;
    CHECK(diff(a, b, lines) == 0);
    CHECK(lines.empty());
}

TEST_CASE("fingerprint: a leaked binding is caught and named") {
    // The exact production failure this guards: the probe unbinds the game's
    // shader program to draw with fixed function and fails to put it back.
    FakeGl before;
    Fingerprint a = capture(kVer46, true, before);

    FakeGl after;
    after.poisonToken = 0x8B8D;   // GL_CURRENT_PROGRAM
    after.poisonValue = 0;        // left unbound
    Fingerprint b = capture(kVer46, true, after);

    std::vector<std::string> lines;
    CHECK(diff(a, b, lines) == 1);
    CHECK(lines.size() == 1);
    CHECK(!lines.empty());
    if (!lines.empty()) CHECK(lines[0].find("CURRENT_PROGRAM") != std::string::npos);
}

TEST_CASE("fingerprint: one component of a multi-value token is caught, with its index") {
    FakeGl before;
    Fingerprint a = capture(kVer46, true, before);

    FakeGl after;
    after.poisonToken = 0x0C10;   // GL_SCISSOR_BOX, four values
    after.poisonSlot = 2;
    after.poisonValue = 999999;
    Fingerprint b = capture(kVer46, true, after);

    std::vector<std::string> lines;
    CHECK(diff(a, b, lines) == 1);
    CHECK(lines.size() == 1);
    if (!lines.empty()) CHECK(lines[0].find("SCISSOR_BOX[2]") != std::string::npos);
}

TEST_CASE("fingerprint: an unbalanced push/pop shows up as a stack depth change") {
    // glPushAttrib without its glPopAttrib leaks nothing visible in the enables
    // — the values still read back correctly — but the attrib stack grows. It is
    // the only symptom, which is why the depth tokens are in the table.
    FakeGl before;
    Fingerprint a = capture(kVer46, true, before);

    FakeGl after;
    after.poisonToken = 0x0BB0;   // GL_ATTRIB_STACK_DEPTH
    after.poisonValue = 1;
    Fingerprint b = capture(kVer46, true, after);

    std::vector<std::string> lines;
    CHECK(diff(a, b, lines) == 1);
    CHECK(lines.size() == 1);
    if (!lines.empty()) CHECK(lines[0].find("ATTRIB_STACK_DEPTH") != std::string::npos);
}

TEST_CASE("fingerprint: a differing sample SIZE is a difference, not a shorter compare") {
    // Comparing the common prefix of two different token sets would report
    // "clean" while having checked less state than it claims. It must not.
    FakeGl gl;
    Fingerprint core = capture(kVer46, /*compat=*/false, gl);
    Fingerprint compat = capture(kVer46, /*compat=*/true, gl);
    REQUIRE(core.used < compat.used);

    std::vector<std::string> lines;
    CHECK(diff(core, compat, lines) > 0);
    CHECK(lines.size() == 1);
    if (!lines.empty()) CHECK(lines[0].find("sample size changed") != std::string::npos);
}

TEST_CASE("token gating: nothing unsupported is ever queried") {
    // Querying a token the context does not support raises GL_INVALID_ENUM in
    // the GAME's context — an error the game did not cause. The gate is the
    // only thing preventing that, so it is checked at the boundary versions.
    FakeGl gl;
    Fingerprint gl11 = capture(10, /*compat=*/true, gl);
    for (int i = 0; i < gl11.used; ++i) {
        const StateToken& t = kStateTokens[gl11.owner[i]];
        CHECK(t.minVer <= 10);
    }

    // A core profile must not be asked for a single fixed-function token.
    Fingerprint core = capture(46, /*compat=*/false, gl);
    for (int i = 0; i < core.used; ++i) CHECK(!kStateTokens[core.owner[i]].compatOnly);

    // GL_SAMPLER_BINDING appeared in 3.3: supported at 3.3, not at 3.2.
    const StateToken sampler{ 0x8919, "SAMPLER_BINDING", 1, 33, false };
    CHECK(tokenSupported(sampler, 33, true));
    CHECK(!tokenSupported(sampler, 32, true));
}

TEST_CASE("parseVersion: real GL_VERSION strings, and the ones that must not fool it") {
    CHECK(parseVersion("4.6.0 NVIDIA 551.23") == 46);
    CHECK(parseVersion("3.3.0 - Build 31.0.101.4502") == 33);
    CHECK(parseVersion("2.1 Mesa 21.2.6") == 21);
    CHECK(parseVersion("1.1.0") == 11);
    // A two-digit minor would read as version 4*10+60 and wave every modern
    // token through; GL has no such version, so only the first digit is taken.
    CHECK(parseVersion("4.60 Vendor") == 46);
    // Unparseable => 0 => every modern token is skipped. The conservative
    // direction: under-report rather than raise an error in the game's context.
    CHECK(parseVersion(nullptr) == 0);
    CHECK(parseVersion("") == 0);
    CHECK(parseVersion("OpenGL ES 3.2") == 0);
    CHECK(parseVersion("4") == 0);
    CHECK(parseVersion("4.") == 0);
}
