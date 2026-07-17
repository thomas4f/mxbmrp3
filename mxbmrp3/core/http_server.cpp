// ============================================================================
// core/http_server.cpp
// Embedded HTTP server for serving race data to external tools (OBS, etc.)
// ============================================================================

// Thread pool must be larger than MAX_SSE_CONNECTIONS so REST requests
// are never starved by long-lived SSE connections holding threads.
#define CPPHTTPLIB_THREAD_POOL_COUNT 8

// httplib.h must be included before windows.h to avoid winsock conflicts
#include "../vendor/httplib/httplib.h"

#include "http_server.h"
#include "http_server_internal.h"
#include "plugin_data.h"
#include "plugin_constants.h"
#include "plugin_utils.h"
#include "color_config.h"
#include "font_config.h"
#include "tracked_riders_manager.h"
#include "director_manager.h"
#include "../diagnostics/logger.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <utility>
#include <vector>

using namespace PluginConstants;
using namespace http_server_detail;

HttpServer::HttpServer()
    : m_enabled(false)
    , m_initialized(false)
    , m_running(false)
    , m_shutdownRequested(false)
    , m_port(DEFAULT_PORT)
    , m_throttleMs(DEFAULT_THROTTLE_MS)
    , m_bindAddress(DEFAULT_BIND_ADDRESS)
    , m_sseSequence(0)
    , m_webRoot("plugins\\mxbmrp3_data\\web") {
}

HttpServer::~HttpServer() {
    shutdown();
}

HttpServer& HttpServer::getInstance() {
    static HttpServer instance;
    return instance;
}

void HttpServer::initialize(const char* savePath) {
    if (savePath) {
        m_savePath = savePath;
    }
    m_initialized = true;

    // Start the server now that all settings (port, throttle, bind address)
    // have been loaded. setEnabled() during settings load only sets the flag
    // without starting, because [Advanced] settings haven't been parsed yet.
    if (m_enabled && !m_running) {
        start();
    }
    DEBUG_INFO_F("HttpServer initialized (port=%d, throttle=%dms, webRoot=%s)",
        m_port.load(), m_throttleMs.load(), m_webRoot.c_str());
}

void HttpServer::shutdown() {
    stop();
    DEBUG_INFO("HttpServer shutdown");
}

void HttpServer::start() {
    if (m_running) return;

    m_shutdownRequested = false;

    // Build initial snapshot so SSE clients get data immediately
    {
        std::lock_guard<std::mutex> lock(m_dataMutex);
        m_sseSequence = 0;
        m_cachedJson = buildJsonSnapshot();
    }
    m_snapshotStale = false;

    // Create server on game thread so stop() always has a valid pointer.
    // Route setup and listen() happen on the server thread.
    m_server = std::make_unique<httplib::Server>();

    // Disable SO_REUSEADDR so the server fails to bind if the port is already
    // taken, rather than silently sharing it with another process.
    m_server->set_socket_options([](socket_t) {});

    m_serverThread = std::thread(&HttpServer::serverThread, this);

    // Block until httplib has bound the port (or failed).
    // This avoids a race where isRunning() returns true before listen() succeeds.
    m_server->wait_until_ready();
    m_running = m_server->is_running();

    if (m_running) {
        DEBUG_INFO_F("HttpServer listening on port %d", m_port.load());
    } else {
        DEBUG_WARN_F("HttpServer failed to start on port %d", m_port.load());
        // Clean up the failed thread
        if (m_serverThread.joinable()) {
            m_serverThread.join();
        }
        m_server.reset();
    }
}

void HttpServer::stop() {
    if (!m_running && !m_serverThread.joinable()) return;

    m_shutdownRequested = true;

    // Wake up any waiting SSE threads
    m_dataCondition.notify_all();

    // Stop the httplib server (thread-safe, causes listen() to return).
    // Safe to read m_server here: stop() calls m_server->stop() which
    // unblocks listen(), and only after join() below does serverThread
    // reset m_server — so m_server can't be nulled while we dereference.
    if (m_server) {
        m_server->stop();
    }

    if (m_serverThread.joinable()) {
        m_serverThread.join();
    }

    m_server.reset();
    m_running = false;
    DEBUG_INFO("HttpServer stopped");
}

