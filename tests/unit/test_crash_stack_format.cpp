// ============================================================================
// tests/unit/test_crash_stack_format.cpp
// Unit tests for core/crash_stack_format.h — the pure formatting for the crash
// handler's faulting-stack backtrace. The WALK (RtlVirtualUnwind) is Windows/MSVC-
// only and can't run in Linux CI; this pins the string plumbing it feeds: frame
// formatting, the space-delimited list, empty/skip handling, and — critically —
// the MAX_STACK_CHARS budget that keeps the value WHOLE-FRAME-only so the analytics
// sink can't truncate it mid-frame. Header-only, no Win32. See run_tests.sh.
// ============================================================================
// The doctest implementation + main() live in test_plugin_utils.cpp.
#include "doctest.h"

#include "core/crash_stack_format.h"

#include <cstring>
#include <string>

using CrashStack::Frame;
using CrashStack::formatFrame;
using CrashStack::formatFrameList;
using CrashStack::avTypeName;

namespace {
Frame mk(const char* mod, unsigned long long off) {
    Frame f{};
    if (mod) std::strncpy(f.module, mod, sizeof(f.module) - 1);
    f.offset = off;
    return f;
}
}  // namespace

TEST_CASE("formatFrame: module+0xoffset, lowercase hex, no padding") {
    char buf[128];
    CHECK(formatFrame(buf, sizeof(buf), "mxbmrp3.dlo", 0x378d8ULL) > 0);
    CHECK(std::string(buf) == "mxbmrp3.dlo+0x378d8");

    CHECK(formatFrame(buf, sizeof(buf), "ntdll.dll", 0ULL) > 0);
    CHECK(std::string(buf) == "ntdll.dll+0x0");
}

TEST_CASE("formatFrame: empty/null module renders as unknown") {
    char buf[128];
    formatFrame(buf, sizeof(buf), "", 0x10ULL);
    CHECK(std::string(buf) == "unknown+0x10");
    formatFrame(buf, sizeof(buf), nullptr, 0x10ULL);
    CHECK(std::string(buf) == "unknown+0x10");
}

TEST_CASE("formatFrame: unresolved fault renders its real address (unknown+0x<addr>)") {
    // When the crash handler can't attribute an address to a module it passes the raw
    // address as the offset with module "unknown" — this is how "unknown+0x0" becomes
    // "unknown+0x7ff8dbcd4a8b" and how a wild jump is told apart from a literal null call.
    char buf[128];
    CHECK(formatFrame(buf, sizeof(buf), "unknown", 0x7ff8dbcd4a8bULL) > 0);
    CHECK(std::string(buf) == "unknown+0x7ff8dbcd4a8b");
    formatFrame(buf, sizeof(buf), "unknown", 0ULL);          // a genuine null call
    CHECK(std::string(buf) == "unknown+0x0");
}

TEST_CASE("avTypeName: ExceptionInformation[0] -> read/write/execute, else empty") {
    CHECK(std::string(avTypeName(0)) == "read");
    CHECK(std::string(avTypeName(1)) == "write");
    CHECK(std::string(avTypeName(8)) == "execute");   // the injector-at-launch tell
    CHECK(std::string(avTypeName(2)).empty());        // unknown/reserved -> omit
    CHECK(std::string(avTypeName(0xdeadULL)).empty());
}

TEST_CASE("formatFrame: too-small outSize leaves empty string and returns 0") {
    char buf[128];
    // Pass a size smaller than the rendered text (8 < len("mxbmrp3.dlo+0x378d8")).
    // (Small explicit size into a big buffer, not a tiny array, so the fortify
    //  _FORTIFY_SOURCE snprintf-truncation warning doesn't fire on the test itself.)
    CHECK(formatFrame(buf, 8, "mxbmrp3.dlo", 0x378d8ULL) == 0);
    CHECK(buf[0] == '\0');
    CHECK(formatFrame(buf, 0, "x", 1ULL) == 0);  // zero-size buffer is a no-op
}

