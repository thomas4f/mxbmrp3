// ============================================================================
// tests/integration/tests/http_sse_test.cpp
// THE SSE STREAM ITSELF: /api/events opens, pushes on a data change, and lets
// go of the DLL at shutdown.
//
// WHY THIS EXISTS. Every other HTTP test drives request/response — http_test
// and http_robust_test fetch /api/state (and malformed variants), and
// http_gating_test's "client" is an /api/state poll. Nothing opened the
// STREAM, so the endpoint the web overlay actually lives on was covered only
// by the fact that it compiled. That gap surfaced when cpp-httplib was bumped
// to 0.54.1, whose diff lands squarely on this path: the chunked-body writer
// (a zero-length write no longer terminates the body), DataSink::is_writable
// (new default, forwarded into provider items), and Server::stop()'s socket
// teardown. A green /api/state says nothing about any of them.
//
// THE THIRD ASSERTION IS THE LOAD-BEARING ONE. The content provider parks
// inside itself for up to 15s per keepalive and unwinds only when stop() sets
// m_shutdownRequested and notifies the condition; HttpServer::stop() then
// JOINS the server thread, which cannot finish while a pool worker is still in
// the provider. Get that wrong and the failure is not a wrong pixel — it is
// the DLL-detach hang CLAUDE.md keeps a maintenance invariant for
// (FreeLibrary's loader lock versus a live worker), and the game hangs on exit
// rather than crashing. So shutdown is timed WITH A CLIENT ATTACHED: prompt
// means the wake path worked, and anything near the keepalive interval means
// the provider sat out its own timeout.
//
// The bytes are chunk-framed by httplib, so the frames are read by searching
// for the SSE fields (see PluginHost::sseOpen). Self-contained doctest.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

#include <cstdlib>
#include <string>

namespace {

// The id: field of the FIRST event in `frame`, or -1. Each push carries the
// snapshot sequence, so comparing two of them is how a test says "this is a new
// event", not "these bytes differ".
long long sseId(const std::string& frame) {
    const size_t at = frame.find("id: ");
    if (at == std::string::npos) return -1;
    return std::strtoll(frame.c_str() + at + 4, nullptr, 10);
}

}  // namespace

TEST_CASE("http: /api/events streams, pushes on a data change, and releases at shutdown") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\http_sse\\");
    REQUIRE(host.startHttp());        // starts the server + waits for it to answer

    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(/*session=*/6, /*numLaps=*/10, /*lengthMs=*/0);
    host.addEntry(10, "Alice");
    host.addEntry(22, "Bob");
    host.classify(6, 300000, {
        { .num = 10, .best = 90000, .laps = 3, .gap = 0 },
        { .num = 22, .best = 91000, .laps = 3, .gap = 1500 },
    });

    const uintptr_t sse = host.sseOpen();
    REQUIRE(PluginHost::rawValid(sse));

    // --- 1. The stream opens and the initial snapshot arrives ----------------
    // "\n\n" ends an SSE event and cannot appear in the CRLF header block, so it
    // is the marker for "one whole event has landed".
    const std::string first = host.sseRead(sse, "\n\n");
    CHECK(first.find("text/event-stream") != std::string::npos);
    CHECK(first.find("id: ") != std::string::npos);
    CHECK(first.find("data: {") != std::string::npos);
    // The payload is the live snapshot, not a placeholder: it carries the race
    // this test just drove.
    CHECK(first.find("\"Alice\"") != std::string::npos);
    const long long firstId = sseId(first);
    CHECK(firstId >= 0);

    // --- 2. A data change is PUSHED, with a newer sequence -------------------
    // The client is attached, so the frequent-change gating lets the rebuild
    // through and the provider wakes on it (rather than on its keepalive).
    host.classify(6, 305000, {
        { .num = 10, .best = 90000, .laps = 4, .gap = 0 },
        { .num = 22, .best = 89000, .laps = 4, .gap = 900 },
    });
    const std::string pushed = host.sseRead(sse, "\n\n");
    CHECK(pushed.find("data: {") != std::string::npos);
    CHECK(sseId(pushed) > firstId);

    // --- 3. Shutdown unwinds the provider instead of waiting it out ----------
    // The bound is far below the 15s keepalive: passing means stop()'s notify
    // reached the parked provider, failing means it did not and the join waited
    // for the timeout. Generous enough not to flap on a slow Wine box, small
    // enough that it can only pass for the right reason.
    const ULONGLONG t0 = GetTickCount64();
    host.shutdown();
    const ULONGLONG elapsed = GetTickCount64() - t0;
    INFO("shutdown with an SSE client attached took " << elapsed << " ms");
    CHECK(elapsed < 8000);

    host.rawClose(sse);
}