void HttpServer::setEnabled(bool enabled) {
    if (m_enabled == enabled) return;
    m_enabled = enabled;

    // Before initialize(), just store the flag — start() is deferred
    // until initialize() when all settings (port, bind address) are loaded.
    if (!m_initialized) return;

    if (enabled) {
        start();
    } else {
        stop();
    }
}

void HttpServer::setPort(int port) {
    m_port = std::max(MIN_PORT, std::min(port, MAX_PORT));
}

void HttpServer::setThrottleMs(int ms) {
    m_throttleMs = std::max(MIN_THROTTLE_MS, std::min(ms, MAX_THROTTLE_MS));
}

void HttpServer::setBindAddress(const std::string& addr) {
    std::lock_guard<std::mutex> lock(m_bindMutex);
    m_bindAddress = addr.empty() ? DEFAULT_BIND_ADDRESS : addr;
}

std::string HttpServer::getBindAddress() const {
    std::lock_guard<std::mutex> lock(m_bindMutex);
    return m_bindAddress;
}

void HttpServer::onDataChanged(DataChangeType changeType) {
    if (!m_running) return;

    // Only push updates for data types relevant to standings/event log.
    // The relevant types split into two classes:
    //  - frequent: Standings fires many times per second on a full grid,
    //    EventLog on every race event. These are the expensive firehose and
    //    are skipped (cache marked stale) while no overlay is consuming.
    //  - rare: SessionData / RaceEntries / SpectateTarget are transition
    //    events. They ALWAYS rebuild, client or not: a transition (e.g. the
    //    EventDeinit -> "in menus" change) may be the LAST notification for
    //    minutes - the plugin gets NO callbacks while the player sits in
    //    menus - so skipping one would leave a later-connecting client
    //    serving a stale snapshot with no rebuild opportunity ever arriving.
    bool relevant;
    bool frequent = false;
    switch (changeType) {
    case DataChangeType::Standings:
    case DataChangeType::EventLog:
        relevant = true;
        frequent = true;
        break;
    case DataChangeType::SessionData:
    case DataChangeType::RaceEntries:
    case DataChangeType::SpectateTarget:
        relevant = true;
        break;
    default:
        relevant = false;  // Telemetry, input, debug metrics, etc.
        break;
    }

    // While inactive (no SSE client, no recent /api/state poll), frequent
    // changes only mark the cache stale. Once a client appears, the next
    // notification of ANY type rebuilds - telemetry fires every tick
    // in-session, so the catch-up window after connect is tiny. Stale state
    // cannot survive into a quiet period: the transition out of the session
    // is a rare-type change, which rebuilds unconditionally.
    if (relevant) {
        if (frequent && !hasActiveClients()) {
            m_snapshotStale = true;
            return;
        }
    } else {
        if (!m_snapshotStale || !hasActiveClients()) {
            return;  // Irrelevant change and nothing to catch up on
        }
    }
    m_snapshotStale = false;

    // Build JSON snapshot on the game thread where PluginData access is safe.
    // Server threads only read the cached string under the mutex.
    std::string snapshot = buildJsonSnapshot();

    {
        std::lock_guard<std::mutex> lock(m_dataMutex);
        m_cachedJson = std::move(snapshot);
        ++m_sseSequence;
    }
    m_dataCondition.notify_all();
}

// Maps the panel enum to the overlay's createSlotPanel name (overlay-panels.js). NONE is
// the empty string (the client's seq starts at 0 and never fires for it).
const char* HttpServer::overlayPanelName(int panel) {
    switch (static_cast<OverlayPanel>(panel)) {
    // Strings are the overlay-panels.js createSlotPanel names (NOT the enumerator names).
    case OverlayPanel::LAST_LAP:    return "fastlap";
    case OverlayPanel::FASTEST_LAP: return "bestlap";
    case OverlayPanel::DOWN_ORDER:  return "tail";
    case OverlayPanel::SECTORS:     return "sectors";
    case OverlayPanel::CHARTS:      return "charts";
    default:                    return "";
    }
}

