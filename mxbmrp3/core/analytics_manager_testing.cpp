// ============================================================================
// core/analytics_manager_testing.cpp
// AnalyticsManager's dry-run capture seam for the headless wiring tests. The
// whole file is under MXBMRP3_TEST_BUILD, so a shipping DLL compiles nothing
// from it. Moved verbatim out of analytics_manager.cpp when that file crossed
// the file budget; the class and its API are unchanged.
// ============================================================================
#include "analytics_manager.h"
#include "analytics_manager_internal.h"
#include "../vendor/nlohmann/json.hpp"

#include <cwchar>
#include <deque>
#include <fstream>
#include <string>
#include <vector>

using namespace AnalyticsInternal;

#if defined(MXBMRP3_TEST_BUILD)
// ============================================================================
// Dry-run capture seam (headless wiring tests). Never compiled into a shipping
// DLL. Drives the payload build + the sampling gate with no network and no
// background threads — see analytics_wiring_test.cpp.
// ============================================================================
void AnalyticsManager::testPrime() {
    // Fake just enough of what initialize() would establish (identity + session +
    // ingest host) that the event-build gates pass and buildEventBody() has an
    // identity — without loadAndUpdateIdentity() (file I/O), the beacon/worker
    // threads, or any network. Capture mode makes the real senders no-ops.
    m_enabled = true;
    m_installId = "test-install-000000000000";
    m_versionStatus = "new";
    m_prevVersion.clear();
    m_launchCount = 1;
    m_firstSeenUnix = epochSecondsNow();
    m_sessionStartUnix = m_firstSeenUnix;
    m_host = L"capture.invalid";        // non-empty → queueSessionEnd()/trackEvent() gates pass
    m_wAppKey = L"A-US-testtesttest";
    m_sessionId = makeSessionId();
    m_shutdownRequested = false;
    m_fullLaunch.store(true);
    m_pendingCrashPath.clear();
    s_testCaptureMode = true;           // real senders become no-ops; isConfigured() → true
    MutexLock lock(m_eventMutex);
    m_eventQueue.clear();
}

void AnalyticsManager::testSetFullLaunch(bool full) { m_fullLaunch.store(full); }

std::string AnalyticsManager::testBuildAppStarted() { return buildEventBody(); }

bool AnalyticsManager::testStartEventWorker() {
    if (m_eventWorker.joinable()) return true;   // never overwrite a live std::thread
    m_shutdownRequested = false;
    m_eventWorker = std::thread(&AnalyticsManager::eventWorkerLoop, this);
    return m_eventWorker.joinable();
}

bool AnalyticsManager::testEventWorkerRunning() const { return m_eventWorker.joinable(); }

void AnalyticsManager::testQueueSessionEnd() { queueSessionEnd(); }

void AnalyticsManager::testQueueCustom(const std::string& name) { trackEvent(name, {}); }

void AnalyticsManager::testSeedAndReportCrash(const std::string& markerPath,
                                              const std::string& fault, const std::string& code) {
    // Write a minimal crash marker (mirrors the crash handler's), point the manager at it,
    // then run the crash path — which is DELIBERATELY not gated on m_fullLaunch, so it
    // queues even in a minimal launch.
    m_pendingCrashPath = markerPath;
    try {
        std::ofstream out(markerPath, std::ios::trunc);
        nlohmann::json j;
        j["fault"] = fault; j["code"] = code;
        j["plugin"] = "9.9.9"; j["game_build"] = "test"; j["host"] = "test.exe";
        j["time"] = static_cast<unsigned long long>(epochSecondsNow());
        // Backtrace as the real crash handler writes it: a plain space-delimited
        // "module+0xoffset ..." string (leaf first). Lets the wiring test prove the
        // stack survives the marker -> event round-trip.
        j["stack"] = fault + " mxbmrp3.dlo+0xeaab4 ntdll.dll+0x1234";
        // ...and the whole walk (2.22.0), longer than the event's cut.
        j["stack_full"] = fault + " mxbmrp3.dlo+0xeaab4 mxbmrp3.dlo+0x1234 ntdll.dll+0x1234 kernel32.dll+0x5678";
        // Access-violation sub-type, as the crash handler writes it for a 0xC0000005.
        j["av_type"] = "read";
        out << j.dump();
    } catch (...) { /* no marker → sendPendingCrashReport() no-ops */ }
    sendPendingCrashReport();
}

std::vector<std::string> AnalyticsManager::testDrainPending() {
    MutexLock lock(m_eventMutex);
    std::vector<std::string> out;
    std::deque<Outgoing> keep;
    for (Outgoing& o : m_eventQueue) {
        if (std::wcscmp(o.path, L"/api/v0/events") == 0) out.push_back(std::move(o.body));
        else keep.push_back(std::move(o));
    }
    m_eventQueue.swap(keep);
    return out;
}

std::vector<std::string> AnalyticsManager::testDrainErrors() {
    MutexLock lock(m_eventMutex);
    std::vector<std::string> out;
    std::deque<Outgoing> keep;
    for (Outgoing& o : m_eventQueue) {
        if (std::wcscmp(o.path, L"/api/v0/error") == 0) out.push_back(std::move(o.body));
        else keep.push_back(std::move(o));
    }
    m_eventQueue.swap(keep);
    return out;
}
#endif  // MXBMRP3_TEST_BUILD
