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
#include "plugin_data.h"
#include "plugin_constants.h"
#include "plugin_utils.h"
#include "color_config.h"
#include "font_config.h"
#include "../diagnostics/logger.h"

#include <chrono>
#include <cstring>
#include <fstream>

using namespace PluginConstants;

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

    // Create server on game thread so stop() always has a valid pointer.
    // Route setup and listen() happen on the server thread.
    m_server = std::make_unique<httplib::Server>();

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

    // Only push updates for data types relevant to standings/event log
    switch (changeType) {
    case DataChangeType::SessionData:
    case DataChangeType::RaceEntries:
    case DataChangeType::Standings:
    case DataChangeType::EventLog:
    case DataChangeType::SpectateTarget:
        break;
    default:
        return;  // Ignore telemetry, input, debug metrics, etc.
    }

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

// ============================================================================
// JSON Snapshot Builder
// ============================================================================
// Called on the game thread only (PluginData is not thread-safe).
// Uses direct string building instead of nlohmann::json to avoid per-frame
// heap allocations from json objects. This runs every time standings change,
// so it needs to be fast.

// Append a JSON-escaped string value (handles \, ", control chars)
static void appendJsonString(std::string& out, const char* str) {
    out += '"';
    for (const char* p = str; *p; ++p) {
        switch (*p) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:
            if (static_cast<unsigned char>(*p) < 0x20) {
                char esc[8];
                snprintf(esc, sizeof(esc), "\\u%04x", static_cast<unsigned char>(*p));
                out += esc;
            } else {
                out += *p;
            }
        }
    }
    out += '"';
}

static void appendJsonInt(std::string& out, int val) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", val);
    out += buf;
}

static void appendJsonFloat(std::string& out, float val) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.2f", val);
    out += buf;
}

