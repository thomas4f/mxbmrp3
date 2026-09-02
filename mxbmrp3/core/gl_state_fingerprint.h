// ============================================================================
// core/gl_state_fingerprint.h
// The pure half of the GL probe: WHICH GL state is sampled, and how two samples
// are compared. No GL headers, no Win32, no I/O — so the comparison can be
// unit-tested headlessly, which is the whole reason it is a separate file.
//
// WHY IT MATTERS THAT THIS IS TESTED: drawing inside the game's own GL context
// makes the plugin a guest in someone else's state machine, and a single leaked
// bit corrupts the game's NEXT draw — a garbled game, blamed on the plugin,
// reported by users, miserable to debug. The defense is "sample the state,
// draw, restore, sample again, assert equality". A diff() that silently fails
// to notice a change would report a clean bill of health for a renderer that
// leaks: a false green in exactly the place we are trusting most. Hence
// tests/unit/test_gl_state_fingerprint.cpp, which feeds it planted differences.
//
// The GL enum values are spelled out here rather than pulled from <GL/gl.h> for
// two reasons: this header must compile with no GL at all (that is what makes
// it testable), and half the tokens below are from GL 3.0+, which the 1.1
// gl.h shipped by both MSVC and mingw does not declare anyway. They are stable
// API constants; the names are kept alongside so a log line reads.
//
// VERSION GATING IS NOT COSMETIC: querying a token the context does not support
// raises GL_INVALID_ENUM *in the game's context*, and a game that checks
// glGetError would see an error it did not cause. So each token states the
// version it appeared in, and whether it is fixed-function (absent from a core
// profile), and the sampler skips what this context cannot answer.
// ============================================================================
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace glprobe {

// One piece of GL state worth checking for leaks.
struct StateToken {
    unsigned int token;   // the GL enum passed to glGetIntegerv
    const char* name;     // for the log line
    int count;            // how many integers glGetIntegerv writes
    int minVer;           // GL version x10 the token appeared in (10 = GL 1.0)
    bool compatOnly;      // fixed-function: absent from a core profile
};

// The sampled set. Deliberately biased toward what glPushAttrib does NOT save
// (bound program, VAO, buffers, samplers, FBOs) — that is where a restore
// actually goes wrong; the fixed-function entries are cheap corroboration that
// the push/pop pair ran at all.
inline constexpr StateToken kStateTokens[] = {
    // --- Modern bindings: glPushAttrib saves NONE of these. ---
    { 0x8B8D, "CURRENT_PROGRAM",              1, 20, false },
    { 0x85B5, "VERTEX_ARRAY_BINDING",         1, 30, false },
    { 0x8894, "ARRAY_BUFFER_BINDING",         1, 15, false },
    { 0x8895, "ELEMENT_ARRAY_BUFFER_BINDING", 1, 15, false },
    { 0x8CA6, "DRAW_FRAMEBUFFER_BINDING",     1, 30, false },
    { 0x8CAA, "READ_FRAMEBUFFER_BINDING",     1, 30, false },
    { 0x8CA7, "RENDERBUFFER_BINDING",         1, 30, false },
    { 0x8919, "SAMPLER_BINDING",              1, 33, false },
    { 0x84E0, "ACTIVE_TEXTURE",               1, 13, false },
    { 0x8069, "TEXTURE_BINDING_2D",           1, 11, false },

    // --- Fixed-function / common state: covered by glPushAttrib, checked anyway
    //     because "covered" is the claim under test. ---
    { 0x0BA2, "VIEWPORT",                     4, 10, false },
    { 0x0C10, "SCISSOR_BOX",                  4, 10, false },
    { 0x0C11, "SCISSOR_TEST",                 1, 10, false },
    { 0x0BE2, "BLEND",                        1, 10, false },
    { 0x80C9, "BLEND_SRC_RGB",                1, 14, false },
    { 0x80C8, "BLEND_DST_RGB",                1, 14, false },
    { 0x80CB, "BLEND_SRC_ALPHA",              1, 14, false },
    { 0x80CA, "BLEND_DST_ALPHA",              1, 14, false },
    { 0x8009, "BLEND_EQUATION_RGB",           1, 14, false },
    { 0x883D, "BLEND_EQUATION_ALPHA",         1, 20, false },
    { 0x0B71, "DEPTH_TEST",                   1, 10, false },
    { 0x0B72, "DEPTH_WRITEMASK",              1, 10, false },
    { 0x0B74, "DEPTH_FUNC",                   1, 10, false },
    { 0x0B44, "CULL_FACE",                    1, 10, false },
    { 0x0B45, "CULL_FACE_MODE",               1, 10, false },
    { 0x0B46, "FRONT_FACE",                   1, 10, false },
    { 0x0B90, "STENCIL_TEST",                 1, 10, false },
    { 0x0B98, "STENCIL_WRITEMASK",            1, 10, false },
    { 0x0C23, "COLOR_WRITEMASK",              4, 10, false },
    { 0x0B40, "POLYGON_MODE",                 2, 10, false },
    { 0x0CF5, "UNPACK_ALIGNMENT",             1, 10, false },
    { 0x0D05, "PACK_ALIGNMENT",               1, 10, false },

    // --- Compatibility-profile only. Their STACK DEPTHS are the direct
    //     evidence that every push found its pop: an unbalanced pair shows up
    //     here and nowhere else. ---
    { 0x0BA0, "MATRIX_MODE",                  1, 10, true },
    { 0x0BA3, "MODELVIEW_STACK_DEPTH",        1, 10, true },
    { 0x0BA4, "PROJECTION_STACK_DEPTH",       1, 10, true },
    { 0x0BA5, "TEXTURE_STACK_DEPTH",          1, 10, true },
    { 0x0BB0, "ATTRIB_STACK_DEPTH",           1, 10, true },
    { 0x0BB1, "CLIENT_ATTRIB_STACK_DEPTH",    1, 10, true },
    { 0x0DE1, "TEXTURE_2D_ENABLED",           1, 10, true },
    { 0x0B50, "LIGHTING",                     1, 10, true },
    { 0x0BC0, "ALPHA_TEST",                   1, 10, true },
};

inline constexpr int kStateTokenCount =
    static_cast<int>(sizeof(kStateTokens) / sizeof(kStateTokens[0]));

// Fixed capacity: a Fingerprint is a POD that can live on the stack of a
// per-frame path with no allocation. Sized to the table with headroom; the
// static_assert is what makes adding a 4-value token safe.
inline constexpr int kMaxStateValues = 128;

struct Fingerprint {
    int values[kMaxStateValues]{};
    // Which table entry each value came from, so diff() can name it. -1 = unused.
    int owner[kMaxStateValues];
    int used = 0;
    Fingerprint() { for (int i = 0; i < kMaxStateValues; ++i) owner[i] = -1; }
};

// True when `t` can be queried in a context of version `ver` (x10, e.g. 21 for
// GL 2.1) with the given profile. An unknown/unparseable version is treated as
// 1.0, which skips every modern token — the conservative direction: we would
// rather under-report than raise GL_INVALID_ENUM in the game's context.
inline bool tokenSupported(const StateToken& t, int ver, bool compatProfile) {
    if (ver < t.minVer) return false;
    if (t.compatOnly && !compatProfile) return false;
    return true;
}

// Walk the table and sample it. `read` is `void(unsigned int token, int count,
// int* out)` — glGetIntegerv in production, a planted fake in the unit test.
// Injected rather than called directly so this whole function is exercised
// headlessly; the GL layer stays a one-line lambda.
template <class Reader>
inline Fingerprint capture(int ver, bool compatProfile, Reader&& read) {
    Fingerprint fp;
    for (int i = 0; i < kStateTokenCount; ++i) {
        const StateToken& t = kStateTokens[i];
        if (!tokenSupported(t, ver, compatProfile)) continue;
        if (fp.used + t.count > kMaxStateValues) break;   // table outgrew the budget
        read(t.token, t.count, &fp.values[fp.used]);
        for (int k = 0; k < t.count; ++k) fp.owner[fp.used + k] = i;
        fp.used += t.count;
    }
    return fp;
}

// Append one human-readable line per differing value to `out` and return how
// many differed. A mismatch in `used` (the two samples covered different
// tokens, which should be impossible within one frame) is reported as its own
// line and counts as a difference — silently comparing the shorter prefix is
// exactly the false green this file exists to prevent.
int diff(const Fingerprint& before, const Fingerprint& after,
         std::vector<std::string>& out);

// "4.6.0 NVIDIA 551.23" -> 46. Returns 0 when the string is null/unparseable,
// which callers treat as "assume nothing" (see tokenSupported).
int parseVersion(const char* glVersionString);

}  // namespace glprobe
