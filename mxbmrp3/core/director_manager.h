// ============================================================================
// core/director_manager.h
// Auto-director for spectate mode: picks which rider the broadcast follows
// (and a baseline camera) based on an "interest" model with broadcast pacing.
//
// The plugin can choose the spectated rider (SpectateVehicles) and the camera
// (SpectateCameras) via the proven *piSelect/return-1 one-shot pattern, but it
// cannot choose camera *angles* beyond the named set the game exposes, so the
// director's job is subject selection plus a name-based camera baseline.
//
// Global (broadcast) feature, not per-profile - persisted in the [Director] INI
// section like HelmetOverlay/Rumble. Active only while spectating/replaying a
// race; fully passive otherwise.
// ============================================================================
#pragma once

#include <functional>
#include <unordered_map>
#include <vector>

#include "director_scoring.h"   // director_detail::Rider (evaluate()'s phase signatures)

class DirectorManager {
public:
    static DirectorManager& getInstance();

    // Tunable ranges (exposed in the Director settings tab for live tuning).
    static constexpr int MIN_SHOT_LO = 2,  MIN_SHOT_HI = 20;   // seconds
    static constexpr int MAX_SHOT_LO = 5,  MAX_SHOT_HI = 40;   // seconds (0 = Off, see setMaxShotSec)
    static constexpr int BATTLE_GAP_LO = 500, BATTLE_GAP_HI = 5000, BATTLE_GAP_STEP = 250;  // ms
    static constexpr int BATTLE_MAXPOS_HI = 40;   // ignore battles beyond this position (0 = no limit)
    static constexpr int RESUME_HI = 120, RESUME_STEP = 5;     // seconds (0 = off)
    static constexpr int VARIETY_LO = 2,  VARIETY_HI = 10;     // every Nth cut dips to an onboard
    static constexpr int HOLD_LO = 2, HOLD_HI = 15;           // shared story hold (seconds)
    static constexpr int INC_MAX_LO = 5,  INC_MAX_HI = 30;    // incident hold cap (INI-only, >= hold)

