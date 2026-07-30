// ============================================================================
// core/http_server_snapshot.cpp
// HttpServer::buildJsonSnapshot() — builds the JSON state snapshot streamed to
// web overlays (OBS, etc.). Split out of http_server.cpp (which keeps server
// lifecycle, threading, routing, onDataChanged and the overlay-force command)
// when that file grew past ~1.2k lines. The shared JSON-append helpers live in
// http_server_internal.h.
//
// STRUCTURE. buildJsonSnapshot() gathers the shared reads once into a SnapshotCtx
// and then calls one appendX() per top-level key, in emission order:
//
//     overlayCmd (inline — the only part that touches HttpServer members)
//     director · battles · bestSectors · laps · session · standings · events
//
// EACH HELPER OWNS EXACTLY ITS OWN KEY: it emits its own `"name":`, its own
// opening and closing bracket, and its own separating comma — and emits it
// unconditionally, on every path. That self-containment is the property worth
// protecting, because it is what makes a helper independently readable and
// independently editable. (It did not hold when these were first split: each
// helper trailed off by opening the NEXT one's key, so `appendSession` emitted a
// session object without the word "session" in it, and an early return added to
// `appendLapSeries` would have silently truncated a different key. Nothing
// crashed — it just made the pieces un-reviewable one at a time.)
//
// So: a helper may be edited freely as long as it still emits its whole key.
// Reordering the CALLS changes the key order in the JSON — harmless to a parser,
// but it changes the bytes, so the mirrored-helper parity fixtures care.
//
// Called on the game thread only (PluginData is not thread-safe). Uses direct
// string building instead of nlohmann::json to avoid per-frame heap allocations
// from json objects — this runs every time standings change, so it must be fast.
// The helpers are free functions taking `out` by reference for the same reason:
// nothing here returns a string that would then be copied into the buffer.
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