std::string HttpServer::buildJsonSnapshot() const {
    const PluginData& pd = PluginData::getInstance();
    const SessionData& session = pd.getSessionData();
    const auto& classificationOrder = pd.getDisplayClassificationOrder();
    const auto& raceEntries = pd.getRaceEntries();
    const auto& standings = pd.getStandings();
    int displayRaceNum = pd.getDisplayRaceNum();

    // Pre-allocate ~16KB - typical for a 30-rider grid with events
    std::string out;
    out.reserve(16384);

    // No active session (cleared/menu) — return minimal idle snapshot.
    // Empty type/state signal "in menus" to the client, which supplies its
    // own label.
    if (session.session == -1) {
        out += "{\"session\":{\"time\":\"--:--\",\"timeMs\":0,\"type\":\"\",\"state\":\"\""
               ",\"numLaps\":0,\"sessionLength\":0,\"isRace\":false"
               ",\"trackName\":\"\",\"trackLength\":0,\"leaderLap\":-1"
               ",\"pluginVersion\":\"";
        out += PLUGIN_VERSION;
        out += "\"},\"standings\":[],\"events\":[]}";
        return out;
    }

    // Determine session mode once, used by session and standings sections
    bool isRaceSession = (session.eventType == PluginConstants::EventType::RACE)
        && (session.session == PluginConstants::Session::RACE_1
            || session.session == PluginConstants::Session::RACE_2);

    out += "{\"session\":{";

    // --- Session info ---
    {
        // Session time (formatted as MM:SS) - always show the actual value
        int sessionTime = pd.getSessionTime();
        char timeBuf[16];
        if (sessionTime > 0) {
            PluginUtils::formatTimeMinutesSeconds(sessionTime, timeBuf, sizeof(timeBuf));
        } else {
            snprintf(timeBuf, sizeof(timeBuf), "00:00");
        }
        out += "\"time\":";
        appendJsonString(out, timeBuf);

        out += ",\"timeMs\":";
        appendJsonInt(out, sessionTime);

        // Session type
        const char* sessionStr = PluginUtils::getSessionString(session.eventType, session.session);
        out += ",\"type\":";
        appendJsonString(out, sessionStr ? sessionStr : "");

        // Session state
        const char* stateStr = PluginUtils::getSessionStateString(session.sessionState);
        out += ",\"state\":";
        appendJsonString(out, stateStr ? stateStr : "");

        out += ",\"numLaps\":";
        appendJsonInt(out, session.sessionNumLaps);

        out += ",\"sessionLength\":";
        appendJsonInt(out, session.sessionLength);

        out += ",\"isRace\":";
        out += isRaceSession ? "true" : "false";

        // Track info
        out += ",\"trackName\":";
        appendJsonString(out, session.trackName);

        out += ",\"trackLength\":";
        appendJsonFloat(out, session.trackLength);

        // Leader lap
        int leaderLap = 0;
        if (!classificationOrder.empty()) {
            auto it = standings.find(classificationOrder[0]);
            if (it != standings.end()) {
                leaderLap = it->second.numLaps;
            }
        }
        out += ",\"leaderLap\":";
        appendJsonInt(out, leaderLap);

        // Plugin version (used by the web overlay to show a startup banner)
        out += ",\"pluginVersion\":\"";
        out += PLUGIN_VERSION;
        out += "\"";

        // Draw state: 0=on track (riding), 1=spectating, 2=replay
        out += ",\"isSpectating\":";
        out += (pd.getDrawState() >= 1) ? "true" : "false";

        // Color palette from in-game settings (ABGR → CSS hex)
        const ColorConfig& colors = ColorConfig::getInstance();
        out += ",\"palette\":{";
        {
            auto appendColor = [&](const char* name, unsigned long abgr) {
                char hex[8];
                snprintf(hex, sizeof(hex), "#%02x%02x%02x",
                    abgr & 0xFF, (abgr >> 8) & 0xFF, (abgr >> 16) & 0xFF);
                out += '"';
                out += name;
                out += "\":";
                appendJsonString(out, hex);
            };
            appendColor("primary", colors.getPrimary());
            out += ','; appendColor("secondary", colors.getSecondary());
            out += ','; appendColor("tertiary", colors.getTertiary());
            out += ','; appendColor("muted", colors.getMuted());
            out += ','; appendColor("background", colors.getBackground());
            out += ','; appendColor("positive", colors.getPositive());
            out += ','; appendColor("warning", colors.getWarning());
            out += ','; appendColor("neutral", colors.getNeutral());
            out += ','; appendColor("negative", colors.getNegative());
            out += ','; appendColor("accent", colors.getAccent());
        }
        out += '}';

        // Font categories from in-game settings
        const FontConfig& fonts = FontConfig::getInstance();
        out += ",\"fonts\":{";
        {
            auto appendFont = [&](const char* name, FontCategory cat) {
                out += '"';
                out += name;
                out += "\":";
                appendJsonString(out, fonts.getFontName(cat));
            };
            appendFont("title", FontCategory::TITLE);
            out += ','; appendFont("normal", FontCategory::NORMAL);
            out += ','; appendFont("strong", FontCategory::STRONG);
            out += ','; appendFont("digits", FontCategory::DIGITS);
        }
        out += '}';
    }

    out += "},\"standings\":[";

    // --- Standings ---
    {
        const LapLogEntry* overallBest = pd.getOverallBestLap();
        bool firstRider = true;
        int position = 1;

        for (int raceNum : classificationOrder) {
            auto entryIt = raceEntries.find(raceNum);
            auto standingIt = standings.find(raceNum);
            if (entryIt == raceEntries.end()) {
                continue;  // Skip riders not yet in race entries (don't increment position)
            }

            if (!firstRider) out += ',';
            firstRider = false;

            out += "{\"pos\":";
            appendJsonInt(out, position);
            out += ",\"num\":";
            appendJsonInt(out, raceNum);
            out += ",\"name\":";
            appendJsonString(out, entryIt->second.truncatedName);
            out += ",\"fullName\":";
            appendJsonString(out, entryIt->second.name);
            out += ",\"bike\":";
            appendJsonString(out, entryIt->second.bikeName);

            // Brand color as CSS hex (e.g. "#ff6600") and brand name
            // In-game colors are stored as ABGR: R=bits[0:7], G=bits[8:15], B=bits[16:23]
            unsigned long bc = entryIt->second.bikeBrandColor;
            if (bc != 0) {
                char colorBuf[8];
                snprintf(colorBuf, sizeof(colorBuf), "#%02x%02x%02x",
                    bc & 0xFF, (bc >> 8) & 0xFF, (bc >> 16) & 0xFF);
                out += ",\"brandColor\":";
                appendJsonString(out, colorBuf);
            }
            if (entryIt->second.brandName && entryIt->second.brandName[0] != '\0') {
                out += ",\"brand\":";
                appendJsonString(out, entryIt->second.brandName);
            }

            if (standingIt != standings.end()) {
                const StandingsData& s = standingIt->second;

                // Gap formatting - differs between race and non-race sessions
                char gapBuf[32];
                gapBuf[0] = '\0';
                if (s.state == RiderState::DNS) {
                    snprintf(gapBuf, sizeof(gapBuf), "%s", DisplayStrings::RiderState::DNS);
                } else if (s.state == RiderState::RETIRED) {
                    snprintf(gapBuf, sizeof(gapBuf), "%s", DisplayStrings::RiderState::RETIRED);
                } else if (s.state == RiderState::DSQ) {
                    snprintf(gapBuf, sizeof(gapBuf), "%s", DisplayStrings::RiderState::DISQUALIFIED);
                } else if (isRaceSession) {
                    // Race: leader tag, relative gaps, lap gaps
                    if (position == 1) {
                        snprintf(gapBuf, sizeof(gapBuf), "Leader");
                    } else if (s.gapLaps > 0) {
                        snprintf(gapBuf, sizeof(gapBuf), "+%dL", s.gapLaps);
                    } else if (s.gap > 0) {
                        PluginUtils::formatTimeDiff(gapBuf, sizeof(gapBuf), s.gap);
                    }
                } else {
                    // Non-race (practice, qualify, etc.): absolute best lap for everyone
                    if (s.bestLap > 0) {
                        PluginUtils::formatLapTime(s.bestLap, gapBuf, sizeof(gapBuf));
                    }
                }
                out += ",\"gap\":";
                appendJsonString(out, gapBuf);
                out += ",\"gapMs\":";
                appendJsonInt(out, s.gap);
                out += ",\"gapLaps\":";
                appendJsonInt(out, s.gapLaps);

                // State info
                out += ",\"state\":";
                appendJsonInt(out, static_cast<int>(s.state));
                out += ",\"numLaps\":";
                appendJsonInt(out, s.numLaps);
                out += ",\"inPit\":";
                out += (s.pit != 0) ? "true" : "false";

                // Penalty
                int penaltySec = 0;
                if (s.penalty > 0) {
                    penaltySec = (s.penalty + 500) / 1000;
                }
                out += ",\"penalty\":";
                appendJsonInt(out, penaltySec);
                out += ",\"penaltyMs\":";
                appendJsonInt(out, s.penalty);

                // Best lap (always full precision - lap times use .mmm, not .t)
                char bestBuf[16];
                bestBuf[0] = '\0';
                if (s.bestLap > 0) {
                    PluginUtils::formatLapTime(s.bestLap, bestBuf, sizeof(bestBuf));
                }
                out += ",\"bestLap\":";
                appendJsonString(out, bestBuf);
                out += ",\"bestLapMs\":";
                appendJsonInt(out, s.bestLap);

                // Last lap time (always full precision - lap times use .mmm, not .t)
                const IdealLapData* idealLap = pd.getIdealLapData(raceNum);
                if (idealLap && idealLap->lastLapTime > 0) {
                    char lastBuf[16];
                    PluginUtils::formatLapTime(idealLap->lastLapTime, lastBuf, sizeof(lastBuf));
                    out += ",\"lastLap\":";
                    appendJsonString(out, lastBuf);
                    out += ",\"lastLapMs\":";
                    appendJsonInt(out, idealLap->lastLapTime);
                }

                // Finish detection
                bool isFinished = session.isRiderFinished(s.numLaps, s.numLapsAtLeaderFinish);
                out += ",\"finished\":";
                out += isFinished ? "true" : "false";

                // Chips - all status indicators, web UI decides which to display
                bool isInactive = (s.state == RiderState::DNS || s.state == RiderState::RETIRED || s.state == RiderState::DSQ);
                out += ",\"chips\":[";
                if (!isInactive) {
                    bool firstChip = true;
                    auto addChip = [&](const char* chip) {
                        if (!firstChip) out += ',';
                        firstChip = false;
                        out += '"';
                        out += chip;
                        out += '"';
                    };
                    if (isFinished) addChip("finished");
                    if (s.pit != 0) addChip("pit");
                    if (penaltySec > 0) addChip("penalty");
                    if (raceNum == displayRaceNum) addChip("camera");
                    if (overallBest && overallBest->lapNum >= 0 && s.bestLap > 0 && s.bestLap == overallBest->lapTime) {
                        addChip("fastest");
                    }
                }
                out += ']';
            }

            out += '}';
            ++position;
        }
    }

    out += "],\"events\":[";

    // --- Event Log ---
    // Send all events — the web UI filters client-side.
    // Cap serialized events to avoid expensive serialization during long sessions.
    static constexpr size_t MAX_SERIALIZED_EVENTS = 50;
    {
        const auto& eventLog = pd.getEventLog();
        size_t startIdx = (eventLog.size() > MAX_SERIALIZED_EVENTS)
            ? eventLog.size() - MAX_SERIALIZED_EVENTS : 0;
        bool firstEvent = true;

        for (size_t i = startIdx; i < eventLog.size(); ++i) {
            const auto& entry = eventLog[i];

            if (!firstEvent) out += ',';
            firstEvent = false;

            out += "{\"message\":";
            appendJsonString(out, entry.message);

            if (entry.detail[0] != '\0') {
                out += ",\"detail\":";
                appendJsonString(out, entry.detail);
            }

            out += ",\"type\":";
            appendJsonInt(out, static_cast<int>(entry.type));
            out += ",\"sessionTimeMs\":";
            appendJsonInt(out, entry.sessionTimeMs);

            // Format wall clock time
            auto tt = std::chrono::system_clock::to_time_t(entry.systemTime);
            struct tm tmBuf{};
            localtime_s(&tmBuf, &tt);
            char clockBuf[16];
            snprintf(clockBuf, sizeof(clockBuf), "%02d:%02d:%02d", tmBuf.tm_hour, tmBuf.tm_min, tmBuf.tm_sec);
            out += ",\"clockTime\":";
            appendJsonString(out, clockBuf);

            // Format session time
            char sessionTimeBuf[16];
            PluginUtils::formatTimeMinutesSeconds(entry.sessionTimeMs, sessionTimeBuf, sizeof(sessionTimeBuf));
            out += ",\"sessionTime\":";
            appendJsonString(out, sessionTimeBuf);

            out += '}';
        }
    }

    out += "]}";
    return out;
}

