// ============================================================================
// diagnostics/log_scrub.h
// Takes the player's Windows user folder out of every log line.
//
// mxbmrp3_log.txt gets posted with bug reports, so it stays anonymous (the
// chat managers already keep channel names out of it). Paths were the leak
// nobody had to write on purpose: the save path, the log file, crash dumps,
// asset, settings and stats files are all logged in full, and under
// C:\Users\<name>\Documents every one of them names the Windows account. So
// the Logger runs each line through here, once, rather than every call site
// remembering to: "C:\Users\tom\Documents\PiBoSo\..." is logged as
// "<Documents>\PiBoSo\...", anything else under the profile as
// "%USERPROFILE%\...". The player's in-game name is left alone.
//
// Pure (no Windows headers) so the unit suite compiles it as is; the Logger
// resolves the folders and owns the prefix table. Pinned by
// tests/unit/test_log_scrub.cpp and, through the real log file,
// tests/integration/tests/log_scrub_test.cpp.
// ============================================================================
#pragma once

#include <cstddef>
#include <cstring>

namespace LogScrub {

// One folder to take out, and what is written in its place.
struct Prefix {
    static constexpr size_t MAX_PATH_CHARS = 260;
    char path[MAX_PATH_CHARS] = {};
    size_t len = 0;              // 0 = unused slot
    const char* label = "";
};

// Fill `p` from a resolved folder. Trailing separators are dropped, so the
// match below works with or without one. A folder too short to be a real user
// folder ("C:\", empty) is refused: it would scrub far more than the account.
inline bool setPrefix(Prefix& p, const char* folder, const char* label) {
    p.len = 0;
    p.label = label;
    if (!folder) return false;
    size_t n = strlen(folder);
    while (n > 0 && (folder[n - 1] == '\\' || folder[n - 1] == '/')) --n;
    if (n < 4 || n >= Prefix::MAX_PATH_CHARS) return false;
    memcpy(p.path, folder, n);
    p.path[n] = '\0';
    p.len = n;
    return true;
}

// Windows paths compare case-insensitively, and the plugin builds some of its
// paths with '/', so a separator matches either kind.
inline bool pathCharEq(char a, char b) {
    if (a == '/') a = '\\';
    if (b == '/') b = '\\';
    if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
    if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
    return a == b;
}

// A match must end where the folder name ends: C:\Users\tom is not the start
// of C:\Users\tomas (that would leave "%USERPROFILE%as" behind).
inline bool continuesName(char c) {
    const unsigned char u = static_cast<unsigned char>(c);
    return (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9') ||
           u == '_' || u == '-' || u == '.' || u >= 0x80;
}

inline bool matchesAt(const char* s, const Prefix& p) {
    for (size_t i = 0; i < p.len; ++i) {
        if (s[i] == '\0' || !pathCharEq(s[i], p.path[i])) return false;
    }
    return !continuesName(s[p.len]);
}

// Copy `in` to `out` (always terminated, cut to fit) with every occurrence of
// each prefix replaced by its label. Where two prefixes match at one place the
// earlier in `prefixes` wins, so list the more specific folder first. Returns
// true if anything was replaced. No allocation: a log() call costs a scan of
// its own message.
inline bool scrub(const char* in, char* out, size_t outSize,
                  const Prefix* prefixes, size_t count) {
    if (outSize == 0) return false;
    bool replaced = false;
    size_t o = 0;
    const char* s = in ? in : "";
    while (*s && o + 1 < outSize) {
        const Prefix* hit = nullptr;
        for (size_t k = 0; k < count; ++k) {
            if (prefixes[k].len > 0 && matchesAt(s, prefixes[k])) { hit = &prefixes[k]; break; }
        }
        if (hit) {
            for (const char* l = hit->label; *l && o + 1 < outSize; ++l) out[o++] = *l;
            s += hit->len;
            replaced = true;
        } else {
            out[o++] = *s++;
        }
    }
    out[o] = '\0';
    return replaced;
}

}  // namespace LogScrub