void HttpServer::forceOverlayPanel(OverlayPanel panel) {
    if (!m_running) return;

    // Record the command and bump the sequence so the client treats this as a
    // discrete new press (edge-triggered), then push immediately. A broadcaster
    // keypress always sends, regardless of the frequent/rare change gating.
    m_forcedPanel.store(static_cast<int>(panel));
    m_forcedSeq.fetch_add(1);

    // Built on the game thread (the caller), where PluginData access is safe.
    std::string snapshot = buildJsonSnapshot();
    {
        std::lock_guard<std::mutex> lock(m_dataMutex);
        m_cachedJson = std::move(snapshot);
        ++m_sseSequence;
    }
    m_dataCondition.notify_all();
}

// ============================================================================
// Server Thread
// ============================================================================

void HttpServer::serverThread() {
    // m_server is created by start() on the game thread so that stop()
    // can safely call m_server->stop() without racing on the pointer.
    //
    // Exception barrier: an uncaught throw inside a std::thread calls
    // std::terminate() and kills the host game process. httplib catches
    // most exceptions inside request handlers, but the setup code below
    // (file I/O, mount_point, etc.) and the chunked SSE provider can
    // still escape, so wrap the whole thread body.
    try {

        // /sw.js and /custom.css need custom serving (version substitution and
        // no-cache headers), but both files also exist under the static mount,
        // and httplib's routing() runs handle_file_request() (the mount) BEFORE
        // dispatching Get() handlers — a plain Get() registration for a mounted
        // file is dead code regardless of registration order. The pre-routing
        // handler is the one hook that runs before the mount, so these two
        // paths are intercepted there. Default headers (CORS) are applied to
        // the response before routing, so they are preserved.
        //
        // GET /sw.js - service worker, with PLUGIN_VERSION substituted into the
        // cache name so a plugin update automatically invalidates cached overlay
        // assets in the browser. Note: this bypasses the AssetManager
        // user-override path — sw.js is intentionally not user-customizable so
        // version substitution always runs against the bundled copy.
        //
        // GET /custom.css - user style overrides, served with no-cache so edits
        // show up on the next browser reload without waiting for HTTP cache
        // expiry (the static mount would serve it with heuristic freshness).
        // The service worker also bypasses caching for this path (see sw.js).
        m_server->set_pre_routing_handler(
            [this](const httplib::Request& req, httplib::Response& res) {
            if (req.method != "GET" && req.method != "HEAD") {
                return httplib::Server::HandlerResponse::Unhandled;
            }
            if (req.path == "/sw.js") {
                std::string path = m_webRoot + "/sw.js";
                std::ifstream f(path, std::ios::binary);
                if (!f) {
                    res.status = 404;
                    return httplib::Server::HandlerResponse::Handled;
                }
                std::string body((std::istreambuf_iterator<char>(f)),
                                 std::istreambuf_iterator<char>());
                const std::string placeholder = "__PLUGIN_VERSION__";
                const size_t versionLen = std::strlen(PLUGIN_VERSION);
                size_t pos = 0;
                while ((pos = body.find(placeholder, pos)) != std::string::npos) {
                    body.replace(pos, placeholder.size(), PLUGIN_VERSION);
                    pos += versionLen;
                }
                // Service workers must be served with a JS MIME type and should
                // not be cached by the HTTP layer so updates are picked up
                // promptly.
                res.set_header("Cache-Control", "no-cache");
                res.set_content(body, "application/javascript; charset=utf-8");
                return httplib::Server::HandlerResponse::Handled;
            }
            if (req.path == "/custom.css") {
                std::string path = m_webRoot + "/custom.css";
                std::ifstream f(path, std::ios::binary);
                if (!f) {
                    res.status = 404;
                    return httplib::Server::HandlerResponse::Handled;
                }
                std::string body((std::istreambuf_iterator<char>(f)),
                                 std::istreambuf_iterator<char>());
                res.set_header("Cache-Control", "no-cache");
                res.set_content(body, "text/css; charset=utf-8");
                return httplib::Server::HandlerResponse::Handled;
            }
            return httplib::Server::HandlerResponse::Unhandled;
        });

        // Mount static files from web directory
        if (!m_webRoot.empty()) {
            // Override default mime types to include charset=utf-8 so browsers
            // don't fall back to Latin-1 and mangle non-ASCII chars (em dashes,
            // etc.) in our text assets.
            m_server->set_file_extension_and_mimetype_mapping("html", "text/html; charset=utf-8");
            m_server->set_file_extension_and_mimetype_mapping("css", "text/css; charset=utf-8");
            m_server->set_file_extension_and_mimetype_mapping("js", "application/javascript; charset=utf-8");
            m_server->set_file_extension_and_mimetype_mapping("json", "application/json; charset=utf-8");
            m_server->set_file_extension_and_mimetype_mapping("svg", "image/svg+xml; charset=utf-8");

            auto ret = m_server->set_mount_point("/", m_webRoot);
            if (!ret) {
                DEBUG_INFO_F("HttpServer WARNING: web root not found: %s", m_webRoot.c_str());
            }
        }

        // CORS headers for browser sources.
        //
        // Deliberate trade-off: the wildcard origin lets OBS browser sources and
        // arbitrary local tooling read the overlay data without configuration.
        // It also means any webpage open in a browser on this machine (or on the
        // LAN when bound to 0.0.0.0) can read live session data and hold SSE
        // slots. The data is low-sensitivity (public race state, rider names)
        // and every endpoint is read-only, so convenience wins here; revisit if
        // a write endpoint is ever added.
        m_server->set_default_headers({
            {"Access-Control-Allow-Origin", "*"},
            {"Access-Control-Allow-Methods", "GET"},
            {"Access-Control-Allow-Headers", "Content-Type"}
        });

        // GET /api/logos - list PNG files in the logos/ subdirectory
        m_server->Get("/api/logos", [this](const httplib::Request&, httplib::Response& res) {
            std::string logosDir = m_webRoot + "\\logos";
            std::string searchPath = logosDir + "\\*.png";

            // Collect filenames first so we can sort for consistent order
            std::vector<std::string> files;
            WIN32_FIND_DATAA findData;
            HANDLE hFind = FindFirstFileA(searchPath.c_str(), &findData);
            if (hFind != INVALID_HANDLE_VALUE) {
                do {
                    if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                        files.emplace_back(findData.cFileName);
                    }
                } while (FindNextFileA(hFind, &findData));
                FindClose(hFind);
            }
            std::sort(files.begin(), files.end());

            std::string json = "[";
            for (size_t i = 0; i < files.size(); ++i) {
                if (i > 0) json += ',';
                appendJsonString(json, files[i].c_str());
            }
            json += ']';
            res.set_content(json, "application/json");
        });

        // GET /api/state - JSON snapshot (for initial load / polling fallback)
        m_server->Get("/api/state", [this](const httplib::Request&, httplib::Response& res) {
            // Record the poll so the game thread keeps the snapshot fresh
            // (see hasActiveClients) - the first response after an idle
            // period may be stale until the next data change rebuilds it
            m_lastStatePollMs.store(steadyNowMs());
            std::lock_guard<std::mutex> lock(m_dataMutex);
            res.set_content(m_cachedJson, "application/json");
        });

        // GET /api/events - SSE stream (push on data change)
        m_server->Get("/api/events", [this](const httplib::Request&, httplib::Response& res) {
            // Reject if too many SSE connections (avoid starving the thread pool).
            // Reserve the slot atomically: a plain load()-then-increment lets N
            // concurrent requests all pass the check before any of them increments.
            // fetch_add returns the prior value; if we're already at the cap, roll
            // back the speculative reservation and reject. The matching decrement is
            // the content provider's resource releaser (runs on any exit path).
            if (m_sseConnections.fetch_add(1) >= MAX_SSE_CONNECTIONS) {
                --m_sseConnections;
                res.status = 503;
                res.set_content("{\"error\":\"Too many connections\"}", "application/json");
                return;
            }

            res.set_header("Cache-Control", "no-cache");
            res.set_header("Connection", "keep-alive");
            res.set_header("X-Accel-Buffering", "no");

            res.set_chunked_content_provider(
                "text/event-stream",
                // Content provider - called by httplib to generate SSE data
                [this](size_t /*offset*/, httplib::DataSink& sink) -> bool {
                    // Slot already reserved atomically in the GET handler above;
                    // the resource releaser below balances it with a decrement.

                    // Per-client sequence tracking — each client independently
                    // detects new data by comparing against the global sequence.
                    // This avoids the race where m_dataChanged=false starved
                    // the second client when two wake from the same notify.
                    uint64_t clientSeq = 0;

                    // Send initial snapshot immediately
                    {
                        std::lock_guard<std::mutex> lock(m_dataMutex);
                        clientSeq = m_sseSequence;
                        std::string sseMsg = "id: " + std::to_string(clientSeq)
                            + "\ndata: " + m_cachedJson + "\n\n";
                        if (!sink.write(sseMsg.data(), sseMsg.size())) {
                            return false;
                        }
                    }

                    const int throttleMs = m_throttleMs.load();

                    // Keepalive cadence for idle connections — a failed write
                    // detects dead clients so their slot is released. Fixed and
                    // independent of the throttle (the throttle bounds how OFTEN
                    // data is pushed, not how often an idle stream is probed).
                    constexpr auto kKeepaliveInterval = std::chrono::seconds(15);

                    // Loop: wait for data changes, then push updates — but never
                    // more than one push per throttleMs (the documented contract
                    // of webServerThrottleMs: min interval between SSE pushes).
                    // Pushes arriving inside the throttle window coalesce: after
                    // waiting out the remainder we re-read m_sseSequence and send
                    // the LATEST cached snapshot, so nothing is lost — only
                    // intermediate snapshots are skipped.
                    auto lastPush = std::chrono::steady_clock::now();
                    while (!m_shutdownRequested) {
                        std::string sseMsg;
                        {
                            std::unique_lock<std::mutex> lock(m_dataMutex);
                            m_dataCondition.wait_for(lock, kKeepaliveInterval,
                                [this, clientSeq] {
                                    return m_sseSequence > clientSeq || m_shutdownRequested.load();
                                });

                            if (m_shutdownRequested) return false;

                            if (m_sseSequence > clientSeq && throttleMs > 0) {
                                // Data pending — enforce the min interval since
                                // the last push before sending it.
                                auto nextAllowed = lastPush + std::chrono::milliseconds(throttleMs);
                                m_dataCondition.wait_until(lock, nextAllowed,
                                    [this] { return m_shutdownRequested.load(); });
                                if (m_shutdownRequested) return false;
                            }

                            if (m_sseSequence > clientSeq) {
                                clientSeq = m_sseSequence;
                                sseMsg = "id: " + std::to_string(clientSeq)
                                    + "\ndata: " + m_cachedJson + "\n\n";
                            }
                        }

                        if (!sseMsg.empty()) {
                            lastPush = std::chrono::steady_clock::now();
                            if (!sink.write(sseMsg.data(), sseMsg.size())) {
                                return false;
                            }
                        } else {
                            // SSE comment keepalive — ignored by EventSource,
                            // but a failed write detects dead connections
                            if (!sink.write(":keepalive\n\n", 12)) {
                                return false;
                            }
                        }
                    }

                    return false;  // Shutdown requested
                },
                // Resource releaser - called when content provider returns (any exit path)
                [this](bool /*success*/) {
                    --m_sseConnections;
                }
            );
        });

        // Bind and listen (localhost only by default for security).
        // Use 0.0.0.0 to allow connections from other machines on the network.
        // start() calls wait_until_ready() + is_running() to detect success/failure.
        int port = m_port.load();
        std::string bindAddr = getBindAddress();
        m_server->listen(bindAddr.c_str(), port);

    } catch (const std::exception& e) {
        DEBUG_WARN_F("HttpServer thread terminated by exception: %s", e.what());
        // No thread is alive to serve requests anymore — clear the flag so
        // onDataChanged stops building snapshots that nobody will read.
        m_running = false;
        // If we threw *before* listen() bound the port, is_running_ was never set;
        // decommission() unblocks start()'s wait_until_ready() so it doesn't spin forever.
        if (m_server) m_server->decommission();
    } catch (...) {
        DEBUG_WARN("HttpServer thread terminated by unknown exception");
        m_running = false;
        if (m_server) m_server->decommission();
    }
}