    // --- Persisted settings ---
    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool enabled);
    int getMinShotSec() const { return m_minShotSec; }      // shortest time on one shot
    void setMinShotSec(int sec);
    // Longest time on one subject before the director forces a change of scene. 0 = Off:
    // FORCED ROTATION DISABLED — the director then only ever cuts for a story (incident,
    // fastest lap, battle, overtake, ...) and returns to the broadcaster's own rider when
    // there is none, instead of pacing the field on a timer. See forcedRotation().
    int getMaxShotSec() const { return m_maxShotSec; }
    void setMaxShotSec(int sec);
    // Is the timer-driven rotation on? Everything the max shot drives — the forced variety
    // cut, the lull round-robin, the non-race dip, the rider-lock camera cycle — is gated on
    // this one predicate, so "Max shot = Off" cannot half-apply.
    bool forcedRotation() const { return m_maxShotSec > 0; }
    int getBattleGapMs() const { return m_battleGapMs; }    // max gap that counts as a battle
    void setBattleGapMs(int ms);
    int getBattleMaxPos() const { return m_battleMaxPos; }  // ignore battles whose leader is beyond this position (0 = no limit)
    void setBattleMaxPos(int pos);
    int getManualResumeSec() const { return m_manualResumeSec; }  // resume after manual cam (0 = off)
    void setManualResumeSec(int sec);
    // When on, a deliberate stick push grabs the Free-Roam camera and pauses the
    // director (no in-game menu needed); the usual inactivity-resume reclaims it.
    bool getGamepadTakeover() const { return m_gamepadTakeover; }
    void setGamepadTakeover(bool v) { m_gamepadTakeover = v; }

    // Variety cameras: which onboard cams the director may sprinkle into solo shots
    // (battles always use Trackside). Unchecking one removes it from the pool.
    bool getCamFront()  const { return m_camFront; }
    bool getCamRear()   const { return m_camRear; }
    bool getCamHelmet() const { return m_camHelmet; }
    bool getCamHelmet2() const { return m_camHelmet2; }
    bool getCamForks()  const { return m_camForks; }
    void setCamFront(bool v)  { m_camFront = v; }
    void setCamRear(bool v)   { m_camRear = v; }
    void setCamHelmet(bool v) { m_camHelmet = v; }
    void setCamHelmet2(bool v) { m_camHelmet2 = v; }
    void setCamForks(bool v)  { m_camForks = v; }

    // Story toggles: high-value events that steer the subject beyond the normal
    // battle/leader scoring. Each is an independent opt-out (defaults on).
    //  - incidents : a fresh crash interrupts immediately (forced Trackside),
    //                lingering while the rider stays down (up to a cap).
    //  - fastest   : flash to a rider who just set the overall fastest lap.
    //  - finishLock: on the leader's final lap, lock to the front so the win
    //                is never missed.
    // Follow battles: let close on-track fights steer the camera. Independent of the
    // battle-gap value, which still defines a battle for the web overlay's panel even
    // when the director itself isn't chasing them.
    bool getFollowBattles() const { return m_followBattles; }
    void setFollowBattles(bool v) { m_followBattles = v; }
    bool getFollowIncidents()  const { return m_followIncidents; }
    bool getFollowFastestLap() const { return m_followFastestLap; }
    bool getFinishLock()       const { return m_finishLock; }
    void setFollowIncidents(bool v)  { m_followIncidents = v; }
    void setFollowFastestLap(bool v) { m_followFastestLap = v; }
    void setFinishLock(bool v)       { m_finishLock = v; }
    // Catch overtakes: reward a rider who just completed an on-track pass (detected
    // from live-gap order reversals among the active-batch riders near the camera)
    // so the director cuts to / holds the move before the official standings update.
    bool getCatchOvertakes() const { return m_catchOvertakes; }
    void setCatchOvertakes(bool v) { m_catchOvertakes = v; }
    // Follow lappers: cut to a front-runner working through backmarkers (a rider 1+ laps
    // up closing on traffic). Lower priority than a real position battle. Opt-in.
    bool getFollowLappers() const { return m_followLappers; }
    void setFollowLappers(bool v) { m_followLappers = v; }
    // Follow drops: cut to a rider tumbling down the order (losing several places over a
    // short window - a mistake, a moment, a mechanical). Mirror of Catch overtakes, lower
    // priority than a real battle/overtake. Race-only, opt-in.
    bool getFollowDrops() const { return m_followDrops; }
    void setFollowDrops(bool v) { m_followDrops = v; }
    // Fastest sectors: cut to a rider who just beat a session-best individual sector
    // (S1/S2/S3) - the hot lap in progress. In a NON-race it's the core timing story; in a
    // RACE it also fires as a priority story (above battles). A cut needs the rider on a
    // flying lap (past the out-lap), so nothing fires before a time is on the board. On by
    // default.
    bool getFollowPace() const { return m_followPace; }
    void setFollowPace(bool v) { m_followPace = v; }

    // Pacing/timing knobs.
    //  - varietyEvery : every Nth cut dips into an enabled onboard camera (0 = Off).
    //  - hold         : one shared hold for all story shots (incidents + fastest lap).
    //  - incidentMax  : hard cap on the incident hold so a stuck rider can't trap it;
    //                   INI-only tunable (no GUI), kept >= hold.
    int getVarietyEvery()      const { return m_varietyEvery; }
    void setVarietyEvery(int n);
    int getHoldSec() const { return m_holdSec; }
    void setHoldSec(int sec);
    int getIncidentMaxSec()    const { return m_incidentMaxSec; }
    void setIncidentMaxSec(int sec);

    // --- Status (read by the Director HUD and the web-overlay advisory block) ---
    int getCurrentSubject() const { return m_currentSubject; }       // followed raceNum (-1 = none)
    int getCurrentPartner() const { return m_currentPartner; }       // other rider in a battle shot (-1 = none)
    // (battle group size is intentionally not tracked here: the overlay computes its own
    // battle groups for the "+K" badge, so the director never needed to publish it.)
    int getCurrentCameraRole() const { return m_currentCameraRole; } // SpectateHandler::CameraRole as int
    static const char* cameraRoleName(int role);                     // "Auto" / "Trackside" / ...
    // While the rider lock is on, the subject is pinned but the camera still rotates on the
    // shot cadence so it doesn't sit frozen on one angle. Returns the next camera role in the
    // enabled pool (AUTO/TV -> each enabled onboard -> wrap), or currentRole unchanged when
    // only the TV shot is available. Public so a headless test can assert the cycle order.
    int nextLockedCamera(int currentRole) const;

    // Lull round-robin cursor advance (header-only so it can be unit-tested in
    // isolation). Given the current field's race numbers, pick the next rider to
    // dip to during a lull: the smallest race number greater than `lastAirtimeNum`,
    // wrapping to the smallest overall, skipping `currentSubject` and `baselineNum`.
    // Keying on race NUMBER (stable rider identity) rather than grid position keeps
    // the walk stable across a mid-race order shuffle — a position-anchored cursor
    // re-seeds when the order churns, re-showing or skipping riders. Advances
    // `lastAirtimeNum` to the pick; returns -1 when every rider is excluded (tiny field).
    static int pickNextAirtimeNum(const std::vector<int>& raceNums,
                                  int currentSubject, int baselineNum,
                                  int& lastAirtimeNum) {
        int best = -1, wrap = -1;
        for (int num : raceNums) {
            if (num == currentSubject || num == baselineNum) continue;
            if (wrap == -1 || num < wrap) wrap = num;
            if (num > lastAirtimeNum && (best == -1 || num < best)) best = num;
        }
        const int pick = (best != -1) ? best : wrap;
        if (pick != -1) lastAirtimeNum = pick;
        return pick;
    }

    // The rider the BROADCASTER put on camera: whoever was already spectated when the
    // director was enabled, plus every later manual pick. -1 until one is adopted.
    // Only consulted while forced rotation is off, where it is the dead-air floor
    // (see pickBaselineSubject); with rotation on the director paces the whole field
    // and the home rider is just history. Read by a headless test.
    int getHomeSubject() const { return m_homeSubject; }

    // The dead-air floor for one decision pass: which rider the camera falls back to when
    // no story is running. With forced rotation ON that is `fallback` — the leader in a
    // race, the pace-setter in a non-race — and the director paces on from there. With it
    // OFF the broadcaster's own rider owns the floor, so a story cut RETURNS to them once
    // the story ends rather than drifting up the order. Degrades to `fallback` while that
    // rider isn't followable (pitted, retired, finished) or was never established, so
    // "Max shot = Off" never means dead air. Header-only so it is unit-tested in isolation
    // (test_director_airtime.cpp).
    static int pickBaselineSubject(const std::vector<director_detail::Rider>& riders,
                                   bool forcedRotation, int homeSubject, int fallback) {
        if (forcedRotation || homeSubject < 0) return fallback;
        for (const director_detail::Rider& r : riders)
            if (r.raceNum == homeSubject && !r.finished) return homeSubject;
        return fallback;
    }

    const char* getCurrentShotType() const;                          // solo/battle/incident/fastest/finish/overtake/pace/finalLap/lapper/drop
    // Which individual sector the current "pace" shot is up on: 0 = S1, 1 = S2, 2 = S3
    // (-1 when the current shot isn't a pace shot). Lets the overlay caption name
    // the sector ("FASTEST S1" / "FASTEST S2" / "FASTEST S3").
    int getCurrentPaceSplit() const { return (m_currentShotType == SHOT_PACE) ? m_currentPaceSplit : -1; }
    // How many positions the current overtake shot's rider gained in the move (-1 when
    // the current shot isn't an overtake). Lets the overlay caption read "GAINED N".
    int getCurrentOvertakeGained() const { return (m_currentShotType == SHOT_OVERTAKE) ? m_overtakeGained : -1; }
    // How many positions the current drop shot's rider lost in the move (-1 when the
    // current shot isn't a drop). Lets the overlay caption read "DROPPED N".
    int getCurrentDropLost() const { return (m_currentShotType == SHOT_DROP) ? m_dropLost : -1; }
    bool isActivelyDirecting() const;                                // enabled, following someone, not paused/manual/hold

    // --- Hotkey actions ---
    void toggleEnabled() { setEnabled(!m_enabled); }
    void toggleLock();          // lock onto the current rider (pin subject; suppress auto-cutting)
    bool isLocked() const { return m_riderLock; }
    // True while a gamepad takeover is grabbing manual control. Distinct from
    // isManualCameraActive() (camera-name based): a takeover on a track with no Free-Roam
    // camera still yields via this flag even though the camera name never changes.
    bool isTakeoverActive() const { return m_takeoverActive; }

    // --- Hooks ---
    // Called from HudManager::onDataChanged. changeType is DataChangeType cast to
    // int (kept as int to avoid pulling plugin_data.h into this header).
    void onDataChanged(int changeType);

    // Per-frame manual-control poll (gamepad takeover + auto-resume). Called every
    // frame from HudManager so it ticks independently of timing-data callbacks - the
    // takeover gesture and the resume-on-idle work even when the data is quiet (lulls,
    // solo sessions, replays), unlike the data-driven evaluate(). Throttled internally.
    void pollManualControl();

    // Per-frame pacing pump. evaluate() enforces the min/max-shot cadence, but it is
    // otherwise only driven by Standings/IdealLap data callbacks - so during a quiet
    // stretch (stable formation, no notifications) NOTHING ticks the director and the
    // current shot (usually the leader) blows past the max-shot cap. Calling evaluate()
    // every frame here makes pacing wall-clock-driven instead of data-driven. It is
    // cheap: evaluate() early-outs when the director is disabled / not spectating, and
    // its internal ~3x/sec coalesce gate throttles the actual work (so in a race, where
    // Standings already drives it, this only fills the gaps - it does not run it faster).
    void pollPacing();

