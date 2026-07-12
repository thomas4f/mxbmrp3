// ============================================================================
// core/director_manager.cpp
// ============================================================================
#include "director_manager.h"

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

namespace {
    // Tuning constants (ms). Shot lengths and the battle gap are live-tunable
    // members instead (see the Director tab).
    constexpr long long kDecisionIntervalMs = 300;   // coalesce: decide at most ~3x/sec
    constexpr long long kPendingMs          = 1500;  // grace for a requested cut to land
    constexpr long long kManualGraceMs      = 6000;  // yield after the user takes control
    // The incident-linger / incident-cap / fastest-linger / takeover-resume timings
    // are live-tunable members (see the Director tab), not constants.

    // How long the overtaker keeps a scoring boost after a detected pass.
    constexpr long long kOvertakeWindowMs = 4000;

    // Drop detection: a rider losing >= kDropThreshold places within a rolling
    // kDropWindowMs baseline is "tumbling"; the boost then lasts kOvertakeWindowMs.
    constexpr long long kDropWindowMs = 6000;
    constexpr int       kDropThreshold = 3;

    // After the leader takes the flag, hold on them this long (a winner celebration),
    // then move the finish lock to the battle for the next position - a parked winner
    // isn't a shot.
    constexpr long long kWinnerCelebrationMs = 6000;

    // Trap-guard for gamepad takeover: if the caster grabs Free-Roam while "Resume
    // after" is Off, auto-resume after this much idle anyway (they have no menu to
    // switch back from). Hardcoded - not a second user-facing resume setting.
    constexpr long long kTakeoverResumeFallbackMs = 3000;

    // Throttle for the per-frame pollManualControl(): ~30 Hz is plenty to catch a
    // stick gesture and time the resume, without polling XInput at the full frame rate.
    constexpr long long kManualPollIntervalMs = 33;

    // A deliberate stick push, the gesture that flies a free-roam camera. Used as the
    // gamepad-takeover trigger so a button press (or drift, via the 0.25 deadzone -
    // the stored stick values are raw/un-deadzoned) doesn't grab the camera.
    bool stickMoved(const XInputData& d) {
        if (!d.isConnected) return false;
        constexpr float kStick = 0.25f;
        return std::fabs(d.leftStickX) > kStick || std::fabs(d.leftStickY) > kStick ||
               std::fabs(d.rightStickX) > kStick || std::fabs(d.rightStickY) > kStick;
    }

    // "Still hand-flying the camera": sticks (pan/move) or triggers (height/speed) -
    // the inputs that actually drive a free-roam camera. Deliberately EXCLUDES
    // buttons/dpad/shoulders, so incidental button presses while flying don't keep
    // resetting the resume timer (which made auto-resume feel inconsistent). Used to
    // detect inactivity for resume, not to trigger takeover.
    bool cameraFlying(const XInputData& d) {
        if (!d.isConnected) return false;
        constexpr float kStick = 0.25f, kTrig = 0.1f;
        if (std::fabs(d.leftStickX) > kStick || std::fabs(d.leftStickY) > kStick ||
            std::fabs(d.rightStickX) > kStick || std::fabs(d.rightStickY) > kStick) return true;
        return d.leftTrigger > kTrig || d.rightTrigger > kTrig;
    }

    // Leaders are worth more than midfield; P1 ~1.8x, fading to 1.0 by ~P11.
    double posWeight(int position) {
        int boost = 11 - position;
        if (boost < 0) boost = 0;
        return 1.0 + boost * 0.08;
    }

    struct Rider {
        int position;
        int raceNum;
        int gapToLeaderMs;
        int gapLaps;
        int numLaps;      // completed laps (for final-lap detection)
        int bestLapMs;    // best lap so far in ms (-1 = none; for fastest-lap detection)
        bool finished;    // crossed the line for good (don't follow incidents/battles on them)
    };
}

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

