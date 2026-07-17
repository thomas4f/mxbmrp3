// ============================================================================
// core/http_server_snapshot.cpp
// HttpServer::buildJsonSnapshot() — builds the JSON state snapshot streamed to
// web overlays (OBS, etc.). Split out of http_server.cpp (which keeps server
// lifecycle, threading, routing, onDataChanged and the overlay-force command)
// when that file grew past ~1.2k lines. The method body is moved verbatim; the
// shared JSON-append helpers live in http_server_internal.h.
//
// Called on the game thread only (PluginData is not thread-safe). Uses direct
// string building instead of nlohmann::json to avoid per-frame heap allocations
// from json objects — this runs every time standings change, so it must be fast.
// ============================================================================

#include "http_server.h"
#include "http_server_internal.h"
#include "plugin_data.h"
#include "plugin_constants.h"
#include "plugin_utils.h"
#include "color_config.h"
#include "font_config.h"
#include "tracked_riders_manager.h"
#include "director_manager.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <string>
#include <vector>

using namespace PluginConstants;
using namespace http_server_detail;

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

    // Determine session mode once, used by session and standings sections.
    // Uses the canonical (game-agnostic) race-session check from PluginData;
    // see Game::Adapter::toCanonicalSession() for the per-game mapping.
    bool isRaceSession = pd.isRaceSession();

    // Compact time format mirrored from the in-game HUD so the overlay tracks it
    // ("configure once in-game"). Safe to read here without locking: this snapshot is
    // built on the game thread — the same thread that mutates the setting. The ± column's
    // reference is NOT inherited: the overlay picks it client-side from the per-reference
    // deltas emitted per rider below, so it works even when the in-game column is off.
    bool compactTimes = pd.isShortTimeFormat();

    out += "{";

    // --- Broadcaster panel-force command (edge-triggered on the client by seq) ---
    out += "\"overlayCmd\":{\"panel\":";
    appendJsonString(out, overlayPanelName(m_forcedPanel.load()));
    out += ",\"seq\":";
    appendJsonInt(out, static_cast<int>(m_forcedSeq.load()));
    out += "},";

    // --- Director advisory: what the auto-director is currently doing, so the overlay
    // can highlight the followed rider / battle pair to match the broadcast feed.
    // subject/with are -1 unless actively directing (suppressed while paused, on a
    // manual camera, held, or disabled) so the overlay never marks a stale rider. ---
    {
        DirectorManager& dir = DirectorManager::getInstance();
        bool active = dir.isActivelyDirecting();
        out += "\"director\":{\"on\":";
        out += dir.isEnabled() ? "true" : "false";
        out += ",\"active\":";
        out += active ? "true" : "false";
        out += ",\"subject\":";
        appendJsonInt(out, active ? dir.getCurrentSubject() : -1);
        out += ",\"with\":";
        appendJsonInt(out, active ? dir.getCurrentPartner() : -1);
        out += ",\"shot\":";
        appendJsonString(out, dir.getCurrentShotType());
        out += ",\"paceSplit\":";
        appendJsonInt(out, active ? dir.getCurrentPaceSplit() : -1);
        out += ",\"gained\":";
        appendJsonInt(out, active ? dir.getCurrentOvertakeGained() : -1);
        out += ",\"lost\":";
        appendJsonInt(out, active ? dir.getCurrentDropLost() : -1);
        out += ",\"camera\":";
        appendJsonString(out, DirectorManager::cameraRoleName(dir.getCurrentCameraRole()));
        out += "},";
    }

    // --- Battles: the single battle definition (PluginData::getBattleGroups), driven
    // by the Director's battle-gap / max-position settings, so the in-game director and
    // the overlay's battle panel agree. Emitted as groups of race numbers (front-first);
    // the overlay hydrates them from standings[] and renders the panel. ---
    {
        DirectorManager& dir = DirectorManager::getInstance();
        auto groups = pd.getBattleGroups(dir.getBattleGapMs(), dir.getBattleMaxPos());
        out += "\"battles\":[";
        for (size_t gi = 0; gi < groups.size(); ++gi) {
            if (gi) out += ",";
            out += "[";
            for (size_t ri = 0; ri < groups[gi].size(); ++ri) {
                if (ri) out += ",";
                appendJsonInt(out, groups[gi][ri]);
            }
            out += "]";
        }
        out += "],";
    }

    // --- Best sectors: per sector, a ranked list of the fastest riders (by each rider's
    // best time in that sector, from IdealLapData). "Who's fast where" content for the
    // overlay's best-sectors carousel, which pages one sector at a time. Emitted in ALL
    // session types so a caster can force the board on the hotkey at any time; the client
    // only *auto-shows* it in non-race sessions (in a race the bottom slot auto-belongs to
    // position battles), but a manual force bypasses that.
    // Shape: [{s, riders:[{num, ms}, ...]}]; the client hydrates riders from standings[]
    // by num. Sector 4 only appears on 4-sector games (GP Bikes). ---
    {
        out += "\"sectors\":[";
        {
            constexpr int kTopN = 8;   // ranked riders shown per sector
            std::vector<std::pair<int,int>> bySec[4];  // (ms, raceNum) per sector
            for (const auto& kv : standings) {
                const IdealLapData* il = pd.getIdealLapData(kv.second.raceNum);
                if (!il) continue;
                const int sec[4] = { il->bestSector1, il->bestSector2, il->bestSector3, il->bestSector4 };
                for (int i = 0; i < 4; ++i) {
                    if (sec[i] > 0) bySec[i].push_back({ sec[i], kv.second.raceNum });
                }
            }
            bool firstSec = true;
            for (int i = 0; i < 4; ++i) {
                if (bySec[i].empty()) continue;
                std::sort(bySec[i].begin(), bySec[i].end());  // ascending by ms (fastest first)
                if (!firstSec) out += ",";
                firstSec = false;
                out += "{\"s\":";
                appendJsonInt(out, i + 1);
                out += ",\"riders\":[";
                int n = 0;
                for (const auto& r : bySec[i]) {
                    if (n >= kTopN) break;
                    if (n) out += ",";
                    out += "{\"num\":";
                    appendJsonInt(out, r.second);
                    out += ",\"ms\":";
                    appendJsonInt(out, r.first);
                    out += "}";
                    ++n;
                }
                out += "]}";
            }
        }
        out += "],";
    }

    // --- Per-rider lap series: the raw data the overlay's session-charts carousel
    // derives all four charts from (lap chart / race trace / gap / pace), mirroring
    // the in-game SessionChartsHud (session_charts_math.h) which reads the same
    // PluginData lap log. Shape: [{num, t:[ms,...], v:[1/0,...]?}] in classification
    // order, oldest-first, completed positive laps only. `v` (per-lap validity) is
    // omitted when every lap is valid (the common case) — the client defaults to
    // all-valid. Riders with no completed lap are skipped. Kept raw (no derivation)
    // so the plugin stays lean and the derivation/theming lives client-side, like
    // the sectors board. ---
    {
        out += "\"laps\":[";
        bool firstLapRider = true;
        for (int raceNum : classificationOrder) {
            const std::deque<LapLogEntry>* log = pd.getLapLog(raceNum);
            if (!log) continue;
            // Deque is newest-first; walk it oldest-first, keeping completed positive
            // laps (invalid laps included — their time still elapsed, so cumulative /
            // position / gap must count them; validity is recorded in parallel so the
            // client's pace/best-lap views can exclude them). Matches collectField().
            std::vector<int> t;
            std::vector<char> v;
            bool anyInvalid = false;
            t.reserve(log->size());
            v.reserve(log->size());
            for (auto it = log->rbegin(); it != log->rend(); ++it) {
                if (it->isComplete && it->lapTime > 0) {
                    t.push_back(it->lapTime);
                    v.push_back(it->isValid ? 1 : 0);
                    if (!it->isValid) anyInvalid = true;
                }
            }
            if (t.empty()) continue;
            if (!firstLapRider) out += ',';
            firstLapRider = false;
            out += "{\"num\":";
            appendJsonInt(out, raceNum);
            out += ",\"t\":[";
            for (size_t i = 0; i < t.size(); ++i) {
                if (i) out += ',';
                appendJsonInt(out, t[i]);
            }
            out += "]";
            if (anyInvalid) {
                out += ",\"v\":[";
                for (size_t i = 0; i < v.size(); ++i) {
                    if (i) out += ',';
                    out += v[i] ? '1' : '0';
                }
                out += "]";
            }
            out += "}";
        }
        out += "],";
    }

    out += "\"session\":{";

    // --- Session info ---
    {
        // Session clock: MM:SS countdown, or the time+lap overtime label
        // ("N TO GO" / "FINAL LAP" / "CHECKERED") so the web header matches the
        // in-game StandingsHud / TimeWidget. The overlay renders this string
        // directly, so the label needs no client-side logic.
        int sessionTime = pd.getSessionTime();
        char timeBuf[16];
        PluginUtils::formatSessionClock(pd.getLeaderLapsToGo(), sessionTime, timeBuf, sizeof(timeBuf));
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

        // Session format string ("8:00 + 6L" / "6L" / "8:00") - shared helper, so
        // the web header reads identically to in-game / Discord / Steam.
        char fmtBuf[32];
        PluginUtils::formatSessionFormat(session.sessionLength, session.sessionNumLaps, fmtBuf, sizeof(fmtBuf));
        out += ",\"format\":";
        appendJsonString(out, fmtBuf);

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
            // Small labels (default Tiny5-Regular) — the session-charts SVG axis labels
            // and #num line tags use this, matching the in-game charts HUD's SMALL font.
            out += ','; appendFont("small", FontCategory::SMALL);
        }
        out += '}';

        // Compact time format mirrored from the in-game HUD (see top of buildJsonSnapshot).
        // The overlay applies this instead of its own control, so users configure once.
        out += ",\"compactTimes\":";
        out += compactTimes ? "true" : "false";
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

            // Tracked-rider plate color as CSS hex (emitted only when the rider is
            // tracked). Lets the overlay tint the number badge to match the in-game
            // plate — e.g. a red points-leader plate.
            const TrackedRiderConfig* trackedConfig =
                TrackedRidersManager::getInstance().getTrackedRider(entryIt->second.name);
            if (trackedConfig && trackedConfig->color != 0) {
                unsigned long pc = trackedConfig->color;
                char plateBuf[8];
                snprintf(plateBuf, sizeof(plateBuf), "#%02x%02x%02x",
                    pc & 0xFF, (pc >> 8) & 0xFF, (pc >> 16) & 0xFF);
                out += ",\"plateColor\":";
                appendJsonString(out, plateBuf);
            }

            // Positions gained/lost vs each reference, so the overlay can show whichever it
            // likes (race start / last S/F / last split) entirely client-side, independent
            // of the in-game column's on/off and mode. Each field is omitted when its
            // reference doesn't exist yet (non-race, or before the rider's first lap/split).
            // "Start" falls back to the last-S/F reference for mid-race joiners who never
            // saw the grid. All use official positions (getPositionForRaceNum) for a stable
            // delta — deliberately NOT the local `position` counter above, which diverges
            // when riders are skipped via `continue`.
            int curPos = pd.getPositionForRaceNum(raceNum);
            if (curPos > 0) {
                int startRef = pd.getRaceStartPosition(raceNum);
                if (startRef <= 0) startRef = pd.getSfReferencePosition(raceNum);
                if (startRef > 0) {
                    out += ",\"posDeltaStart\":";
                    appendJsonInt(out, startRef - curPos);
                }
                int sfRef = pd.getSfReferencePosition(raceNum);
                if (sfRef > 0) {
                    out += ",\"posDeltaSf\":";
                    appendJsonInt(out, sfRef - curPos);
                }
                int splitRef = pd.getSplitReferencePosition(raceNum);
                if (splitRef > 0) {
                    out += ",\"posDeltaSplit\":";
                    appendJsonInt(out, splitRef - curPos);
                }
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

                // Live (real-time) gap: leader-relative ms (0 for the leader), plus
                // whether that value is trustworthy right NOW. Validity = it's the
                // leader (its 0 is valid data), OR the rider is in the current
                // ~10-closest track-position batch with a computed same-lap gap and
                // isn't lapped/finished. A rider that dropped out of the batch has a
                // stale realTimeGap, so liveGapValid is false and the client falls
                // back to the official split. Race sessions only. (This is a pure
                // DATA-validity flag — deliberately includes the leader, unlike the
                // in-game per-row display predicate which shows the leader as
                // "Leader"; the two answer different questions.)
                out += ",\"liveGapMs\":";
                appendJsonInt(out, s.realTimeGap);
                bool liveGapValid = isRaceSession &&
                    (position == 1 ||
                     (pd.hasActiveTrackPos(s.raceNum) && s.realTimeGap > 0 && s.gapLaps == 0 &&
                      !pd.getSessionData().isRiderFinished(s.numLaps, s.numLapsAtLeaderFinish)));
                out += ",\"liveGapValid\":";
                out += liveGapValid ? "true" : "false";

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

                // Ideal lap (sum of best individual sectors) for the battle/focus cards.
                // Only emitted once every sector has a time (getIdealLapTime() returns -1
                // otherwise), so the overlay shows a placeholder until it's real.
                if (idealLap) {
                    int idealMs = idealLap->getIdealLapTime();
                    if (idealMs > 0) {
                        char idealBuf[16];
                        PluginUtils::formatLapTime(idealMs, idealBuf, sizeof(idealBuf));
                        out += ",\"idealLap\":";
                        appendJsonString(out, idealBuf);
                        out += ",\"idealLapMs\":";
                        appendJsonInt(out, idealMs);
                    }
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

            // Monotonic epoch-ms key for chronological sorting on the client. clockTime
            // (HH:MM:SS) sorts lexically and inverts across midnight; clockMs doesn't.
            out += ",\"clockMs\":";
            appendJsonInt64(out, std::chrono::duration_cast<std::chrono::milliseconds>(
                entry.systemTime.time_since_epoch()).count());

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