#if defined(MXBMRP3_TEST_BUILD)
    // Test-only: inject a simulated wall-clock (ms) so a headless tape replay can drive
    // the director's real pacing from recorded timestamps. -1 restores the real clock.
    // Not present in a shipping DLL. See nowMs() in the .cpp. Each cut the director makes
    // is logged (cutTo()), so the broadcast can be reconstructed by parsing the log.
    static void testSetNowMs(long long ms);
#endif

private:
    DirectorManager() = default;
    ~DirectorManager() = default;
    DirectorManager(const DirectorManager&) = delete;
    DirectorManager& operator=(const DirectorManager&) = delete;

    // Shot kind, recorded at each cut for the status HUD / overlay advisory block.
    // SHOT_PACE is the non-race "hot lap" cut (a rider on session-best cumulative pace).
    enum ShotType { SHOT_SOLO = 0, SHOT_BATTLE, SHOT_INCIDENT, SHOT_FASTEST, SHOT_FINISH, SHOT_OVERTAKE, SHOT_PACE, SHOT_FINAL_LAP, SHOT_LAPPER, SHOT_DROP };

    void evaluate();

    // evaluate() phases. evaluate() itself is the priority ladder — each story is
    // tried in order and a winner cuts and returns — so these are the self-
    // contained blocks that ladder consults, pulled out to keep the ladder itself
    // readable. They deliberately stay MEMBERS rather than free functions: each
    // reads or updates the per-rider edge-detection caches (m_prevCrashCount,
    // m_bestSectorMs, ...) that make the detection stateful. The genuinely
    // stateless parts — every story-score formula, and the overtake/drop
    // comparisons — live in director_scoring.h / director_detect.h instead, where
    // they are unit-tested without a game.
    //
    // Snapshot of the racing, on-track field for one decision pass, sorted by
    // position. Built once and read by every phase below.
    std::vector<director_detail::Rider> collectRiders() const;
    // Front-most rider with a fresh crash edge, or a followed rider who just
    // became a confirmed hazard. Refreshes the per-rider crash baseline for the
    // WHOLE field either way, so edge detection stays correct even while the
    // camera is elsewhere. -1 = no incident to cut to.
    int detectIncidentSubject(const std::vector<director_detail::Rider>& riders,
                              long long now, bool seedOnly, bool currentPresent,
                              bool currentFinished);
    // Refresh the session-best individual sector times (S1/S2/S3) from the live
    // cumulative splits. Returns the rider on the deepest freshly-improved sector
    // (-1 = none) and writes which sector into `outSplit`. The BASELINE update
    // always runs — even for out-lap riders and even when the feature is off — so
    // the records stay current across a session; only the returned cut candidate
    // is gated.
    int updateSectorBests(const std::vector<director_detail::Rider>& riders,
                          bool seedOnly, int& outSplit);
    // The non-race (practice/qualify/warmup) timing show: sit on the pace-setter
    // with variety dips. Terminal — it cuts as needed and evaluate() returns.
    void runNonRaceShow(long long now, bool currentPresent, int paceSetterNum,
                        const std::function<int(int, int)>& nextAirtimeSubject);

    // One race decision pass's scored candidates. `best` is the highest-scoring
    // story overall; `alt` is the highest whose subject differs from the one we
    // are already on, which is what a max-shot variety cut needs (falling back to
    // `best` there would just re-pick the current subject and never cut away).
    struct StoryScores {
        int baselineNum = -1;   // front-most rider STILL RACING (the dead-air floor)
        int bestSubject = -1;  bool bestIsBattle = false;  int bestPartner = -1;
        int altSubject = -1;   bool altIsBattle = false;   int altPartner = -1;
        // Front-first race numbers per battle group, as PluginData defines them —
        // kept so a cut can hand pickShot the riders to rotate an onboard through.
        std::vector<std::vector<int>> battleGroups;
        // The group `frontNum` leads, or nullptr if it isn't a battle front.
        const std::vector<int>* findGroup(int frontNum) const {
            for (const auto& g : battleGroups)
                if (!g.empty() && g[0] == frontNum) return &g;
            return nullptr;
        }
    };
    // Score every race story type (battles, the overtake reward, lappers, drops)
    // against the leader baseline. The RANKING between story types lives in
    // director_scoring.h; this decides which riders are candidates at all.
    StoryScores scoreRaceStories(const std::vector<director_detail::Rider>& riders,
                                 long long now);

    void resetRuntime();   // clear all runtime/story state (enable toggle + new session)
    long long nowMs() const;
    // forceRole >= 0 pins the camera (a SpectateHandler::CameraRole as int) instead
    // of running the per-context pickShot - used by incident/fastest/finish
    // cuts that always want Trackside. shotType < 0 derives solo/battle from isBattle;
    // partner is the other rider in a battle/overtake shot (-1 = none).
    // `group` (front-first race numbers) lets a battle variety dip redirect the shot to a
    // chasing rider with a forward onboard; pass nullptr for solo / forced cuts.
    // `reason` is the PACING trigger for this cut (why the camera left the previous shot
    // NOW), logged as reason=<...> alongside the shot type so a log/replay can tell a
    // min-shot-honoring cut from a legitimate bypass. The distinction matters because the
    // shot TYPE alone doesn't: a solo cut can be an acquire, a subject-gone, a max-shot
    // dip, or a session reset. Values: "acquire" (first shot / post-reset, incl. a new
    // session), "subject-gone" (followed rider left the race), "story" (a better-placed
    // rider/battle/overtake won the scoring), "maxshot" (held the max -> forced variety
    // dip), "return" (non-race: back to the pace-setter after a dip), "home" (forced
    // rotation off: the story ended, so back to the broadcaster's own rider), and the
    // story cuts that name themselves: "incident", "fastest", "pace", "finish". The bypass
    // (acquire / subject-gone / incident / finish) is exactly the cuts that skip the
    // min-shot floor by design; everything else honors it.
    void cutTo(int raceNum, bool isBattle, long long now, int forceRole = -1,
               int shotType = -1, int partner = -1,
               const std::vector<int>* group = nullptr,
               const char* reason = "?");
    // Event-log transparency (broadcast). Entries are emitted UNCONDITIONALLY, like every
    // other event producer — the in-game "Director" event toggle and the web overlay filter
    // them at display time (raw-data contract; see http_server.cpp).
    // Push a one-line "director cut" entry. Reads m_currentShotType (set by cutTo) to phrase
    // the decision; deduped against the previous cut. Called at the end of cutTo.
    void emitCutEvent(int subject, int partner);
    // Push a one-line director STATE change (enable/disable, rider lock, manual control,
    // resume). Clears the cut-dedup so the next cut always logs fresh after a state change.
    // iconColorSlot: optional ColorSlot (as int) tinting this entry's event-log icon to
    // match the director button's state color; -1 = the event log's default director color.
    void logDirectorEvent(const char* message, int iconColorSlot = -1);
    // Format a rider's plate ("#42") into buf — its formattedRaceNum, or "#<num>" fallback.
    void riderLabel(int raceNum, char* buf, size_t n) const;
    // Per-context camera + variety. For a battle variety dip it may redirect the shot to a
    // group rider (front -> Rear Fender; a chaser -> a forward onboard), writing the chosen
    // rider to *outTarget (left = the subject otherwise). Returns the CameraRole as int.
    int pickShot(bool isBattle, const std::vector<int>& group, int* outTarget);

    // Story-hold helpers (incident / fastest-lap / non-race pace). One shared latch
    // holds whichever story shot is currently pinned; only one can hold at a time
    // because each story fires with cutTo()+return in priority order. armHold() pins a
    // shot of `shotType` for `lingerMs`; holdActive() reports whether that hold is still
    // running (held rider trackable) and, when it has lapsed (timer up OR rider gone),
    // clears the latch so a later cut to a different rider isn't mistaken for a hold.
    void armHold(int shotType, long long now, long long lingerMs);
    bool holdActive(int shotType, long long now, bool currentPresent);

    // All access (hotkeys, settings load, UI dispatch, onDataChanged) is on the
    // game thread, so these members are deliberately non-atomic. A future
    // background writer would need to make the touched fields atomic (see the
    // cross-thread-flags invariant in CLAUDE.md).

    long long minShotMs() const { return static_cast<long long>(m_minShotSec) * 1000; }
    long long maxShotMs() const { return static_cast<long long>(m_maxShotSec) * 1000; }
    long long holdMs()        const { return static_cast<long long>(m_holdSec) * 1000; }
    long long incidentMaxMs() const { return static_cast<long long>(m_incidentMaxSec) * 1000; }

    // Settings (tunable live from the Director tab)
    // Off by default (opt-in): the status button is shown by default for discoverability
    // (one click enables, and the choice persists), but auto-direction stays off so an
    // upgrade doesn't silently take over every spectator's camera or override the
    // pre-existing click-to-spectate. Broadcasters enable it once.
    bool m_enabled = false;
    int m_minShotSec = 8;       // hold a shot at least this long (broadcast-like floor)
    // Off by default (like the director itself being opt-in): a caster who enables the
    // director has a rider on screen already, and the least surprising thing it can do is
    // cover the stories and hand that shot back - not start touring the field. Turning it
    // on is one click for anyone who wants the full auto-broadcast. Existing installs are
    // unaffected: [Director] is written unconditionally, so a saved 25 stays 25.
    int m_maxShotSec = 0;       // force variety after this long (0 = Off, see setMaxShotSec)
    int m_battleGapMs = 2500;   // adjacent gap (ms) that counts as a battle
    int m_battleMaxPos = 10;    // ignore battles whose leader is beyond this position (0 = no limit)
    int m_manualResumeSec = 5;  // reclaim control this long after the caster goes manual (0 = off)
    bool m_gamepadTakeover = true;   // stick push grabs Free-Roam + pauses (on by default)
    // Variety-camera pool (defaults: a tasteful set; the awkward ones off).
    bool m_camFront = true;     // Front Fender
    bool m_camRear = false;     // Rear Fender
    bool m_camHelmet = true;    // Helmet 1 (on-head POV)
    bool m_camHelmet2 = false;  // Helmet 2 (side cam)
    bool m_camForks = false;    // Forks (suspension)
    // Story toggles (sensible defaults; opt out per broadcast).
    bool m_followBattles = true;     // let close on-track battles steer the camera
    bool m_followIncidents = false;  // crashes interrupt and hold (off by default)
    bool m_followFastestLap = true;  // flash to a new overall fastest lap
    bool m_finishLock = true;        // lock to the front on the final lap
    bool m_catchOvertakes = true;    // reward a rider who just completed an on-track pass
    bool m_followLappers = false;    // follow a front-runner working through backmarkers (opt-in)
    bool m_followDrops = false;      // follow a rider tumbling down the order (opt-in)
    bool m_followPace = true;        // cut to a rider on a session-best split (hot lap; both modes)
    // Timing knobs.
    int m_varietyEvery = 3;          // every Nth cut dips to an onboard
    int m_holdSec = 5;               // shared story hold (incidents + fastest lap)
    int m_incidentMaxSec = 12;       // incident hold cap (INI-only, >= hold)

    // Runtime state
    int m_currentSubject = -1;          // rider the director intends to follow
    int m_homeSubject = -1;             // rider the BROADCASTER put on camera (see getHomeSubject)
    int m_currentPartner = -1;          // other rider in the current battle shot (-1 = none)
    int m_currentShotType = SHOT_SOLO;  // kind of the current shot (ShotType)
    bool m_currentIsBattle = false;     // pickShot framed this shot as a pair (see cutTo)
    int m_currentCameraRole = -1;       // last camera role requested (for the status HUD)
    long long m_shotStartMs = 0;        // when the current shot began
    long long m_lastDecisionMs = 0;     // last time evaluate() actually ran
    bool m_wasPaused = true;            // prior decision was a pause -> next live eval re-seeds (no stale cut)
    int m_lastSessionGen = -1;          // last seen SessionData::sessionGeneration (reset only when it changes)
    long long m_pendingUntilMs = 0;     // grace for a just-requested cut to land
    long long m_manualOverrideUntilMs = 0;  // user took control; director yields
    long long m_manualSinceMs = 0;      // when the caster entered a manual camera (0 = not manual)
    long long m_lastInputActivityMs = 0;  // last controller activity while hand-flying
    bool m_takeoverActive = false;      // current manual cam was grabbed via gamepad takeover
    bool m_reclaiming = false;          // resume fired -> next evaluate() makes a fresh cut past the manual cam
    long long m_lastManualPollMs = 0;   // throttle for pollManualControl()
    bool m_riderLock = false;           // rider lock: pin the director to the current rider (the DIRECTOR_LOCK hotkey)
    int m_shotCount = 0;                // counts all cuts (drives the variety cadence)
    int m_lastAirtimeNum = -1;          // last rider dipped to during a lull (round-robin cursor; -1 = none yet)
    int m_onboardRotation = 0;          // cycles through eligible variety cams

    // Shared story-hold latch (incident / fastest-lap / non-race pace). The held rider
    // is always m_currentSubject (the story cut to it); m_holdStartMs is read only by the
    // incident extend/cap. armHold()/holdActive() own this; see their declarations above.
    int m_holdShotType = SHOT_SOLO;     // which story is currently holding the shot
    long long m_holdStartMs = 0;        // when the current hold's shot fired (for the incident cap)
    long long m_holdUntilMs = 0;        // hold the current story shot until this time (0 = no hold)

    // Story-toggle runtime state.
    std::unordered_map<int,int> m_prevCrashCount;  // per-rider crash count, for rising-edge detection
    int m_prevBestOverallMs = -1;       // previous overall fastest lap (ms), for new-PB detection
    int m_hazardLatchSubject = -1;      // subject we already fired a hazard-incident for (edge latch)

    // Overtake detection runtime state.
    std::unordered_map<int,int> m_prevPosition;  // per-rider official position last eval (for swap detection)
    int m_overtakeSubject = -1;         // rider rewarded for a just-completed pass
    int m_overtakePartner = -1;         // the rider just passed (framed behind the overtaker)
    int m_overtakeGained = 1;           // how many riders the overtaker passed in the move
    long long m_overtakeUntilMs = 0;    // boost the overtaker's score until this time
    // Drop detection runtime state (rider tumbling down the order). A rolling window
    // baseline captures each rider's position; a rider whose position has worsened by
    // >= the threshold since the baseline is "dropping". m_dropLost is the positions lost.
    std::unordered_map<int,int> m_dropBasePos;  // per-rider position at the window baseline
    long long m_dropWindowStartMs = 0;  // when the current drop window baseline was taken
    int m_dropSubject = -1;             // rider rewarded for a fresh tumble
    int m_dropLost = 0;                 // positions lost in the move (for the caption)
    long long m_dropUntilMs = 0;        // boost the dropping rider's score until this time
    int m_lapperSubject = -1;           // front-most lapper this eval (for the SHOT_LAPPER tag)
    int m_finishedWinnerNum = -1;       // leader registered as finished (winner-celebration edge; also
                                        // what tells emitCutEvent which SHOT_FINISH cut is the actual win)
    long long m_winnerHoldUntilMs = 0;  // hold on the winner this long after they finish, then move on

    // "Pace" runtime state. The director cuts to a rider who has just beaten the
    // session-best INDIVIDUAL sector time (S1, S2 or S3) mid-lap - "on pace" content.
    // m_bestSectorMs[i] is the best individual time seen in sector i (0=S1, 1=S2, 2=S3)
    // across the session, derived from the live cumulative splits (S2 = split2-split1,
    // S3 = split3-split2). A full lap (all three) is the fastest-lap path's job, not a
    // sector. Reset on a new session like the other baselines.
    int m_bestSectorMs[3] = { -1, -1, -1 };  // session-best individual sector ms (S1/S2/S3)
    int m_currentPaceSplit = -1;             // sector index the active pace shot is up on (0=S1, 1=S2, 2=S3)

    // Event-log transparency: a canonical key ("shot:riderA:riderB", rider pair unordered)
    // for the last director cut logged, so a camera refresh on the SAME shot/subject (a
    // max-shot dip, or a battle variety dip that swaps subject<->partner) isn't logged twice
    // in a row. Cleared on a state change and on session reset so the next cut logs fresh.
    char m_lastCutKey[32] = "";
};
