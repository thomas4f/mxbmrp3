// ============================================================================
// core/analytics_manager.h
// Privacy-friendly anonymous usage analytics (Aptabase).
//
// Fires a single fire-and-forget "app_started" beacon per game launch from a
// background thread, then exits. No personal data is collected: the only
// identifier is a locally-generated random install UUID (stored in
// mxbmrp3_analytics.json) used to distinguish unique installs from active ones.
//
// Opt-out, default ON. The toggle lives in Settings > General > Integrations
// and persists as `analytics=` in the [General] INI section.
// ============================================================================
#pragma once

#include <string>
#include <atomic>
#include <thread>
#include <mutex>
#include <deque>
#include <map>
#include <vector>
#include <condition_variable>

class AnalyticsManager {
public:
    // Singleton access
    static AnalyticsManager& getInstance();

    // Lifecycle management (called by PluginManager). initialize() gathers the
    // beacon payload on the game thread (PluginData/HUD state is not
    // thread-safe), then spawns the background sender. Must be called AFTER
    // settings load so the enabled flag and HUD visibility are current.
    void initialize(const char* savePath);
    void shutdown();

    // Settings (set during settings load, persisted in [General] analytics=)
    void setEnabled(bool enabled) { m_enabled = enabled; }
    bool isEnabled() const { return m_enabled; }

    // Track a rare, user-initiated event (e.g. a link click). Aptabase best
    // practice is one event name + a property dimension, so prefer e.g.
    // trackEvent("link_clicked", {{"target","donate"}}) over many event names.
    // Game-thread safe and non-blocking: the event is queued and sent on a
    // background worker under the current session. No-op if analytics is off or
    // not configured, or before app_started has been sent this launch.
    void trackEvent(const std::string& eventName,
                    const std::map<std::string, std::string>& props = {});

#if defined(MXBMRP3_TEST_BUILD)
    // --- Dry-run capture seam (headless wiring tests; never in a shipping DLL). ---
    // The manager's only real effect is a network POST the harness can't observe, so
    // these drive the payload build + the sampling gate WITHOUT any network or threads:
    // testPrime() fakes just enough identity/session/host state that the gates pass and
    // buildEventBody() works, and turns on capture mode (which makes the real senders a
    // no-op, so a test build can never phone home). The event-build paths still push their
    // JSON onto m_eventQueue exactly as in production; testDrainPending() reads it back.
    void testPrime();                          // fake identity/session/host + capture mode on
    void testSetFullLaunch(bool full);         // simulate the remote-sampling decision
    std::string testBuildAppStarted();         // buildEventBody() output (always-sent tier)
    void testQueueSessionEnd();                // run queueSessionEnd() (gated on full launch)
    void testQueueCustom(const std::string& name);  // run trackEvent() (gated on full launch)
    void testSeedAndReportCrash(const std::string& markerPath,
                                const std::string& fault, const std::string& code);  // never gated
    std::vector<std::string> testDrainPending();    // pop + return the queued event bodies
#endif

private:
    AnalyticsManager();
    ~AnalyticsManager();
    AnalyticsManager(const AnalyticsManager&) = delete;
    AnalyticsManager& operator=(const AnalyticsManager&) = delete;

    // True only when a real Aptabase App Key has been configured (not the
    // shipped placeholder). When false, no Aptabase event is sent.
    static bool isConfigured();

    // True only when a real GoatCounter site code has been configured.
    static bool goatCounterConfigured();

    // Load + update the persisted anonymous identity in mxbmrp3_analytics.json:
    // sets m_installId (generating a UUID on first run), m_prevVersion (the
    // version from the previous launch, "" on a fresh install), m_firstSeenUnix
    // (first-run timestamp) and m_launchCount (this launch's lifetime count),
    // then persists installId + current version + firstSeen + launchCount.
    // m_installId is left empty if both reading and writing failed.
    void loadAndUpdateIdentity(const char* savePath);

    // Build the full app_started JSON body (a one-element events array). Reads
    // game-thread state (settings, HUD visibility), so call from initialize().
    std::string buildEventBody() const;

    // Build a one-event array body for a custom event under the current session.
    std::string buildCustomEventBody(const std::string& eventName,
                                     const std::map<std::string, std::string>& props) const;

    // Build a one-event body carrying a NUMERIC duration_seconds (so Aptabase can
    // sum/avg playtime - buildCustomEventBody only sends string props) plus any extra
    // string props (crash fault/code, crash-time plugin/game versions). Used for the
    // session_end and crash events.
    std::string buildSessionEventBody(const std::string& eventName, long long durationSec,
                                      const std::map<std::string, std::string>& extraProps) const;

