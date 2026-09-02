// ============================================================================
// core/gl_state_fingerprint.cpp
// diff() and parseVersion() for gl_state_fingerprint.h. Pure: no GL, no Win32.
// ============================================================================
#include "gl_state_fingerprint.h"

#include <cstdio>
#include <cstdlib>

namespace glprobe {
namespace {

// The widest possible sample: every token in the table, all values. If this
// exceeds the fixed budget, capture() would silently stop mid-table and the
// probe would check less state than it reports — so it is a compile error
// instead. Adding a 4-value token to the table is what this guards.
constexpr int totalTableValues() {
    int n = 0;
    for (int i = 0; i < kStateTokenCount; ++i) n += kStateTokens[i].count;
    return n;
}
static_assert(totalTableValues() <= kMaxStateValues,
              "kStateTokens outgrew kMaxStateValues — raise the budget");

const char* tokenName(int owner) {
    if (owner < 0 || owner >= kStateTokenCount) return "?";
    return kStateTokens[owner].name;
}

// Index of `slot` within its own token's values, so a 4-value token reports
// VIEWPORT[2] rather than an opaque flat offset.
int subIndex(const Fingerprint& fp, int slot) {
    int owner = fp.owner[slot], k = 0;
    while (slot - k - 1 >= 0 && fp.owner[slot - k - 1] == owner) ++k;
    return k;
}

}  // namespace

int diff(const Fingerprint& before, const Fingerprint& after,
         std::vector<std::string>& out) {
    if (before.used != after.used) {
        char buf[128];
        std::snprintf(buf, sizeof(buf),
                      "sample size changed: %d -> %d values (token set differed)",
                      before.used, after.used);
        out.emplace_back(buf);
        return 1 + (before.used > after.used ? before.used - after.used
                                             : after.used - before.used);
    }
    int differing = 0;
    for (int i = 0; i < before.used; ++i) {
        if (before.values[i] == after.values[i]) continue;
        ++differing;
        char buf[160];
        const int sub = subIndex(before, i);
        if (kStateTokens[before.owner[i] < 0 ? 0 : before.owner[i]].count > 1) {
            std::snprintf(buf, sizeof(buf), "%s[%d]: %d -> %d",
                          tokenName(before.owner[i]), sub,
                          before.values[i], after.values[i]);
        } else {
            std::snprintf(buf, sizeof(buf), "%s: %d -> %d",
                          tokenName(before.owner[i]),
                          before.values[i], after.values[i]);
        }
        out.emplace_back(buf);
    }
    return differing;
}

int parseVersion(const char* s) {
    if (s == nullptr) return 0;
    // GL_VERSION is "<major>.<minor>[.<release>] [vendor stuff]". Leading
    // whitespace is not legal but costs nothing to tolerate.
    while (*s == ' ' || *s == '\t') ++s;
    if (*s < '0' || *s > '9') return 0;
    int major = 0;
    while (*s >= '0' && *s <= '9') { major = major * 10 + (*s - '0'); ++s; }
    if (*s != '.') return 0;
    ++s;
    if (*s < '0' || *s > '9') return 0;
    // Only the FIRST minor digit: "4.60" would otherwise read as minor 60 and
    // compare as version 4*10+60. GL never ships a two-digit minor.
    const int minor = *s - '0';
    return major * 10 + minor;
}

}  // namespace glprobe