TEST_CASE("formatFrameList: space-delimited, no quotes/brackets") {
    Frame frames[3] = {
        mk("mxbmrp3.dlo", 0x378d8ULL),
        mk("mxbmrp3.dlo", 0xeaab4ULL),
        mk("mxbikes.exe", 0x1f1923ULL),
    };
    char buf[256];
    int n = formatFrameList(buf, sizeof(buf), frames, 3);
    CHECK(n > 0);
    CHECK(std::string(buf) ==
          "mxbmrp3.dlo+0x378d8 mxbmrp3.dlo+0xeaab4 mxbikes.exe+0x1f1923");
    CHECK(static_cast<int>(std::strlen(buf)) == n);
    // No JSON metacharacters — embeds safely inside a marker string, and the sink
    // won't parse/re-serialize it.
    CHECK(std::string(buf).find('"') == std::string::npos);
    CHECK(std::string(buf).find('[') == std::string::npos);
}

TEST_CASE("formatFrameList: single frame, no leading/trailing space") {
    Frame frames[1] = { mk("ntdll.dll", 0x1234ULL) };
    char buf[128];
    CHECK(formatFrameList(buf, sizeof(buf), frames, 1) > 0);
    CHECK(std::string(buf) == "ntdll.dll+0x1234");
}

TEST_CASE("formatFrameList: empty / non-positive count -> empty string, 0") {
    Frame frames[1] = { mk("x.dll", 1ULL) };
    char buf[128] = "sentinel";
    CHECK(formatFrameList(buf, sizeof(buf), frames, 0) == 0);
    CHECK(buf[0] == '\0');
    CHECK(formatFrameList(buf, sizeof(buf), nullptr, 3) == 0);
    CHECK(buf[0] == '\0');
    CHECK(formatFrameList(buf, sizeof(buf), frames, -1) == 0);
    CHECK(buf[0] == '\0');
}

TEST_CASE("formatFrameList: stops at the char budget on WHOLE frames only") {
    // 10 identical 19-char frames ("mxbmrp3.dlo+0x12345"); with a 40-char budget
    // only two fit ("...345 ...345" = 39 chars), and the result must never end
    // mid-frame (that's exactly what the sink's cap would do to us otherwise).
    Frame frames[10];
    for (auto& f : frames) f = mk("mxbmrp3.dlo", 0x12345ULL);
    char buf[256];
    int n = formatFrameList(buf, sizeof(buf), frames, 10, /*maxChars=*/40);
    CHECK(n > 0);
    CHECK(n <= 40);
    std::string s(buf);
    CHECK(s == "mxbmrp3.dlo+0x12345 mxbmrp3.dlo+0x12345");   // exactly two whole frames
    CHECK(s.back() != ' ');            // no dangling separator
    CHECK(s.find("+0x") != std::string::npos);
}

TEST_CASE("formatFrameList: default budget never exceeds MAX_STACK_CHARS") {
    // 16 max-width frames would overflow 180; the default budget must clamp it.
    Frame frames[CrashStack::MAX_FRAMES];
    for (auto& f : frames) f = mk("mxbmrp3.dlo", 0xffffffffULL);  // 21-char frames
    char buf[512];
    int n = formatFrameList(buf, sizeof(buf), frames, CrashStack::MAX_FRAMES);
    CHECK(n > 0);
    CHECK(n <= CrashStack::MAX_STACK_CHARS);
    CHECK(std::string(buf).back() != ' ');   // whole frames only
}

TEST_CASE("formatFrameList: a max-width frame (63-char module + 64-bit offset) still renders") {
    // The frame scratch buffer is MODULE_NAME_SIZE + 24, and a Frame caps its module
    // at MODULE_NAME_SIZE-1 (63) chars with a <=16-hex offset, so formatFrame can never
    // overflow for a valid Frame — both frames must render (the skip path is defensive).
    Frame frames[2] = {};
    std::memset(frames[0].module, 'Z', sizeof(frames[0].module) - 1);   // 63 chars
    frames[0].offset = 0xffffffffffffffffULL;                            // widest offset
    frames[1] = mk("ok.dll", 0x5ULL);
    char buf[256];
    int n = formatFrameList(buf, sizeof(buf), frames, 2);
    CHECK(n > 0);
    std::string s(buf);
    CHECK(s.find("+0xffffffffffffffff") != std::string::npos);
    CHECK(s.find("ok.dll+0x5") != std::string::npos);
    CHECK(s.find("  ") == std::string::npos);   // single-space separators only
}
