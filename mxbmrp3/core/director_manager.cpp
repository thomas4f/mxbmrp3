// ============================================================================
// core/director_manager.cpp
// ============================================================================
#include "director_manager.h"
#include "director_manager_internal.h"

#include "plugin_data.h"
#include "plugin_constants.h"
#include "xinput_reader.h"
#include "event_log_types.h"
#include "color_config.h"  // ColorSlot, for tinting state-transition event-log entries
#include "../handlers/spectate_handler.h"
#include "../diagnostics/logger.h"

#include <vector>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <climits>
#include <cstdio>
#include <cstring>

using namespace director_detail;

DirectorManager& DirectorManager::getInstance() {
    static DirectorManager instance;
    return instance;
}

#if defined(MXBMRP3_TEST_BUILD)
// Injectable simulated clock for headless broadcast-measurement tests. -1 = use the
// real steady_clock (production path). The test replay sets this from each tape event's
// timestamp so the director's wall-clock pacing (min/max shot, holds) plays out at the
// recorded cadence instead of collapsing into the ~milliseconds a naive replay takes.
// Never compiled into a shipping DLL.
static long long s_dirTestNowMs = -1;
void DirectorManager::testSetNowMs(long long ms) { s_dirTestNowMs = ms; }
#endif

long long DirectorManager::nowMs() const {
#if defined(MXBMRP3_TEST_BUILD)
    if (s_dirTestNowMs >= 0) return s_dirTestNowMs;
#endif
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

void DirectorManager::resetRuntime() {
    // Full clean slate - shared by the enable toggle and a new-session transition so a
    // stale baseline can't leak across (e.g. m_prevBestOverallMs firing a bogus
    // fastest-lap cut on lap 1 of a new track). The other caches self-heal via the
    // per-eval refresh, but resetting them here is harmless and keeps it uniform.
    long long now = nowMs();
    m_currentSubject = -1;
    m_currentPartner = -1;
    m_currentShotType = SHOT_SOLO;
    m_currentCameraRole = -1;
    m_shotStartMs = now;
    m_lastDecisionMs = 0;
    m_wasPaused = true;
    m_pendingUntilMs = 0;
    m_manualOverrideUntilMs = 0;
    m_manualSinceMs = 0;
    m_lastInputActivityMs = 0;
    m_riderLock = false;
    m_shotCount = 0;
    m_lastAirtimeNum = -1;
    m_onboardRotation = 0;
    m_prevCrashCount.clear();
    m_holdShotType = SHOT_SOLO;
    m_holdStartMs = 0;
    m_holdUntilMs = 0;
    m_prevBestOverallMs = -1;
    m_hazardLatchSubject = -1;
    m_takeoverActive = false;
    m_reclaiming = false;
    // Clear the spectate handler's cached manual-camera flag so a new session (or a
    // re-enable) re-resolves it fresh rather than inheriting a stale value.
    SpectateHandler::getInstance().resetCameraTracking();
    m_lastManualPollMs = 0;
    m_prevPosition.clear();
    m_overtakeSubject = -1;
    m_overtakePartner = -1;
    m_overtakeGained = 1;
    m_overtakeUntilMs = 0;
    m_dropBasePos.clear();
    m_dropWindowStartMs = 0;
    m_dropSubject = -1;
    m_dropLost = 0;
    m_dropUntilMs = 0;
    m_lapperSubject = -1;
    m_finishedWinnerNum = -1;
    m_winnerHoldUntilMs = 0;
    m_bestSectorMs[0] = -1;
    m_bestSectorMs[1] = -1;
    m_bestSectorMs[2] = -1;
    m_currentPaceSplit = -1;
    m_lastCutKey[0] = '\0';
}

void DirectorManager::setEnabled(bool enabled) {
    if (m_enabled == enabled) return;
    m_enabled = enabled;
    resetRuntime();
    // Seed the session token to the current generation so the next SessionData
    // notification (the 1 Hz clock heartbeat shares that type) doesn't re-reset.
    m_lastSessionGen = PluginData::getInstance().getSessionData().sessionGeneration;
    DEBUG_INFO_F("Director: %s", enabled ? "enabled" : "disabled");
    // Tint state-transition entries with the director button's state colors (see
    // director_widget.cpp stateColor()): auto-on == POSITIVE (running), off == MUTED.
    if (enabled) logDirectorEvent("Auto-director enabled",  static_cast<int>(ColorSlot::POSITIVE));
    else         logDirectorEvent("Auto-director disabled", static_cast<int>(ColorSlot::MUTED));
}

void DirectorManager::setMinShotSec(int sec) {
    m_minShotSec = std::clamp(sec, MIN_SHOT_LO, MIN_SHOT_HI);
    if (m_maxShotSec < m_minShotSec) m_maxShotSec = m_minShotSec;  // keep max >= min
}

void DirectorManager::setMaxShotSec(int sec) {
    m_maxShotSec = std::clamp(sec, MAX_SHOT_LO, MAX_SHOT_HI);
    if (m_minShotSec > m_maxShotSec) m_minShotSec = m_maxShotSec;  // keep min <= max
}

void DirectorManager::setVarietyEvery(int n) {
    // 0 = Off (never dip to a variety onboard - shots stay trackside); otherwise
    // clamp to the meaningful cadence range. The dip check in pickShot() already
    // treats 0 as off (m_varietyEvery > 0).
    m_varietyEvery = (n <= 0) ? 0 : std::clamp(n, VARIETY_LO, VARIETY_HI);
}

void DirectorManager::setHoldSec(int sec) {
    m_holdSec = std::clamp(sec, HOLD_LO, HOLD_HI);
    if (m_incidentMaxSec < m_holdSec) m_incidentMaxSec = m_holdSec;  // cap >= hold
}

void DirectorManager::setIncidentMaxSec(int sec) {
    m_incidentMaxSec = std::clamp(sec, INC_MAX_LO, INC_MAX_HI);
    if (m_incidentMaxSec < m_holdSec) m_incidentMaxSec = m_holdSec;  // cap >= hold
}

void DirectorManager::setBattleGapMs(int ms) {
    m_battleGapMs = std::clamp(ms, BATTLE_GAP_LO, BATTLE_GAP_HI);
}

void DirectorManager::setBattleMaxPos(int pos) {
    m_battleMaxPos = std::clamp(pos, 0, BATTLE_MAXPOS_HI);  // 0 = no limit
}

void DirectorManager::setManualResumeSec(int sec) {
    m_manualResumeSec = std::clamp(sec, 0, RESUME_HI);
}

void DirectorManager::toggleLock() {
    m_riderLock = !m_riderLock;
    DEBUG_INFO_F("Director: rider lock %s", m_riderLock ? "ON" : "OFF");
    // Rider lock == the director button's Paused state == WARNING (matches the standings
    // lock icon); releasing it resumes auto == POSITIVE. See director_widget.cpp stateColor().
    if (m_riderLock && m_currentSubject >= 0) {
        char lab[16], msg[64];
        riderLabel(m_currentSubject, lab, sizeof(lab));
        snprintf(msg, sizeof(msg), "Locked on %s", lab);
        logDirectorEvent(msg, static_cast<int>(ColorSlot::WARNING));
    } else if (m_riderLock) {
        logDirectorEvent("Rider lock on",  static_cast<int>(ColorSlot::WARNING));
    } else {
        logDirectorEvent("Rider lock off", static_cast<int>(ColorSlot::POSITIVE));
    }
}

int DirectorManager::pickShot(bool isBattle, const std::vector<int>& group, int* outTarget) {
    using CR = SpectateHandler::CameraRole;

    // Forward-facing onboards (look AHEAD at the rider in front) - the chaser's-eye view
    // in a battle, and the solo variety set. Direction matters: a forward cam on the front
    // rider points up an empty track, so it's never used to frame a battle.
    CR fwd[3]; int nFwd = 0;
    if (m_camFront)   fwd[nFwd++] = CR::ONBOARD_FRONT;
    if (m_camHelmet)  fwd[nFwd++] = CR::ONBOARD_HELMET;
    if (m_camHelmet2) fwd[nFwd++] = CR::ONBOARD_HELMET2;

    // Every Nth cut dips to an onboard (battle OR solo) so variety shows even in a bunched
    // field where almost every shot is a "battle". Forced cuts don't reach here, so the
    // cadence only counts genuine picks.
    ++m_shotCount;
    const bool dip = (m_varietyEvery > 0 && (m_shotCount % m_varietyEvery) == 0);

    if (isBattle) {
        // Primary battle shot: Trackside (TV) on the front rider, framing the whole fight.
        if (!dip) return static_cast<int>(CR::TRACKSIDE);
        // Variety dip: a direction-correct onboard, rotating through the battle's riders.
        //   front rider (group[0]) -> Rear Fender  (looks back at the chaser)  [defending]
        //   a chaser   (group[i>0]) -> forward cam (looks ahead at the hunt)   [attacking]
        // Build only the options the enabled cams support, then rotate; with none usable
        // we stay Trackside rather than ever pointing an onboard the wrong way.
        struct Opt { int rider; int role; } opts[8]; int nOpt = 0;
        const int groupN = static_cast<int>(group.size());
        if (m_camRear && groupN >= 1) opts[nOpt++] = { group[0], static_cast<int>(CR::REAR) };
        for (int i = 1; i < groupN && nFwd > 0 && nOpt < 8; ++i)
            opts[nOpt++] = { group[i], static_cast<int>(fwd[(i - 1) % nFwd]) };
        if (nOpt == 0) return static_cast<int>(CR::TRACKSIDE);
        const Opt& o = opts[m_onboardRotation % nOpt];
        ++m_onboardRotation;
        *outTarget = o.rider;   // may be a chasing rider, not the front subject
        return o.role;
    }

    // Solo: the full variety pool (any enabled onboard, including the rearward/forks ones),
    // else Auto (the game's own trackside director).
    CR pool[5]; int n = 0;
    if (m_camFront)   pool[n++] = CR::ONBOARD_FRONT;
    if (m_camHelmet)  pool[n++] = CR::ONBOARD_HELMET;
    if (m_camHelmet2) pool[n++] = CR::ONBOARD_HELMET2;
    if (m_camRear)    pool[n++] = CR::REAR;
    if (m_camForks)   pool[n++] = CR::FORKS;
    if (dip && n > 0) {
        CR pick = pool[m_onboardRotation % n];
        ++m_onboardRotation;
        return static_cast<int>(pick);
    }
    return static_cast<int>(CR::AUTO);
}

int DirectorManager::nextLockedCamera(int currentRole) const {
    using CR = SpectateHandler::CameraRole;
    // TV first (AUTO = the game's own trackside framing of the subject), then each enabled
    // onboard. This is the rotation the lock cycles through, one step per shot cadence.
    int pool[6]; int n = 0;
    pool[n++] = static_cast<int>(CR::AUTO);
    if (m_camFront)   pool[n++] = static_cast<int>(CR::ONBOARD_FRONT);
    if (m_camHelmet)  pool[n++] = static_cast<int>(CR::ONBOARD_HELMET);
    if (m_camHelmet2) pool[n++] = static_cast<int>(CR::ONBOARD_HELMET2);
    if (m_camRear)    pool[n++] = static_cast<int>(CR::REAR);
    if (m_camForks)   pool[n++] = static_cast<int>(CR::FORKS);
    if (n <= 1) return currentRole;   // only TV available -> nothing to cycle
    int idx = -1;
    for (int i = 0; i < n; ++i) if (pool[i] == currentRole) { idx = i; break; }
    return pool[(idx + 1) % n];       // idx==-1 (e.g. a battle Trackside / free-roam) -> start at TV
}

const char* DirectorManager::cameraRoleName(int role) {
    using CR = SpectateHandler::CameraRole;
    switch (static_cast<CR>(role)) {
        case CR::AUTO:           return "Auto";
        case CR::TRACKSIDE:      return "Trackside";
        case CR::START:          return "Start";
        case CR::ONBOARD_FRONT:  return "Front Fender";
        case CR::ONBOARD_HELMET: return "Helmet 1";
        case CR::ONBOARD_HELMET2: return "Helmet 2";
        case CR::REAR:           return "Rear Fender";
        case CR::FORKS:          return "Forks";
        case CR::FREE_ROAM:      return "Free-Roam";
    }
    return "-";
}

const char* DirectorManager::getCurrentShotType() const {
    switch (m_currentShotType) {
        case SHOT_BATTLE:   return "battle";
        case SHOT_INCIDENT: return "incident";
        case SHOT_FASTEST:  return "fastest";
        case SHOT_FINISH:   return "finish";
        case SHOT_FINAL_LAP: return "finallap";
        case SHOT_OVERTAKE: return "overtake";
        case SHOT_PACE:     return "pace";
        case SHOT_LAPPER:   return "lapper";
        case SHOT_DROP:     return "drop";
        case SHOT_SOLO:
        default:            return "solo";
    }
}

bool DirectorManager::isActivelyDirecting() const {
    // Following someone, enabled, and not yielding (rider lock or broadcaster manual camera).
    return m_enabled && m_currentSubject >= 0 && !m_riderLock
        && !SpectateHandler::getInstance().isManualCameraActive();
}

void DirectorManager::cutTo(int raceNum, bool isBattle, long long now, int forceRole,
                            int shotType, int partner,
                            const std::vector<int>* group, const char* reason) {
    int target = raceNum;
    int role;
    if (forceRole >= 0) {
        role = forceRole;   // incident/fastest/pace/finish force Trackside; no variety dip
    } else {
        static const std::vector<int> kEmpty;
        role = pickShot(isBattle, group ? *group : kEmpty, &target);
    }
    // A battle dip can redirect the shot to a chasing rider; then the front rider (the
    // original subject) becomes the framed partner, so the badge still reads the contested
    // position (front) regardless of which rider the onboard sits on.
    if (target != raceNum) partner = raceNum;

    m_currentSubject = target;
    m_currentShotType = (shotType >= 0) ? shotType : (isBattle ? SHOT_BATTLE : SHOT_SOLO);
    m_currentPartner = partner;
    m_currentCameraRole = role;
    m_shotStartMs = now;
    m_pendingUntilMs = now + kPendingMs;
    SpectateHandler& spectate = SpectateHandler::getInstance();
    spectate.requestSpectateRider(target);
    spectate.requestSpectateCamera(static_cast<SpectateHandler::CameraRole>(role));
    // Structured, single-line cut record (parseable): sim/real time is prefixed by the
    // logger; here we emit subject, shot kind, camera name, framed partner and the pacing
    // reason. The whole broadcast can be reconstructed from these lines (cut count,
    // per-rider screen time, camera-angle mix, cut-reason mix) - see
    // tests/integration/tests/director_broadcast_test.cpp and tools/director_report.py.
    // FORMAT COUPLING: keep this in step with parseCuts() (the test) and CUT_RE
    // (director_report.py) - reason is appended last so older parsers stay compatible.
    DEBUG_INFO_F("Director cut: t=%lldms #%d shot=%s cam=%s partner=%d reason=%s",
                 now, target, getCurrentShotType(), cameraRoleName(role), partner, reason);

    // Broadcast transparency: surface each shot decision in the event log so a caster can
    // see the director's logic in the feed. Like every other event producer we emit the raw
    // entry UNCONDITIONALLY (the plugin sends raw data; the in-game "Director" event toggle
    // and the web overlay filter it at display time — see http_server.cpp "Send all events").
    // A same-subject/same-shot camera refresh is deduped so it doesn't re-log. Only the
    // message + camera icon are shown — a short human phrase per cut.
    // TRADEOFF (deliberate): director cuts are more frequent than any other producer
    // (>= one per min-shot, default 8 s), so on a long replay they churn the shared 100-entry
    // ring buffer and can evict older race highlights even for a viewer who keeps the Director
    // event type hidden. Accepted for the raw-data contract (the web overlay filters
    // independently) and bounded by the fact that cuts only flow while a broadcaster has
    // explicitly ENABLED the director; the dedup keeps repeats out. Revisit with a dedicated
    // buffer if that eviction ever bites.
    emitCutEvent(target, partner);
}

void DirectorManager::riderLabel(int raceNum, char* buf, size_t n) const {
    const RaceEntryData* e = PluginData::getInstance().getRaceEntry(raceNum);
    if (e && e->formattedRaceNum[0] != '\0') strncpy_s(buf, n, e->formattedRaceNum, _TRUNCATE);
    else                                     snprintf(buf, n, "#%d", raceNum);
}

void DirectorManager::logDirectorEvent(const char* message, int iconColorSlot) {
    PluginData::getInstance().addEventLogEntry(EventLogType::Director, message, nullptr, iconColorSlot);
    // A state change breaks the cut run, so let the next cut log fresh (even if it lands
    // on the same subject/shot as the cut before the state change).
    m_lastCutKey[0] = '\0';
}

void DirectorManager::emitCutEvent(int subject, int partner) {
    char subjLab[16];
    riderLabel(subject, subjLab, sizeof(subjLab));
    char partnerLab[16] = "";
    if (partner >= 0) riderLabel(partner, partnerLab, sizeof(partnerLab));

    char msg[64];
    switch (m_currentShotType) {
        case SHOT_BATTLE:
            if (partner >= 0) snprintf(msg, sizeof(msg), "Battle %s vs %s", subjLab, partnerLab);
            else              snprintf(msg, sizeof(msg), "Battle %s", subjLab);
            break;
        case SHOT_OVERTAKE:
            if (partner >= 0) snprintf(msg, sizeof(msg), "Overtake %s on %s", subjLab, partnerLab);
            else              snprintf(msg, sizeof(msg), "Overtake %s", subjLab);
            break;
        case SHOT_INCIDENT:  snprintf(msg, sizeof(msg), "Incident %s", subjLab); break;
        case SHOT_FASTEST:   snprintf(msg, sizeof(msg), "Fastest lap %s", subjLab); break;
        case SHOT_FINISH:    snprintf(msg, sizeof(msg), "Race winner %s", subjLab); break;
        case SHOT_FINAL_LAP: snprintf(msg, sizeof(msg), "Final lap %s", subjLab); break;
        case SHOT_PACE:      snprintf(msg, sizeof(msg), "Fast sector %s", subjLab); break;
        case SHOT_LAPPER:    snprintf(msg, sizeof(msg), "Lapping %s", subjLab); break;
        case SHOT_DROP:      snprintf(msg, sizeof(msg), "Losing places %s", subjLab); break;
        case SHOT_SOLO:
        default:             snprintf(msg, sizeof(msg), "Following %s", subjLab); break;
    }

    // Dedup on a canonical key (shot + unordered rider pair): a same-shot camera refresh —
    // including a battle variety dip that swaps subject<->partner — collapses to one line.
    int a = subject, b = partner;
    if (b >= 0 && b < a) { int t = a; a = b; b = t; }
    char key[32];
    snprintf(key, sizeof(key), "%d:%d:%d", m_currentShotType, a, b);
    if (strcmp(key, m_lastCutKey) == 0) return;      // same shot/subjects as the previous cut
    strncpy_s(m_lastCutKey, sizeof(m_lastCutKey), key, _TRUNCATE);
    PluginData::getInstance().addEventLogEntry(EventLogType::Director, msg);
}

void DirectorManager::armHold(int shotType, long long now, long long lingerMs) {
    // Pin the just-cut story shot. The held rider is m_currentSubject (cutTo set it).
    m_holdShotType = shotType;
    m_holdStartMs = now;
    m_holdUntilMs = now + lingerMs;
}

bool DirectorManager::holdActive(int shotType, long long now, bool currentPresent) {
    // Only one story holds at a time; a mismatched type means a different story owns the
    // latch (checked at its own priority slot), so leave it untouched.
    if (m_holdShotType != shotType || m_holdUntilMs == 0) return false;
    if (now < m_holdUntilMs && currentPresent) return true;
    m_holdUntilMs = 0;   // lapsed (timer up or held rider gone) -> open the floor
    return false;
}

void DirectorManager::onDataChanged(int changeType) {
    if (!m_enabled) return;
    // A new session/track resets the field: drop all story baselines so a stale one
    // can't leak across (notably the fastest-lap baseline firing a bogus cut on the
    // first lap of the new session). Gate on the session GENERATION, not the raw
    // SessionData notification - that type also carries the 1 Hz session-clock
    // heartbeat (setSessionTime), so resetting on every SessionData would wipe the
    // director's pacing/variety/story state once per second all race long.
    if (changeType == static_cast<int>(DataChangeType::SessionData)) {
        int gen = PluginData::getInstance().getSessionData().sessionGeneration;
        if (gen != m_lastSessionGen) {
            m_lastSessionGen = gen;
            resetRuntime();
        }
        return;
    }
    // Standings drives race direction; IdealLap (live splits + completed-lap sectors,
    // field-wide) drives the non-race timing show - it carries the hot-lap pace signal
    // there are no position changes to ride in practice/qualifying. evaluate() coalesces
    // (~3x/sec) so the extra IdealLap traffic in a race is just a cheap clock check.
    if (changeType != static_cast<int>(DataChangeType::Standings) &&
        changeType != static_cast<int>(DataChangeType::IdealLap)) return;
    evaluate();
}

void DirectorManager::pollManualControl() {
    // Runs every frame (throttled), independent of timing-data callbacks, so the gamepad
    // takeover gesture and the auto-resume work even when evaluate() isn't ticking (quiet
    // lulls, solo sessions, replays). evaluate() still owns the actual shot decisions.
    if (!m_enabled) return;

    PluginData& pd = PluginData::getInstance();
    int ds = pd.getDrawState();
    if (ds != PluginConstants::ViewState::SPECTATE && ds != PluginConstants::ViewState::REPLAY) return;

    long long now = nowMs();
    if (now - m_lastManualPollMs < kManualPollIntervalMs) return;  // ~30 Hz
    m_lastManualPollMs = now;

    SpectateHandler& spectate = SpectateHandler::getInstance();
    // Keyed on the takeover flag too, not just isManualCameraActive(): a gamepad takeover
    // must hold for the full resume time even where the game doesn't report the grabbed
    // camera as "manual" (e.g. offline replays). Without this the pause would collapse the
    // instant the stick is released and snap straight back to auto.
    if (spectate.isManualCameraActive() || m_takeoverActive) {
        // --- Resume: reclaim once the caster has been idle on the controller long enough. ---
        if (m_reclaiming) return;  // reclaim already decided; let the cut land (camera switches back)
        if (m_manualSinceMs == 0) {
            m_manualSinceMs = now; m_lastInputActivityMs = now;
            m_riderLock = false;  // taking manual control releases the rider lock
            // Edge into a menu-driven manual camera (the gamepad-takeover path logs its own
            // message below, and sets m_manualSinceMs first, so it never reaches here).
            logDirectorEvent("Manual camera", static_cast<int>(ColorSlot::NEUTRAL));  // == button Manual state
        }
        // A free-roam grabbed via gamepad takeover always auto-resumes (default timeout
        // when "resume after" is Off) so the caster isn't trapped without opening the
        // menu; a menu-driven manual cam with resume Off stays paused.
        long long resumeMs = (m_manualResumeSec > 0)
            ? static_cast<long long>(m_manualResumeSec) * 1000
            : (m_takeoverActive ? kTakeoverResumeFallbackMs : 0);
        if (resumeMs <= 0) return;
        XInputReader& xi = XInputReader::getInstance();
        xi.update();
        // Only continued camera-flying (sticks/triggers) extends the pause; a button
        // press doesn't. Resume fires `resumeMs` after the caster stops flying.
        if (cameraFlying(xi.getData())) m_lastInputActivityMs = now;
        if (now - m_lastInputActivityMs < resumeMs) return;
        // Idle long enough -> reclaim: clear manual state and force a fresh cut now,
        // bypassing evaluate()'s coalesce gate so it runs even with no data flowing.
        m_takeoverActive = false;
        m_manualSinceMs = 0;
        m_reclaiming = true;
        m_lastDecisionMs = 0;
        logDirectorEvent("Auto-director resumed", static_cast<int>(ColorSlot::POSITIVE));  // == button Running state
        evaluate();
        return;
    }

    // --- Idle (no manual cam, no takeover in progress). Watch for the takeover gesture:
    // a stick push grabs free-roam + pauses. (m_takeoverActive is already false here; it's
    // cleared only when the resume above reclaims.) ---
    m_manualSinceMs = 0;
    if (m_gamepadTakeover) {
        XInputReader& xi = XInputReader::getInstance();
        xi.update();
        if (stickMoved(xi.getData())) {
            // If this track exposes no manual camera, handleSpectateCameras leaves the
            // camera untouched - the director still pauses (m_takeoverActive gates
            // evaluate()) while the stick is held, then resumes on idle.
            spectate.requestSpectateCamera(SpectateHandler::CameraRole::FREE_ROAM);
            m_takeoverActive = true;
            m_manualSinceMs = now;
            m_lastInputActivityMs = now;
            m_riderLock = false;  // taking manual control releases the rider lock
            DEBUG_INFO("Director: gamepad takeover -> Free-Roam (paused)");
            logDirectorEvent("Manual control (gamepad)", static_cast<int>(ColorSlot::NEUTRAL));  // == button Manual state
        }
    }
}

void DirectorManager::pollPacing() {
    // evaluate() gates on spectate/replay + coalescing internally, but assumes its
    // callers have already checked the enable toggle (onDataChanged / pollManualControl
    // both do), so guard it here too. This is the wall-clock pacing tick that keeps the
    // max-shot cap enforced when timing-data callbacks go quiet (see the header).
    if (!m_enabled) return;
    evaluate();
}