    // If the crash handler left a pending_crash.json marker from a previous launch,
    // queue a "crash" event (fault module+offset, code, recovered duration) under
    // this session and delete the marker. No-op if there's no marker. Called from
    // initialize() only when Aptabase is active this launch.
    void sendPendingCrashReport();

    // Queue the session_end event (with this session's duration) at shutdown so a
    // clean exit's length is tracked. Crashed sessions are covered by the crash
    // event's duration instead (no session_end is sent).
    void queueSessionEnd();

    // Background drain loop for trackEvent(): POSTs queued custom events to
    // Aptabase under the session, exits when shutdown is requested and the
    // queue is empty.
    void eventWorkerLoop();

    // Background sender for the startup beacon (thread entry): fires the Aptabase
    // POST then the GoatCounter GET. Args are passed by value so the thread never
    // touches game-thread members. Cancellable via closeHandles() during shutdown.
    void postBeacon(std::wstring host, std::string body);

    // Aptabase app_started POST. No-op caller path when Aptabase isn't configured.
    void postAptabase(const std::wstring& host, const std::string& body);

    // Synchronous one-shot POST used by the custom-event worker. Short timeout
    // so a slow send can't stall shutdown for long; not cancellable.
    void postSync(const std::wstring& host, const std::string& body, unsigned long timeoutMs);

    // GoatCounter headcount hit (one authenticated POST to /api/v0/count — the
    // public pixel bot-filters non-browser senders) — runs on the same
    // background thread as the Aptabase beacon. No-op if not configured.
    void sendGoatCounterHit();

    // Fetch + apply the remote sampling config (developer cost lever; runs on the
    // background beacon thread, once per launch, before the Aptabase POST). Reads
    // aptabase_full_sample ∈ [0,1] from the public config file and rolls once to set
    // m_fullLaunch — whether this launch sends session_end + custom on top of the
    // always-sent app_started (+ crash). Fail-open to full (m_fullLaunch stays true) on
    // any fetch/parse failure. See analytics_remote_config.h.
    void applyRemoteSampling();

    // WinHTTP GET the remote config file into `out`. Returns true on HTTP 200 with a
    // body. Short timeout; publishes handles like the beacon so shutdown can cancel it.
    bool fetchRemoteConfig(std::string& out);

    // Build the GoatCounter /api/v0/count JSON body (one launch hit).
    std::string buildGoatCounterBody() const;

    // Close any live WinHTTP handles to abort the blocking POST on shutdown.
    void closeHandles();

    std::atomic<bool> m_enabled;          // opt-out flag (default true)
    std::atomic<bool> m_shutdownRequested;
    // Whether THIS launch sends the full event set (session_end + custom) on top of the
    // always-sent app_started (+ crash). Set once by applyRemoteSampling() on the
    // background thread; read by queueSessionEnd()/trackEvent() on the game thread — hence
    // atomic. Defaults true (full): a fast quit before the config fetch completes, or any
    // fetch failure, keeps the consented full behavior (fail-open).
    std::atomic<bool> m_fullLaunch{true};
    std::string m_installId;              // anonymous random UUID (game thread)
    std::string m_prevVersion;            // plugin version from the previous launch ("" = new install)
    std::string m_versionStatus;          // "new" | "update" | "same"
    unsigned long long m_firstSeenUnix = 0;  // first-run timestamp (epoch seconds)
    unsigned long long m_launchCount = 0;    // this install's lifetime launch count
    std::wstring m_wAppKey;               // Aptabase App-Key header value
    std::wstring m_host;                  // Aptabase ingest host (region-derived)
    std::wstring m_gcHost;                // GoatCounter host (<code>.goatcounter.com)
    std::string m_gcBody;                 // GoatCounter /api/v0/count JSON body
    std::string m_sessionId;             // per-launch session id
    std::string m_osVersion;             // real OS/Wine version (registry-derived, shim-proof)
    unsigned long long m_sessionStartUnix = 0;  // this launch's start (epoch seconds)
    unsigned long long m_prevSessionStart = 0;  // previous launch's start (crash-duration recovery)
    std::string m_pendingCrashPath;      // <savePath>\mxbmrp3\pending_crash.json (crash marker)

    std::thread m_thread;

    // Live WinHTTP handles, published by postBeacon so shutdown() can cancel
    // the blocking send. void* to keep windows.h out of this header.
    std::mutex m_handleMutex;
    void* m_hSession;
    void* m_hConnect;
    void* m_hRequest;

    // Custom-event queue drained by m_eventWorker (started only when Aptabase is
    // active this launch). Each entry is a finished JSON body ready to POST.
    std::deque<std::string> m_eventQueue;
    std::mutex m_eventMutex;
    std::condition_variable m_eventCv;
    std::thread m_eventWorker;
};