void DirectorManager::evaluate() {
    PluginData& pd = PluginData::getInstance();

    // Only direct while spectating/replaying.
    int ds = pd.getDrawState();
    if (ds != PluginConstants::ViewState::SPECTATE && ds != PluginConstants::ViewState::REPLAY) {
        m_wasPaused = true;   // not directing -> next live eval re-seeds, fires nothing stale
        return;
    }
    // Race vs non-race is the only session distinction the director cares about: a race
    // has on-track position battles to ride; every non-race (practice/qualify/warmup) is
    // a timing show handled identically below. The shared front matter (manual yield,
    // rider snapshot, incidents) applies to both; the scoring branches on this.
    const bool isRace = pd.isRaceSession();

    long long now = nowMs();
    if (now - m_lastDecisionMs < kDecisionIntervalMs) return;  // coalesce (throttles everything below)
    m_lastDecisionMs = now;

    // seedOnly: was the director paused (manual cam / takeover / override / hold / not
    // spectating) on the prior decision? If so, this eval only re-seeds the edge-based
    // baselines (crash count, position swap, fastest lap) instead of firing on an edge
    // that happened while away - a stale one-off cut on resume. m_wasPaused is set true
    // pessimistically here and cleared only once we reach live detection below, so any
    // early-return pause path leaves it set. (An explicit flag, not a time gap: a quiet
    // race stretch with no notifications is NOT a pause and must not swallow a real edge.)
    const bool seedOnly = m_wasPaused;
    m_wasPaused = true;

    // Yield while the broadcaster is hand-flying the camera (Orbit/Free/Free-Roam) or
    // a gamepad takeover is in flight. The takeover gesture and the resume-on-idle are
    // handled per-frame by pollManualControl() (so they work even when timing data is
    // quiet); when resume fires there it sets m_reclaiming and calls evaluate() to make
    // the fresh cut - the only path that proceeds past a live manual camera.
    SpectateHandler& spectate = SpectateHandler::getInstance();
    if ((spectate.isManualCameraActive() || m_takeoverActive) && !m_reclaiming) return;
    const bool reclaiming = m_reclaiming;
    m_reclaiming = false;

    // Manual override vs. reclaim. While the director is running, a spectated rider we
    // didn't pick means the caster manually switched riders -> adopt it and yield briefly.
    // BUT when we're reclaiming a just-released manual camera (the resume timer fired),
    // we must NOT adopt its spectated rider: a Free-Roam camera reports an unstable /
    // arbitrary "spectated" rider, and adopting it re-arms the yield grace every eval,
    // trapping the director on manual forever (the takeover-never-resumes bug). Instead,
    // force a clean fresh cut by clearing the subject so the decision below re-cuts to the
    // best story (which requests a real camera and ends the manual state).
    if (reclaiming) {
        m_currentSubject = -1;
        m_manualOverrideUntilMs = 0;
    } else {
        int spectated = pd.getSpectatedRaceNum();
        if (m_currentSubject < 0) {
            // Freshly enabled: adopt whoever is already on screen as the opening shot,
            // with no manual-override grace so the director starts working right away.
            if (spectated >= 0) {
                m_currentSubject = spectated;
                m_shotStartMs = now;
            }
        } else if (now >= m_pendingUntilMs && spectated >= 0 && spectated != m_currentSubject) {
            // The spectated rider changed to someone we didn't pick -> the user took
            // control. Adopt it and yield for a grace period before resuming.
            m_currentSubject = spectated;
            m_shotStartMs = now;
            m_manualOverrideUntilMs = now + kManualGraceMs;
            m_riderLock = false;  // caster manually chose a rider -> release the lock
            char lab[16], msg[64];
            riderLabel(spectated, lab, sizeof(lab));
            snprintf(msg, sizeof(msg), "Manual pick %s", lab);
            logDirectorEvent(msg);
        }
    }
    if (now < m_manualOverrideUntilMs) return;  // respect the human
    if (m_riderLock) {
        // Rider locked: keep the subject pinned, but keep the show alive by rotating the
        // camera on the shot cadence (TV <-> onboards) instead of freezing on one angle.
        if (m_currentSubject >= 0 && (now - m_shotStartMs) >= maxShotMs()) {
            int next = nextLockedCamera(m_currentCameraRole);
            if (next != m_currentCameraRole) {
                m_currentCameraRole = next;
                m_shotStartMs = now;
                spectate.requestSpectateRider(m_currentSubject);  // reinforce the lock
                spectate.requestSpectateCamera(static_cast<SpectateHandler::CameraRole>(next));
                DEBUG_INFO_F("Director: locked camera -> %s", cameraRoleName(next));
            }
        }
        return;   // subject stays pinned either way
    }

    // Snapshot racing riders.
    const SessionData& sd = pd.getSessionData();
    std::vector<Rider> riders;
    riders.reserve(32);
    for (const auto& kv : pd.getStandings()) {
        const StandingsData& s = kv.second;
        if (s.state != 0) continue;  // 0 = Racing (skip DNS/Retired/DSQ)
        if (s.pit != 0) continue;    // never follow a rider who is in the pits
        int pos = pd.getPositionForRaceNum(s.raceNum);
        if (pos <= 0) continue;
        // Official split gap for battle scoring, matching PluginData::getBattleGroups
        // (stable - realTimeGap flickers as the active batch is recomputed each frame,
        // which made the overlay battle panel dance). The director uses no live gaps at
        // all now; overtakes are detected from official position swaps below.
        int gtl = s.gap;
        // A rider who has crossed the line for good is still "Racing" state on the
        // slow-down lap, but shouldn't anchor incidents or battles (a crash or a close
        // gap among finishers isn't a story). The finish-lock has its own front-most-
        // still-racing logic and doesn't use this flag.
        bool finished = sd.isRiderFinished(s.numLaps, s.numLapsAtLeaderFinish);
        riders.push_back(Rider{ pos, s.raceNum, gtl, s.gapLaps, s.numLaps, s.bestLap, finished });
    }
    if (riders.empty()) return;
    std::sort(riders.begin(), riders.end(),
              [](const Rider& a, const Rider& b) { return a.position < b.position; });

    // Lull round-robin. When the director has held a shot for the full max-shot but there's no
    // real story to cut to (a quiet race sitting on the leader, or a non-race sitting on the
    // pace-setter), it dips to the NEXT rider so the camera never gets stuck and everyone gets
    // some airtime. Cycles through the field (skipping the current subject and the baseline we
    // return to), advancing a cursor per dip. Returns -1 for a one-rider field. Only ever used
    // as the last-resort variety pick — a real story always outranks it — and never during the
    // finish lock (the caller gates that), so it can't pull the camera off anything that matters.
    auto nextAirtimeSubject = [&](int currentSubject, int baselineNum) -> int {
        // Round-robin by RACE NUMBER (stable rider identity), not by grid position:
        // anchoring the cursor on a rider's *position* means a mid-race shuffle re-seeds
        // the walk (a leader that drops to 2nd, say), so riders get re-shown or skipped.
        // The walk itself lives in DirectorManager::pickNextAirtimeNum (header-only, so
        // it's unit-tested in isolation). Only the last-resort variety pick calls this,
        // and only on a lull dip (every several seconds at most), so gathering the race
        // numbers here is off the hot path. See director_airtime_test.cpp.
        std::vector<int> raceNums;
        raceNums.reserve(riders.size());
        for (const Rider& r : riders) raceNums.push_back(r.raceNum);
        return DirectorManager::pickNextAirtimeNum(raceNums, currentSubject, baselineNum, m_lastAirtimeNum);
    };

    // Is the rider we are currently following still racing?
    bool currentPresent = false;
    bool currentFinished = false;
    for (const Rider& r : riders) {
        if (r.raceNum == m_currentSubject) { currentPresent = true; currentFinished = r.finished; break; }
    }

    const Rider& leader = riders.front();

    // ---- Story interrupts (crashes, fastest lap) -------------------------------
    // These steer the subject past the normal scoring. The per-rider caches below
    // are refreshed every eval (seeded on first sight) so enabling a toggle
    // mid-race never replays a backlog of earlier events. We have passed every pause
    // gate, so this is a live decision: clear the pause flag (seedOnly captured it above).
    m_wasPaused = false;
    const int kTrackside = static_cast<int>(SpectateHandler::CameraRole::TRACKSIDE);

    // Fresh crash: front-most rider whose per-rider crash counter ticked up this eval.
    // Riders beyond the max-position cutoff are ignored (same setting as battles) so a
    // packed field's constant backmarker crashes don't hijack the camera. The crash
    // cache is still updated for everyone so edge detection stays correct.
    // Don't let a fresh crash preempt an incident shot that hasn't met its minimum hold
    // yet: a pile-up otherwise whips the camera between crashes back-to-back. This spaces
    // incident cuts by at least the shared "Hold" time (reusing that knob - no extra
    // setting). After the minimum, a new crash can still take over.
    const bool incidentYoung = (m_holdShotType == SHOT_INCIDENT && now < m_holdUntilMs
                                && (now - m_holdStartMs) < holdMs());
    // Position-weigh incident preemption: while we're framing a live battle/overtake,
    // a fresh crash only cuts in if it's at a comparable-or-BETTER position than the
    // rider we're on (posWeight is monotonic, so posWeight(crashPos) >= posWeight(curPos)
    // is just crashPos <= curPos). So a P10 tip-over can't yank the camera off a P2 fight,
    // but a front-runner going down still cuts. Only battle/overtake shots are protected -
    // a solo/leader/pace shot has nothing exciting to lose, so a crash always takes it.
    const bool protectStory = (m_currentShotType == SHOT_BATTLE || m_currentShotType == SHOT_OVERTAKE)
                              && currentPresent && m_currentSubject >= 0;
    const int protectPos = protectStory ? pd.getPositionForRaceNum(m_currentSubject) : 0;
    int crashSubject = -1;
    for (const Rider& r : riders) {
        int cc = pd.getRiderSessionCrashCount(r.raceNum);
        auto it = m_prevCrashCount.find(r.raceNum);
        bool freshCrash = (it != m_prevCrashCount.end()) && cc > it->second;
        m_prevCrashCount[r.raceNum] = cc;
        bool withinCut = (m_battleMaxPos <= 0 || r.position <= m_battleMaxPos);
        // Don't let a lower-order crash steal a better story we're already framing.
        bool outrankedByStory = (protectPos > 0 && r.position > protectPos);
        // Skip finished riders (a spill on the slow-down lap isn't a story) and skip
        // the rider we're already holding an active incident on - a flailing/ragdolling
        // rider whose crashed flag re-edges would otherwise re-cut every eval and reset
        // the linger, trapping the camera past the max-incident cap.
        bool alreadyHeld = (m_holdShotType == SHOT_INCIDENT && now < m_holdUntilMs
                            && r.raceNum == m_currentSubject);
        if (m_followIncidents && freshCrash && withinCut && !seedOnly && !r.finished
            && !alreadyHeld && !incidentYoung && !outrankedByStory && crashSubject < 0)
            crashSubject = r.raceNum;
    }

    // Tracked rider left the track / stopped: if the rider we are following becomes a
    // confirmed hazard (stationary or wrong-way) - an off into the dirt, a stall, a
    // fence - without a crash edge, fold it into the incident path so we hold the
    // moment, then release back to the race. Latched per-subject so a sustained stall
    // fires once, not every eval. (Spectated riders expose no pure off-surface flag,
    // so crash + confirmed hazard is the best "rider in trouble" signal we have.)
    if (m_followIncidents && currentPresent && !currentFinished && m_currentSubject >= 0) {
        int spos = pd.getPositionForRaceNum(m_currentSubject);
        bool withinCut = (m_battleMaxPos <= 0 || (spos > 0 && spos <= m_battleMaxPos));
        bool subjHazard = withinCut && pd.getRiderHazardType(m_currentSubject) != HazardType::None;
        if (subjHazard) {
            if (crashSubject < 0 && m_hazardLatchSubject != m_currentSubject) {
                crashSubject = m_currentSubject;
                m_hazardLatchSubject = m_currentSubject;
            }
        } else if (m_hazardLatchSubject == m_currentSubject) {
            m_hazardLatchSubject = -1;  // cleared - allow a future episode to fire
        }
    }

    // Overall fastest lap: holder, and whether it just improved this eval.
    int fastestHolder = -1, fastestMs = INT_MAX;
    for (const Rider& r : riders) {
        if (r.bestLapMs > 0 && r.bestLapMs < fastestMs) { fastestMs = r.bestLapMs; fastestHolder = r.raceNum; }
    }
    bool freshFastest = false;
    if (fastestHolder >= 0) {
        if (m_prevBestOverallMs > 0 && fastestMs < m_prevBestOverallMs && !seedOnly) freshFastest = true;
        m_prevBestOverallMs = fastestMs;
    }

    // Overtake detection via official position swaps. `riders` is the racing,
    // on-track field sorted by position; an adjacent same-lap pair whose RELATIVE
    // order flipped since last eval (the rider now ahead was behind the other before)
    // is a completed pass. Robust by construction: relative-order comparison ignores
    // jitter and is immune to pit/retire shuffles (a third rider leaving shifts both
    // positions but doesn't flip their order). Front-most pass within the cutoff wins,
    // rewarded for a short window. (The old live-gap sign-flip detector was too
    // fragile - it keyed on the frame-to-frame-volatile active-track-pos batch and a
    // clean gap crossing between 300 ms samples, so it almost never fired.)
    // Overtakes are a race-only signal: non-race standings rank is by best lap, so a
    // position swap there means someone beat another's time, not an on-track pass.
    if (m_catchOvertakes && isRace) {
        int overtaker = -1, overtaken = -1, overtakerPos = INT_MAX, overtakerPrev = 0;
        for (size_t i = 0; i + 1 < riders.size(); ++i) {
            const Rider& a = riders[i];      // now ahead (lower position)
            const Rider& b = riders[i + 1];  // now directly behind
            if (a.gapLaps != b.gapLaps) continue;   // same lap only
            // A finished rider's slow-down lap shuffles positions without an
            // on-track pass, so exclude pairs touching one (mirrors getBattleGroups).
            if (a.finished || b.finished) continue;
            auto pa = m_prevPosition.find(a.raceNum);
            auto pb = m_prevPosition.find(b.raceNum);
            if (pa == m_prevPosition.end() || pb == m_prevPosition.end()) continue;
            if (pa->second > pb->second) {   // a was behind b, now ahead -> a passed b
                bool within = (m_battleMaxPos <= 0 || a.position <= m_battleMaxPos);
                if (within && a.position < overtakerPos) {
                    overtaker = a.raceNum; overtaken = b.raceNum; overtakerPos = a.position; overtakerPrev = pa->second;
                }
            }
        }
        if (overtaker >= 0 && !seedOnly) {
            // Count how many racing riders the overtaker actually got by this move:
            // those who were ahead of it last eval and are now behind it. Only the
            // current on-track field is scanned, so positions gained from a rider
            // ahead pitting/retiring don't inflate the count.
            int passed = 0;
            for (const Rider& r : riders) {
                if (r.raceNum == overtaker || r.finished) continue;  // skip finished riders (slow-down-lap drift)
                auto pr = m_prevPosition.find(r.raceNum);
                if (pr != m_prevPosition.end() && pr->second < overtakerPrev && r.position > overtakerPos) passed++;
            }
            if (passed < 1) passed = 1;   // at least the immediate rider
            m_overtakeSubject = overtaker;
            m_overtakePartner = overtaken;   // the rider just passed (framed behind)
            m_overtakeGained = passed;
            m_overtakeUntilMs = now + kOvertakeWindowMs;
            DEBUG_INFO_F("Director: overtake - #%d passed #%d (+%d)", overtaker, overtaken, passed - 1);
        }
        m_prevPosition.clear();
        // Only snapshot riders who have completed lap 1 (numLaps >= 1). A rider on the
        // opening lap has no prior entry, so the detection above skips any pair touching
        // them - the start-line scramble (everyone passing everyone) never fires an
        // overtake cut, matching the opening-lap battle deferral. Detection resumes
        // cleanly once both riders have two consecutive post-lap-1 snapshots.
        for (const Rider& r : riders) if (r.numLaps >= 1) m_prevPosition[r.raceNum] = r.position;
    } else {
        m_prevPosition.clear();  // keep the snapshot from going stale while disabled
    }

    // Drop detection (rider tumbling down the order). A rolling window baseline records
    // each rider's position; a rider whose position has worsened by >= the threshold
    // since the baseline is dropping. Race-only and, like overtakes, keyed on official
    // positions. The subject must have completed lap 1 (numLaps >= 1) so the opening-lap
    // scramble - where everyone's position swings wildly - doesn't register as a tumble.
    if (m_followDrops && isRace) {
        if (m_dropWindowStartMs == 0 || (now - m_dropWindowStartMs) >= kDropWindowMs || seedOnly) {
            m_dropBasePos.clear();
            // Baseline only riders past lap 1, so a tumble is measured from a settled
            // position - never from a lap-1 holeshot spot, which would read as a big drop
            // as the start order shakes out. A rider gets no baseline (and can't fire a
            // drop) until a window re-seed catches them post-lap-1.
            for (const Rider& r : riders) if (r.numLaps >= 1) m_dropBasePos[r.raceNum] = r.position;
            m_dropWindowStartMs = now;
        } else {  // seedOnly already reseeded above via the first branch
            int worstNum = -1, worstLost = 0;
            for (const Rider& r : riders) {
                if (r.finished || r.numLaps < 1) continue;
                auto it = m_dropBasePos.find(r.raceNum);
                if (it == m_dropBasePos.end()) continue;
                int lost = r.position - it->second;  // positive = fell back this many places
                // Gate on where the drama STARTED (the baseline position): a top-runner
                // sliding back is the story, even though they end up deep in the field.
                bool within = (m_battleMaxPos <= 0 || it->second <= m_battleMaxPos);
                if (within && lost >= kDropThreshold && lost > worstLost) {
                    worstNum = r.raceNum; worstLost = lost;
                }
            }
            if (worstNum >= 0) {
                m_dropSubject = worstNum;
                m_dropLost = worstLost;
                m_dropUntilMs = now + kOvertakeWindowMs;
            }
        }
    } else {
        m_dropBasePos.clear();
        m_dropWindowStartMs = 0;
    }

    // Incident interrupt (highest priority): a fresh crash cuts immediately (forced
    // Trackside to frame the off) and pins the shot. While pinned, extend the hold
    // as long as the rider stays a confirmed hazard (down / wrong-way on track), up
    // to a hard cap so a rider stuck in the gravel can't trap the camera forever.
    if (crashSubject >= 0) {
        cutTo(crashSubject, false, now, kTrackside, SHOT_INCIDENT, -1, nullptr, "incident");
        armHold(SHOT_INCIDENT, now, holdMs());
        return;
    }
    if (holdActive(SHOT_INCIDENT, now, currentPresent)) {
        // Hold the incident shot - the held rider is m_currentSubject (currentPresent
        // dropping ends the hold and cuts away below). Extend while they stay a confirmed
        // hazard (down / wrong-way), up to the hard cap from the shot's start.
        bool stillHazard = m_followIncidents &&
                           pd.getRiderHazardType(m_currentSubject) != HazardType::None;
        if (stillHazard && (now - m_holdStartMs) < incidentMaxMs())
            m_holdUntilMs = std::min(now + holdMs(), m_holdStartMs + incidentMaxMs());
        return;
    }

    // Fastest-lap celebration (yields to incidents): flash to the rider who just set
    // the overall fastest lap, then hold the shot briefly. Honors the Min-shot floor so
    // a flurry of fastest laps (early race / qualifying) doesn't machine-gun the camera -
    // only an incident cuts instantly. A skipped flash is fine: the rider keeps the lap.
    if (m_followFastestLap && freshFastest && fastestHolder != m_currentSubject
        && (now - m_shotStartMs) >= minShotMs()) {
        cutTo(fastestHolder, false, now, kTrackside, SHOT_FASTEST, -1, nullptr, "fastest");
        armHold(SHOT_FASTEST, now, holdMs());
        return;
    }
    if (holdActive(SHOT_FASTEST, now, currentPresent)) return;  // hold the celebration (while trackable)

    // ---- Hot-lap "fastest sectors" detection (baseline tracked in both session types) --
    // A rider who just beat the SESSION best INDIVIDUAL sector time (S1, S2 or S3) is on
    // a hot lap. Individual sectors come from the live cumulative splits: S1 = split1,
    // S2 = split2-split1, S3 = split3-split2. RaceSplit fires field-wide, so the splits
    // are available mid-lap in every session type. The deepest sector improved wins (most
    // recent), best current rank breaks ties. The first sample in each sector only seeds
    // the baseline (no cut), as does any eval after a pause (seedOnly). The baseline loop
    // always runs (even for out-lap riders, and even when "Fastest sectors" is off) so the
    // records stay current; only the *cut* is gated. A cut requires the rider to be on a
    // genuine flying lap - past their out-lap (numLaps >= 1), which also guarantees the
    // session already has a completed-lap reference - so nothing fires before there is a
    // real time on the board. The resulting paceSubject only drives a CUT in the non-race
    // branch below (fastest sectors is a non-race-only story); in a race the baseline is
    // still tracked here so a session transition is seamless, but paceSubject goes unused.
    int paceSubject = -1, paceSplit = -1, pacePos = INT_MAX;
    for (const Rider& r : riders) {
        const CurrentLapData* cl = pd.getCurrentLapData(r.raceNum);
        if (!cl) continue;
        // Individual sector times from the cumulative splits (-1 until crossed).
        const int sec[3] = {
            cl->split1,
            (cl->split2 > 0 && cl->split1 > 0) ? (cl->split2 - cl->split1) : -1,
            (cl->split3 > 0 && cl->split2 > 0) ? (cl->split3 - cl->split2) : -1,
        };
        // Out-lap riders (no completed lap) can refresh the baseline but never trigger a
        // cut: their sectors are cold and there's no reference lap to be "on pace" against.
        const bool eligible = (r.numLaps >= 1);
        for (int i = 0; i < 3; ++i) {
            if (sec[i] <= 0) continue;
            const bool hadBaseline = (m_bestSectorMs[i] > 0);
            if (!hadBaseline || sec[i] < m_bestSectorMs[i]) {
                if (hadBaseline && !seedOnly && eligible &&
                    (i > paceSplit || (i == paceSplit && r.position < pacePos))) {
                    paceSubject = r.raceNum; paceSplit = i; pacePos = r.position;
                }
                m_bestSectorMs[i] = sec[i];
            }
        }
    }
    // NON-RACE ONLY: pace is THE story - a priority interrupt that rides the hot lap
    // (Trackside) and preempts the pace-setter baseline below. It's a stopwatch session,
    // so chasing session-best sectors IS the show. In a RACE it's deliberately not a
    // story at all (too granular - see the race branch), so position battles own the
    // camera and Finish lock owns the closing laps.
    if (!isRace && m_followPace && paceSubject >= 0) {
        // Swapping to a different hot-lap rider is a cut, so it honors the Min-shot floor
        // (only an incident cuts instantly). Re-arming the hold on the SAME rider isn't a
        // cut, so it always passes - we keep riding their lap as they string splits.
        const bool wouldCut = (paceSubject != m_currentSubject || m_currentShotType != SHOT_PACE);
        if (!wouldCut || (now - m_shotStartMs) >= minShotMs()) {
            if (wouldCut) cutTo(paceSubject, false, now, kTrackside, SHOT_PACE, -1, nullptr, "pace");
            armHold(SHOT_PACE, now, holdMs());
            m_currentPaceSplit = paceSplit;  // remember which sector for the overlay caption
            return;
        }
        // Too soon to swap riders - fall through and hold the current shot.
    }
    if (!isRace && m_followPace && holdActive(SHOT_PACE, now, currentPresent)) return;  // ride the hot lap

    // ---- Non-race timing show -------------------------------------------------
    // Practice/qualifying/warmup: no on-track battles for position (rank is by best
    // lap), so after the shared stories above the director sits on the session
    // pace-setter with the usual variety pacing, keeping a stopwatch session moving.
    if (!isRace) {
        // Baseline: sit on the pace-setter (rank P1 = fastest so far), dipping to the NEXT
        // rider for variety once we have held past the max shot - so a quiet track still mixes
        // it up and every rider gets some airtime (not just P2). The min-shot floor keeps cuts
        // from chattering.
        const int paceSetter = leader.raceNum;
        const long long shotElapsed = now - m_shotStartMs;
        if (m_currentSubject < 0 || !currentPresent) {
            cutTo(paceSetter, false, now, -1, -1, -1, nullptr,
                  m_currentSubject < 0 ? "acquire" : "subject-gone");
            return;
        }
        if (m_currentSubject == paceSetter) {
            // A variety ONBOARD dip is brief flavour, not a hero shot: cap it at the
            // min-shot so it doesn't hold the pace-setter on a bike cam for the full
            // max-shot (which reads as the camera being "stuck" onboard). A trackside /
            // Auto hero shot holds the full max-shot as before. Cutting away re-runs
            // pickShot, which returns to Auto/trackside unless the cadence dips again.
            using CR = SpectateHandler::CameraRole;
            const bool onboard = (m_currentCameraRole >= static_cast<int>(CR::ONBOARD_FRONT) &&
                                  m_currentCameraRole <= static_cast<int>(CR::FORKS));
            const long long dwellCap = onboard ? minShotMs() : maxShotMs();
            if (shotElapsed >= dwellCap) {
                int v = nextAirtimeSubject(m_currentSubject, paceSetter);
                if (v >= 0) cutTo(v, false, now, -1, -1, -1, nullptr, "maxshot");
            }
            return;
        }
        // Currently on someone other than the pace-setter (a variety dip or a finished
        // hot lap): return to the pace-setter once the minimum shot has elapsed.
        if (shotElapsed >= minShotMs()) cutTo(paceSetter, false, now, -1, -1, -1, nullptr, "return");
        return;
    }

    // Score candidate stories. best = highest overall; alt = highest whose subject
    // differs from the current one (used to force variety past maxShot). The camera
    // is chosen per context at cut time (battle vs solo), so we only track which
    // kind of shot won here.
    // Leader baseline: the front-most rider STILL RACING, so with Finish lock off we
    // don't park the camera on a finished winner sitting on their slow-down lap while
    // the race for position continues behind. Falls back to the leader if all finished.
    int baselineNum = leader.raceNum;
    for (const Rider& r : riders) { if (!r.finished) { baselineNum = r.raceNum; break; } }
    int bestSubject = baselineNum; bool bestIsBattle = false; int bestPartner = -1;
    double bestScore = posWeight(1) * 0.6;  // leader baseline so there is never dead air
    int altSubject = -1; bool altIsBattle = false; int altPartner = -1;
    double altScore = -1.0;

    // partner = the other rider framed in a battle/overtake shot (-1 for solo).
    auto consider = [&](int subject, double score, bool isBattle, int partner) {
        if (score > bestScore) { bestScore = score; bestSubject = subject; bestIsBattle = isBattle; bestPartner = partner; }
        if (subject != m_currentSubject && score > altScore) { altScore = score; altSubject = subject; altIsBattle = isBattle; altPartner = partner; }
    };

    // Register the leader baseline as a candidate too, so it can also be the alt
    // (variety) pick when we are currently following someone else.
    consider(baselineNum, bestScore, false, -1);

    // Battles: use the shared battle definition (PluginData::getBattleGroups) so the
    // director and the web overlay agree on what a battle is - one brain, one config
    // (battle gap + max position both live in the Director settings). Follow the front
    // rider of each group (chaser framed behind); closeness from the front pair's gap.
    // A bigger group (3+ riders nose-to-tail) scores higher than a lone pair.
    std::unordered_map<int, int> gapByNum;
    gapByNum.reserve(riders.size());
    for (const Rider& r : riders) gapByNum[r.raceNum] = r.gapToLeaderMs;
    // The "Follow battles" toggle gates only the director's subject scoring; the
    // battle-gap value still defines a battle for the overlay panel independently, so
    // when off we simply don't score any battle groups (empty -> findGroup() nullptr).
    const auto battleGroups = m_followBattles ? pd.getBattleGroups(m_battleGapMs, m_battleMaxPos)
                                              : std::vector<std::vector<int>>{};
    // Look up the group a battle front leads (so a cut can hand pickShot the riders to
    // rotate the onboard through). Returns nullptr for non-battle-front subjects.
    auto findGroup = [&](int frontNum) -> const std::vector<int>* {
        for (const auto& g : battleGroups) if (!g.empty() && g[0] == frontNum) return &g;
        return nullptr;
    };
    for (const auto& grp : battleGroups) {
        if (grp.size() < 2) continue;
        int frontNum = grp[0], chaserNum = grp[1];
        int frontPos = pd.getPositionForRaceNum(frontNum);
        auto fg = gapByNum.find(frontNum), cg = gapByNum.find(chaserNum);
        if (frontPos <= 0 || fg == gapByNum.end() || cg == gapByNum.end()) continue;
        int interval = cg->second - fg->second;
        if (interval <= 0) continue;  // defensive; getBattleGroups already guarantees > 0
        int groupSize = static_cast<int>(grp.size());
        double closeness = 1.0 - static_cast<double>(interval) / m_battleGapMs;
        double sizeBoost = std::min(2.0, 1.0 + (groupSize - 2) * 0.25);  // 2->1.0, 3->1.25 ... cap 2.0
        double score = closeness * posWeight(frontPos) * 2.0 * sizeBoost;
        consider(frontNum, score, true, chaserNum);
    }

    // Overtake reward: a freshly-completed pass outscores routine battles for a short
    // window so the director cuts to (or holds) the move. Framed as a battle so the
    // camera frames both riders (Trackside). Falls through if the overtaker dropped out.
    if (m_catchOvertakes && m_overtakeSubject >= 0 && now < m_overtakeUntilMs) {
        int opos = pd.getPositionForRaceNum(m_overtakeSubject);
        // A multi-place move (one rider clearing several at once) is a bigger story than
        // a single pass, so it scores higher - enough to win out over a routine battle
        // and, between two passes, to prefer the rider who gained more. Capped so a pile
        // of lapped-rider passes can't dominate. size = riders involved in the move
        // (overtaker + those passed), so the badge "+K" reads K = passed-beyond-the-
        // immediate, same as a battle group's "+K".
        double passBoost = std::min(2.5, 1.0 + (m_overtakeGained - 1) * 0.5);  // 1 pass=1.0, 2=1.5, 3=2.0, cap 2.5
        if (opos > 0) consider(m_overtakeSubject, posWeight(opos) * 3.0 * passBoost, true, m_overtakePartner);
        else m_overtakeUntilMs = 0;
    }

    // Lappers (opt-in): a front-runner working through backmarkers (1+ laps up, closing on
    // traffic) is good filler content but ranks BELOW a real position battle. Score the
    // front-most lapper within the cutoff; m_lapperSubject tags the shot so the badge reads
    // "lapping #N" and groupFor() leaves it Trackside (no chaser dip on a lapping shot).
    m_lapperSubject = -1;
    if (m_followLappers) {
        for (const Rider& r : riders) {
            if (m_battleMaxPos > 0 && r.position > m_battleMaxPos) continue;
            int lapped = pd.getRiderLappingTarget(r.raceNum);
            if (lapped < 0) continue;
            m_lapperSubject = r.raceNum;   // riders are position-sorted -> front-most lapper
            consider(r.raceNum, posWeight(r.position) * 1.2, true, lapped);  // < a close battle's ~*2.0
            break;
        }
    }

    // Drops (opt-in): a rider tumbling down the order while its boost window is live. A
    // real story but below a battle/overtake - scored a touch above a lapper, weighted by
    // the rider's CURRENT (dropped) position. Framed Trackside solo (shotFor tags it
    // SHOT_DROP; groupFor leaves it Trackside with no chaser dip).
    if (m_followDrops && m_dropSubject >= 0 && now < m_dropUntilMs) {
        int dpos = pd.getPositionForRaceNum(m_dropSubject);
        if (dpos > 0) {
            double dropBoost = std::min(2.0, 1.0 + (m_dropLost - kDropThreshold) * 0.25);
            consider(m_dropSubject, posWeight(dpos) * 1.6 * dropBoost, true, -1);
        } else {
            m_dropUntilMs = 0;  // subject left the race
        }
    }

    // Fastest sectors is a NON-RACE-only story (handled above as a priority interrupt):
    // in a race, chasing session-best individual sectors is too granular - it would pull
    // the camera off position battles for a hot-lap sliver that rarely matters to the
    // race. So it's deliberately NOT scored as a race candidate here. (paceSubject is
    // still detected above only to keep the sector baselines warm across a session
    // transition; it goes unused in the race branch.)

    // Race start: opening-lap battles are deferred upstream (getBattleGroups skips
    // riders still on lap 1), because the bunched grid makes "everyone" a battle. So
    // the start rides the leader baseline instead - it follows the front of the pack
    // with the normal camera until real gaps form. (We can't assume a usable Start
    // camera exists - many tracks have none, and the grid cam points away from the
    // action - so there's no forced start cut either.)

    // Finish lock: on the leader's final lap (or once they have finished), lock to
    // the front so the win is never missed - follow the lead battle if P1/P2 are
    // close, else the leader solo - and suppress the max-shot wander so we don't
    // cut away. (isRiderOnLastLap/isRiderFinished cover every race type; the
    // time+lap getLeaderLapsToGo is overtime-only, so we use these instead.)
    bool finishWindow = false;
    bool leaderFinished = false;
    if (m_finishLock) {
        leaderFinished = sd.isRiderFinished(leader.numLaps);
        finishWindow = sd.isRiderOnLastLap(leader.numLaps) || leaderFinished;
    }
    if (finishWindow) {
        // When the leader crosses the line, arm a brief winner celebration; after it
        // expires, move the lock to the battle for the next position so we don't sit
        // on a parked winner.
        if (leaderFinished && m_finishedWinnerNum != leader.raceNum) {
            m_finishedWinnerNum = leader.raceNum;
            m_winnerHoldUntilMs = now + kWinnerCelebrationMs;
        }

        int frontIdx = 0;  // default: the leader (final-lap run, or the winner during celebration)
        if (leaderFinished && now >= m_winnerHoldUntilMs) {
            // Celebration over: target the front-most rider still racing to the flag.
            // Use the Rider's precomputed two-arg `finished` (the single-arg
            // isRiderFinished can't detect a LAPPED rider's finish - needs
            // numLapsAtLeaderFinish - so it would settle on a lapped rider who's crossed).
            for (size_t i = 0; i < riders.size(); ++i) {
                if (!riders[i].finished) { frontIdx = static_cast<int>(i); break; }
            }
        }

        const Rider& front = riders[frontIdx];
        bestSubject = front.raceNum; bestIsBattle = false; bestPartner = -1;
        size_t nextI = static_cast<size_t>(frontIdx) + 1;
        if (nextI < riders.size()) {
            const Rider& chaser = riders[nextI];
            int interval = chaser.gapToLeaderMs - front.gapToLeaderMs;
            if (chaser.gapLaps == front.gapLaps && interval > 0 && interval <= m_battleGapMs) {
                bestIsBattle = true;  // front pair fighting -> hold the front rider, chaser in frame
                bestPartner = chaser.raceNum;
            }
        }
        altSubject = -1;  // never wander off the front during the finish
    }
    // Before anyone has finished it's the leader's final lap (shot "finallap"); once
    // the leader crosses, it's the run-in to the flag (shot "finish") - so the shot
    // tracks the on-track notice instead of saying "finish" a whole lap early. The
    // overlay caption then refines "finish" per rider: "FINISHING" while the framed
    // rider is still running to the flag, "FINISHED" once they've crossed.
    const int finishShot = finishWindow ? (leaderFinished ? SHOT_FINISH : SHOT_FINAL_LAP) : -1;
    const int finishRole = finishWindow ? kTrackside : -1;

    // The shot type for a cut to `s`: finish lock wins; else a cut to the rewarded
    // overtaker (within its window) is tagged SHOT_OVERTAKE so the badge/overlay show
    // "overtake" rather than a generic "battle"; else let cutTo derive it.
    auto shotFor = [&](int s) -> int {
        if (finishShot >= 0) return finishShot;
        if (m_catchOvertakes && m_overtakeSubject >= 0 && now < m_overtakeUntilMs
            && s == m_overtakeSubject) return SHOT_OVERTAKE;
        // (No SHOT_PACE here: fastest sectors is a non-race-only story, so in the race
        // branch a rider who happens to be on a hot lap must not be mislabeled "pace" -
        // their real story is the battle/overtake/etc. that actually selected them.)
        // Tag a drop shot (rider tumbling down the order) so the badge reads "dropped".
        if (m_followDrops && m_dropSubject >= 0 && now < m_dropUntilMs
            && s == m_dropSubject) return SHOT_DROP;
        // Tag a lapping shot - but only if this rider isn't also a battle front (a real
        // position battle wins the label and the chaser-dip behaviour via groupFor).
        if (m_followLappers && s >= 0 && s == m_lapperSubject && !findGroup(s)) return SHOT_LAPPER;
        return -1;
    };

    long long shotElapsed = now - m_shotStartMs;

    // Hand the battle's group to the cut only for a plain battle (shotFor < 0) - not for an
    // overtake or the finish lock, which stay front-anchored on Trackside.
    auto groupFor = [&](int subject, bool isBattle) -> const std::vector<int>* {
        return (isBattle && shotFor(subject) < 0) ? findGroup(subject) : nullptr;
    };

    // First shot, or the rider we were following has left the race: cut now.
    if (m_currentSubject < 0 || !currentPresent) {
        cutTo(bestSubject, bestIsBattle, now, finishRole, shotFor(bestSubject), bestPartner,
              groupFor(bestSubject, bestIsBattle),
              m_currentSubject < 0 ? "acquire" : "subject-gone");
        return;
    }

    int targetSubject = bestSubject;
    bool targetIsBattle = bestIsBattle;
    int targetPartner = bestPartner;
    // Pacing reason for the log: a different subject winning the scoring is "story";
    // the max-shot variety/round-robin paths below override to "maxshot"; the finish
    // window overrides to "finish" (it also drops the min-shot gate).
    const char* reason = "story";

    if (targetSubject == m_currentSubject) {
        // Best story is still our current subject. Hold unless we have lingered past
        // the max shot, in which case force variety to a different subject.
        if (shotElapsed >= maxShotMs() && altSubject >= 0) {
            targetSubject = altSubject;
            targetIsBattle = altIsBattle;
            targetPartner = altPartner;
            reason = "maxshot";
        } else if (shotElapsed >= maxShotMs() && !finishWindow) {
            // No competing story, but we've held the max shot: dip to the next rider (round-
            // robin) so the camera never sticks on the leader in a quiet race and everyone gets
            // some airtime. A solo shot; a real story on the next eval instantly reclaims the
            // camera (it outranks this). Suppressed during the finish lock (stay on the front).
            int v = nextAirtimeSubject(m_currentSubject, baselineNum);
            if (v < 0) return;   // one-rider field — nothing to dip to
            targetSubject = v;
            targetIsBattle = false;
            targetPartner = -1;
            reason = "maxshot";
        } else {
            return;  // plain hold (no re-cut)
        }
    }
    if (finishWindow) reason = "finish";

    // A different subject wins - honor the minimum shot length before cutting away.
    // The finish lock bypasses the minimum so it snaps to the front immediately.
    long long gate = finishWindow ? 0 : minShotMs();
    if (shotElapsed >= gate) {
        cutTo(targetSubject, targetIsBattle, now, finishRole, shotFor(targetSubject), targetPartner,
              groupFor(targetSubject, targetIsBattle), reason);
    }
}
