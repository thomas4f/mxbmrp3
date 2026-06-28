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

    // Build the GoatCounter /api/v0/count JSON body (one launch hit).
    std::string buildGoatCounterBody() const;

    // Close any live WinHTTP handles to abort the blocking POST on shutdown.
    void closeHandles();

    std::atomic<bool> m_enabled;          // opt-out flag (default true)
    std::atomic<bool> m_shutdownRequested;
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