namespace {

// The shared reads one snapshot pass makes, gathered once at the top of
// buildJsonSnapshot() and handed to each section below.
//
// It exists so the sections can be free functions instead of a single 600-line
// method, WITHOUT each one re-fetching from the singleton: several of these
// accessors are not trivial (getDisplayClassificationOrder() can rebuild a cached
// order), and this whole pass runs on the game thread, so re-fetching per section
// would be real cost for no benefit. Const refs throughout — the snapshot is a
// pure read of PluginData, and nothing here may mutate it.
struct SnapshotCtx {
    const PluginData& pd;
    const SessionData& session;
    const std::vector<int>& classificationOrder;
    const std::unordered_map<int, RaceEntryData>& raceEntries;
    const std::unordered_map<int, StandingsData>& standings;
    int displayRaceNum;
    bool isRaceSession;
    bool compactTimes;
};

// Director advisory section of the snapshot. See buildJsonSnapshot().
void appendDirector(std::string& out, const SnapshotCtx& /*ctx*/) {
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
}

// Battles section of the snapshot. See buildJsonSnapshot().
void appendBattles(std::string& out, const SnapshotCtx& ctx) {
    // --- Battles: the single battle definition (PluginData::getBattleGroups), driven
    // by the Director's battle-gap / max-position settings, so the in-game director and
    // the overlay's battle panel agree. Emitted as groups of race numbers (front-first);
    // the overlay hydrates them from standings[] and renders the panel. ---
    {
        DirectorManager& dir = DirectorManager::getInstance();
        auto groups = ctx.pd.getBattleGroups(dir.getBattleGapMs(), dir.getBattleMaxPos());
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
}

// Best sectors section of the snapshot. See buildJsonSnapshot().
void appendBestSectors(std::string& out, const SnapshotCtx& ctx) {
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
            for (const auto& kv : ctx.standings) {
                const IdealLapData* il = ctx.pd.getIdealLapData(kv.second.raceNum);
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
}

// Per-rider lap series section of the snapshot. See buildJsonSnapshot().
void appendLapSeries(std::string& out, const SnapshotCtx& ctx) {
    // --- Per-rider lap series: the raw data the overlay's session-charts carousel
    // derives all four charts from (lap chart / race trace / gap / pace), mirroring
    // the in-game SessionChartsHud (session_charts_math.h) which reads the same
    // PluginData lap log. Shape: [{num, t:[ms,...], v:[1/0,...]?, s:[ms,...]?}] in
    // classification order, oldest-first, completed positive laps only. `v` (per-lap
    // validity) is omitted when every lap is valid (the common case) — the client defaults
    // to all-valid. `s` is per-SECTOR times, flattened with a fixed stride of the game's
    // sector count, and unlike `t` it also carries the sectors of the lap IN PROGRESS, so
    // it can be longer than t.length*sectors; omitted entirely when the track reports no
    // usable splits. Riders with no completed lap are skipped. Kept raw (no derivation) so
    // the plugin stays lean and the derivation/theming lives client-side, like the sectors
    // board. ---
    {
        // The stride of every laps[].s array. The client MUST NOT infer it from the data:
        // `s` also carries the lap in progress, so a rider sits at 3L+k entries against L
        // completed laps, and floor((3L+k)/L) is 4 whenever k >= L — true for the whole of
        // lap 2 in every race, and for any sufficiently lapped rider after that. A wrong
        // stride silently disables sector points (the hole check then fails for everyone)
        // and flickers frame to frame as riders cross splits. The plugin knows the answer
        // at compile time, so it says so.
        // NOT "sectors" — that key is already the best-sectors BOARD (an array, emitted
        // above). A second top-level "sectors" made the later key win when parsed, silently
        // destroying the board; sectors_test caught it.
        out += "\"sectorCount\":";
        appendJsonInt(out, GAME_SECTOR_COUNT);
        out += ",\"laps\":[";
        bool firstLapRider = true;
        for (int raceNum : ctx.classificationOrder) {
            const std::deque<LapLogEntry>* log = ctx.pd.getLapLog(raceNum);
            if (!log) continue;
            // Deque is newest-first; walk it oldest-first, keeping completed positive
            // laps (invalid laps included — their time still elapsed, so cumulative /
            // position / gap must count them; validity is recorded in parallel so the
            // client's pace/best-lap views can exclude them). Matches collectField().
            std::vector<int> t;
            std::vector<char> v;
            // Per-SECTOR times, flattened oldest-first with a fixed GAME_SECTOR_COUNT
            // stride, so the overlay can draw the same charts at sector resolution the
            // in-game HUD does (ELEM_SECTOR_POINTS). Raw, like `t` — the client decides
            // whether to use it (CONFIG.chartSectorPoints), per the raw-data/presentation
            // split. Costs ~3x the numbers of `t`; emitted only when a rider actually has
            // sector times, so a track with no working splits pays nothing.
            std::vector<int> s;
            bool anyInvalid = false;
            bool anySector = false;
            t.reserve(log->size());
            v.reserve(log->size());
            s.reserve(log->size() * GAME_SECTOR_COUNT + GAME_SECTOR_COUNT);
            for (auto it = log->rbegin(); it != log->rend(); ++it) {
                if (it->isComplete && it->lapTime > 0) {
                    t.push_back(it->lapTime);
                    v.push_back(it->isValid ? 1 : 0);
                    if (!it->isValid) anyInvalid = true;
                    s.push_back(it->sector1);
                    s.push_back(it->sector2);
                    s.push_back(it->sector3);
#if GAME_SECTOR_COUNT >= 4
                    s.push_back(it->sector4);
#endif
                    if (it->sector1 > 0) anySector = true;
                }
            }
            if (t.empty()) continue;
            // The LIVE leading edge: sectors of the lap in progress, which the lap log will
            // not hold until the rider crosses start/finish. RaceSplit fires for every
            // rider, so this is real data for the whole field — it is what lets the
            // overlay's line advance mid-lap instead of once a lap. Appended positionally:
            // completing a lap CLEARS CurrentLapData (setCurrentLapNumber), so it always
            // describes the lap after the last logged one. Mirrors
            // SessionChartsHud::collectSectorTimes.
            if (const CurrentLapData* cur = ctx.pd.getCurrentLapData(raceNum)) {
                int prev = 0;
                const int splits[3] = { cur->split1, cur->split2, cur->split3 };
                for (int k = 0; k < GAME_SECTOR_COUNT - 1; ++k) {
                    if (splits[k] <= prev) break;   // not crossed yet
                    s.push_back(splits[k] - prev);
                    prev = splits[k];
                }
            }
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
            if (anySector) {
                out += ",\"s\":[";
                for (size_t i = 0; i < s.size(); ++i) {
                    if (i) out += ',';
                    appendJsonInt(out, s[i]);
                }
                out += "]";
            }
            out += "}";
        }
        out += "],";
    }
}

// Session info section of the snapshot. See buildJsonSnapshot().
void appendSession(std::string& out, const SnapshotCtx& ctx) {
    // --- Session info ---
    out += "\"session\":{";
    {
        // Session clock: MM:SS countdown, or the time+lap overtime label
        // ("N TO GO" / "FINAL LAP" / "CHECKERED") so the web header matches the
        // in-game StandingsHud / TimeWidget. The overlay renders this string
        // directly, so the label needs no client-side logic.
        int sessionTime = ctx.pd.getSessionTime();
        char timeBuf[16];
        PluginUtils::formatSessionClock(ctx.pd.getLeaderLapsToGo(), sessionTime, timeBuf, sizeof(timeBuf));
        out += "\"time\":";
        appendJsonString(out, timeBuf);

        out += ",\"timeMs\":";
        appendJsonInt(out, sessionTime);

        // Session type
        const char* sessionStr = PluginUtils::getSessionString(ctx.session.eventType, ctx.session.session);
        out += ",\"type\":";
        appendJsonString(out, sessionStr ? sessionStr : "");

        // Session state
        const char* stateStr = PluginUtils::getSessionStateString(ctx.session.sessionState);
        out += ",\"state\":";
        appendJsonString(out, stateStr ? stateStr : "");

        out += ",\"numLaps\":";
        appendJsonInt(out, ctx.session.sessionNumLaps);

        out += ",\"sessionLength\":";
        appendJsonInt(out, ctx.session.sessionLength);

        // Session format string ("8:00 + 6L" / "6L" / "8:00") - shared helper, so
        // the web header reads identically to in-game / Discord / Steam.
        char fmtBuf[32];
        PluginUtils::formatSessionFormat(ctx.session.sessionLength, ctx.session.sessionNumLaps, fmtBuf, sizeof(fmtBuf));
        out += ",\"format\":";
        appendJsonString(out, fmtBuf);

        out += ",\"isRace\":";
        out += ctx.isRaceSession ? "true" : "false";

        // Track info
        out += ",\"trackName\":";
        appendJsonString(out, ctx.session.trackName);

        out += ",\"trackLength\":";
        appendJsonFloat(out, ctx.session.trackLength);

        // Leader lap
        int leaderLap = 0;
        if (!ctx.classificationOrder.empty()) {
            auto it = ctx.standings.find(ctx.classificationOrder[0]);
            if (it != ctx.standings.end()) {
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
        out += (ctx.pd.getDrawState() >= 1) ? "true" : "false";

        // Color palette from in-game settings (ABGR → CSS hex)
        const ColorConfig& colors = ColorConfig::getInstance();
        out += ",\"palette\":{";
        {
            auto appendColor = [&](const char* name, unsigned long abgr) {
                char hex[8];
                // Cast to unsigned: %x's argument type is `unsigned int`, and the
                // masked channels are `unsigned long`. Same width on the LLP64
                // Windows target, but the cast makes it correct by construction
                // rather than by platform coincidence (each value is 0..255).
                snprintf(hex, sizeof(hex), "#%02x%02x%02x",
                    static_cast<unsigned>(abgr & 0xFF),
                    static_cast<unsigned>((abgr >> 8) & 0xFF),
                    static_cast<unsigned>((abgr >> 16) & 0xFF));
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
        out += ctx.compactTimes ? "true" : "false";
    }

    out += "}";
}

// Standings section of the snapshot. See buildJsonSnapshot().
void appendStandings(std::string& out, const SnapshotCtx& ctx) {
    // --- Standings ---
    out += ",\"standings\":[";
    {
        const LapLogEntry* overallBest = ctx.pd.getOverallBestLap();
        bool firstRider = true;
        int position = 1;

        for (int raceNum : ctx.classificationOrder) {
            auto entryIt = ctx.raceEntries.find(raceNum);
            auto standingIt = ctx.standings.find(raceNum);
            if (entryIt == ctx.raceEntries.end()) {
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
                    static_cast<unsigned>(bc & 0xFF),
                    static_cast<unsigned>((bc >> 8) & 0xFF),
                    static_cast<unsigned>((bc >> 16) & 0xFF));
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
                    static_cast<unsigned>(pc & 0xFF),
                    static_cast<unsigned>((pc >> 8) & 0xFF),
                    static_cast<unsigned>((pc >> 16) & 0xFF));
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
            int curPos = ctx.pd.getPositionForRaceNum(raceNum);
            if (curPos > 0) {
                int startRef = ctx.pd.getRaceStartPosition(raceNum);
                if (startRef <= 0) startRef = ctx.pd.getSfReferencePosition(raceNum);
                if (startRef > 0) {
                    out += ",\"posDeltaStart\":";
                    appendJsonInt(out, startRef - curPos);
                }
                int sfRef = ctx.pd.getSfReferencePosition(raceNum);
                if (sfRef > 0) {
                    out += ",\"posDeltaSf\":";
                    appendJsonInt(out, sfRef - curPos);
                }
                int splitRef = ctx.pd.getSplitReferencePosition(raceNum);
                if (splitRef > 0) {
                    out += ",\"posDeltaSplit\":";
                    appendJsonInt(out, splitRef - curPos);
                }
            }

            if (standingIt != ctx.standings.end()) {
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
                } else if (ctx.isRaceSession) {
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
                bool liveGapValid = ctx.isRaceSession &&
                    (position == 1 ||
                     (ctx.pd.hasActiveTrackPos(s.raceNum) && s.realTimeGap > 0 && s.gapLaps == 0 &&
                      !ctx.pd.getSessionData().isRiderFinished(s.numLaps, s.numLapsAtLeaderFinish)));
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
                const IdealLapData* idealLap = ctx.pd.getIdealLapData(raceNum);
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
                bool isFinished = ctx.session.isRiderFinished(s.numLaps, s.numLapsAtLeaderFinish);
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
                    if (raceNum == ctx.displayRaceNum) addChip("camera");
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

    out += "]";
}

// Event log section of the snapshot. See buildJsonSnapshot().
void appendEventLog(std::string& out, const SnapshotCtx& ctx) {
    // --- Event Log ---
    out += ",\"events\":[";
    // Send all events — the web UI filters client-side.
    // Cap serialized events to avoid expensive serialization during long sessions.
    static constexpr size_t MAX_SERIALIZED_EVENTS = 50;
    {
        const auto& eventLog = ctx.pd.getEventLog();
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

    out += "]";
}

}  // namespace

std::string HttpServer::buildJsonSnapshot() const {
    const PluginData& pd = PluginData::getInstance();
    const SessionData& session = pd.getSessionData();
    const auto& classificationOrder = pd.getDisplayClassificationOrder();
    const auto& raceEntries = pd.getRaceEntries();
    const auto& standings = pd.getStandings();
    int displayRaceNum = pd.getDisplayRaceNum();

    // Pre-allocate ~48KB. A full grid with per-rider standings fields + the laps[]
    // series + sectors + up to 50 events realistically reaches ~30-40KB, so 16KB
    // forced 1-2 whole-buffer reallocations (16->32->64KB) on every build; this
    // clears it. Built on the game thread, so those reallocs were game-thread cost.
    std::string out;
    out.reserve(49152);

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

    const SnapshotCtx ctx{ pd, session, classificationOrder, raceEntries, standings,
                           displayRaceNum, isRaceSession, compactTimes };

    out += "{";

    // --- Broadcaster panel-force command (edge-triggered on the client by seq) ---
    out += "\"overlayCmd\":{\"panel\":";
    appendJsonString(out, overlayPanelName(m_forcedPanel.load()));
    out += ",\"seq\":";
    appendJsonInt(out, static_cast<int>(m_forcedSeq.load()));
    out += "},";

    appendDirector(out, ctx);

    appendBattles(out, ctx);

    appendBestSectors(out, ctx);

    appendLapSeries(out, ctx);

    appendSession(out, ctx);

    appendStandings(out, ctx);

    appendEventLog(out, ctx);

    out += "}";
    return out;
}