// ============================================================================
// Server Thread
// ============================================================================

void HttpServer::serverThread() {
    // m_server is created by start() on the game thread so that stop()
    // can safely call m_server->stop() without racing on the pointer.

    // GET /sw.js - service worker, with PLUGIN_VERSION substituted into the
    // cache name so a plugin update automatically invalidates cached overlay
    // assets in the browser. Registered before the static mount so this
    // handler takes precedence over the on-disk file. Note: this bypasses
    // the AssetManager user-override path — sw.js is intentionally not
    // user-customizable so version substitution always runs against the
    // bundled copy.
    m_server->Get("/sw.js", [this](const httplib::Request&, httplib::Response& res) {
        std::string path = m_webRoot + "/sw.js";
        std::ifstream f(path, std::ios::binary);
        if (!f) {
            res.status = 404;
            return;
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
        // Service workers must be served with a JS MIME type and should not
        // be cached by the HTTP layer so updates are picked up promptly.
        res.set_header("Cache-Control", "no-cache");
        res.set_content(body, "application/javascript; charset=utf-8");
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

    // CORS headers for browser sources
    m_server->set_default_headers({
        {"Access-Control-Allow-Origin", "*"},
        {"Access-Control-Allow-Methods", "GET"},
        {"Access-Control-Allow-Headers", "Content-Type"}
    });

    // GET /api/state - JSON snapshot (for initial load / polling fallback)
    m_server->Get("/api/state", [this](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(m_dataMutex);
        res.set_content(m_cachedJson, "application/json");
    });

    // GET /api/events - SSE stream (push on data change)
    m_server->Get("/api/events", [this](const httplib::Request&, httplib::Response& res) {
        // Reject if too many SSE connections (avoid starving the thread pool)
        if (m_sseConnections.load() >= MAX_SSE_CONNECTIONS) {
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
                ++m_sseConnections;

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

                int throttleMs = m_throttleMs.load();

                // Loop: wait for data changes, then push updates.
                // Sends an SSE keepalive on each timeout to detect dead connections.
                while (!m_shutdownRequested) {
                    std::string sseMsg;
                    {
                        std::unique_lock<std::mutex> lock(m_dataMutex);
                        m_dataCondition.wait_for(lock,
                            std::chrono::milliseconds(throttleMs),
                            [this, clientSeq] {
                                return m_sseSequence > clientSeq || m_shutdownRequested.load();
                            });

                        if (m_shutdownRequested) return false;

                        if (m_sseSequence > clientSeq) {
                            clientSeq = m_sseSequence;
                            sseMsg = "id: " + std::to_string(clientSeq)
                                + "\ndata: " + m_cachedJson + "\n\n";
                        }
                    }

                    if (!sseMsg.empty()) {
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
}
