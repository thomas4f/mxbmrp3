// ============================================================================
// core/spotter_manager.h
// Audio "spotter": speaks short race callouts over the game audio.
//
// WHERE THE PIECES ARE. This class is the hub; the parts worth reading
// first are the pure headers it drives, each with its own tests:
//   spotter_phrase.h     event -> words, and the cue CATEGORIES (filed by
//                        who a cue is about — see its header)
//   spotter_cue_pack.h   the pack format: the spec a pack author reads
//   spotter_mix.h        the chunk mixer behind dynamic callouts
//   spotter_hazard.h     proximity/alongside/blue-flag/hazard edges
//   spotter_milestones.h "ten minutes to go", halfway_point
//   spotter_pace.h       gaps to the riders ahead/behind, with trends
//   spotter_tts_voice.h  picking a Windows voice without leaving the game
//
// THREE OUTPUT PATHS, all zero-dependency Windows built-ins, tried in this
// order per cue (see emitCue): a pack's stitched MIX (chunks assembled by
// spotter_mix.h and played from memory), a pack's whole WAV, or SAPI TTS.
//
// The BUNDLED pack (`default`) is text only, so out of the box every cue
// lands on the TTS rung; packs with recorded audio are a separate download
// and light up the two rungs above it. Worth knowing when reading the
// ladder: SAPI does not exist under Wine/Proton, so on those systems the
// default install is silent and a recorded pack is what makes it audible.
//
// THREADING. All audio happens on one worker thread, started lazily on the
// first cue, so the 480fps game thread never touches COM, disk, or a speech
// engine. say()/playWav() just enqueue under a mutex and notify (game thread
// only — the event taps and the Draw path). Speech cues are executed one at
// a time in order (a spotter talking over itself is noise); wav cues are
// fire-and-forget via SND_ASYNC — winmm plays on its own internal thread,
// and a subsequent PlaySound cancels the previous one (single channel).
//
// LIMITS THAT ARE THE BACKEND'S, NOT A DESIGN CHOICE: PlaySound gives one
// channel and no ducking, so a cue interrupts the cue before it. Moving wav
// playback to XAudio2 would fix both; the queue and this API are shaped so
// only the worker's playback calls would change.
//
// The [Spotter] volume reaches ALL THREE rungs, not just TTS: SAPI takes it
// via SetVolume, and both wav paths are scaled sample-by-sample on the way to
// the device (SpotterMix::applyGain), because PlaySound has no per-sound
// volume of its own. A file our RIFF reader rejects is played unscaled rather
// than dropped, which is the one case the slider does not reach.
//
// IT ONLY ATTENUATES, and that is a known gap rather than an oversight: the
// slider is 0..100 and 100 is a no-op, so a player who cannot hear the spotter
// over the engine has nothing to turn UP -- which is what was reported against
// 1.29.1. Raising the ceiling is cheap for the wav rungs (applyGain already
// clamps) and is NOT cheap for TTS: SAPI's SetVolume is a USHORT capped at 100
// and waveOutSetVolume cannot exceed unity either, so boosting speech means
// rendering SAPI into a memory stream and gaining that. Worth knowing before
// anyone "just widens the slider": the BUNDLED pack is text-only, so the
// default install is exactly the case a wav-only boost would not help.
//
// SHUTDOWN follows the project invariants: the worker is joined by the
// orchestrated PluginManager::shutdown() (never by the destructor — joining
// at DLL detach deadlocks on the loader lock); the destructor is the
// no-Shutdown()-teardown backstop and uses the shared spinThenDetach.
// A speech in progress is interrupted within ~50ms (the worker polls the run
// flag while SAPI speaks asynchronously), so quitting the game never waits
// out a sentence.
// ============================================================================
#pragma once

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <string>
#include <thread>
#include <vector>

#include "spotter_cue_pack.h"
#include "spotter_hazard.h"
#include "spotter_milestones.h"
#include "spotter_pace.h"
#include "spotter_phrase.h"
#include "spotter_queue.h"
#include "spotter_stretch.h"
#include "spotter_vars.h"
#include "thread_safety.h"

namespace Unified { struct TrackPositionData; }

// One spoken (or subtitle-only) cue, kept in the history ring for the
// subtitle widget and the headless tests.
struct SpotterLogEntry {
    std::string text;
    SpotterPhrase::Category category = SpotterPhrase::Category::General;
    int sessionTimeMs = 0;
};

class SpotterManager {
public:
    static SpotterManager& getInstance();

    // ---- event intake (game thread) ----------------------------------------
    // Called from PluginData::addEventLogEntry for every detected race event.
    // Gates on enabled + category toggle, composes the phrase, records it in
    // the cue log and queues the audio. Cheap when disabled: one atomic load.
    // focusedRaceNum is the rider the callouts are ABOUT ("you" phrasing):
    // the spectate target when spectating, else the player — the tap site
    // resolves it so this stays decoupled from PluginData.
    // `nums` carries whatever the event's own handler knew as a NUMBER (see
    // EventNumbers). No display string reaches here: the event log's message
    // and detail columns are for the race feed, and this used to parse three
    // of them back into the numbers they were formatted from.
    void onRaceEvent(EventLogType type, int raceNum, int focusedRaceNum,
                     int sessionTimeMs, const EventNumbers& nums = {});

    // Called from the track-position handler each batch (many/sec while on
    // track). Runs the proximity/hazard detectors for the display rider:
    // "rider behind"/"clear" from the batch's positions, blue-flag and
    // hazard-ahead edges from PluginData's existing caches. One atomic-free
    // bool test when the spotter is disabled.
    void onTrackPositions(int numVehicles,
                          const Unified::TrackPositionData* positions);

    // Called from the race-lap handler for EVERY rider's completed lap.
    // Drives the focused rider's position report (the pit-board moment) and,
    // in pure lap races, the leader-lap halfway_point milestone.
    //
    // lapValid is the game's own verdict on the lap just finished, and it is
    // the ONLY signal there is: the cutting flag rides RaceCommunication,
    // which a practice session never sends because it issues no penalties. So
    // an invalidated practice lap is knowable at the line and not a moment
    // sooner — which is exactly where lap_invalidated speaks.
    void onRaceLapCompleted(int raceNum, int completedLaps, int lapTimeMs,
                            bool lapValid = true);

    // Called from the race-lap handler when a lap beats the rider's SESSION
    // best without being an all-time PB — the third tier of the ladder the
    // handler already computes (all-time PB > fastest lap > session PB). The
    // spotter tapped the top two and dropped this one, which is the most
    // common good-news moment there is in practice.
    void onSessionBest(int sessionTimeMs, int lapTimeMs);

    // Called from the track-position path on the rising edge of a rider's
    // crash flag — the same edge PluginData's per-session crash counter uses.
    void onRiderCrash(int raceNum, int focusedRaceNum, int sessionTimeMs);

    // Called when the spectate target changes: "now watching rider N", the one
    // cue that is about the CAMERA rather than the race.
    void onSpectateTarget(int raceNum);

    // The Spotter Cue hotkey: speak whatever the pack defines as
    // `hotkey_triggered`. Nothing is built in, so this is silent until
    // somebody writes that line — which also makes it the on-demand way to
    // hear a template you are editing.
    void speakHotkeyCue();

    // Called from the classification handler on the gate-drop edge PluginData
    // already detects for the lap timer. A standing start holds on the grid
    // AFTER the session flips to running, so this is a later, distinct moment
    // from session_started — and one practice never has.
    void onGateDrop();

    // The session's state, pushed at the moment it changes rather than read
    // back from PluginData: the handler stores it only AFTER logging the
    // change (the store is what its own change-detection compares against),
    // so a cue firing on that transition would otherwise report the state
    // being left rather than the one being entered.
    void onSessionState(int sessionState);

    // Called from the classification handler once the order has been rebuilt.
    // Speaks BOTH deferred cues, deferred to the same moment for different
    // reasons:
    //   - the lap report armed at your crossing, because position and the gaps
    //     it reads only include that lap after this point (emitting at the
    //     crossing reported the standings from before it);
    //   - the held fastest lap, because a classification cannot arrive inside
    //     a callback batch, which is what separates a join's replayed lap
    //     history from laps that just happened. See PendingFastest.
    void flushDeferredCues();

    // Called from the race-lap handler at the moment a lap beats the
    // player's stored all-time PB (the same edge that raises the on-screen
    // notice — the handler owns the edge, so no latch is needed here, and
    // the consumable notice flags stay untouched for NoticesHud).
    void onPersonalBest(int sessionTimeMs, int lapTimeMs);

    // Called from the race-split handler for EVERY rider's split crossing.
    // Feeds the pace tracker: the focused rider's own points, and the
    // resolution point for a pending behind-gap report (spotter_pace.h).
    void onRaceSplit(int raceNum, int lapNum, int splitIndex, int splitTimeMs);

    // Proximity/hazard cue tuning (clamped). Lateral is on the settings tab —
    // it is the one a player feels; the rest stay [Spotter] INI keys.
    void setBehindOnMeters(float m);
    void setClearMeters(float m);
    void setAlongsideOnMeters(float m);
    void setAlongsideAheadMeters(float m);
    void setAlongsideClearMeters(float m);
    void setLateralMeters(float m);
    void setBehindRepeatMs(int ms);
    void setClearMinEpisodeMs(int ms);
    void setBlueFlagCooldownMs(int ms);
    void setLappingCooldownMs(int ms);
    void setHazardCooldownMs(int ms);
    const SpotterHazard::Config& hazardConfig() const { return m_hazardCfg; }

    // How far up you must be at the last split before the on_pace_* cues fire.
    // Without a floor they go off on most laps of any decent run, which is the
    // difference between a spotter and a metronome. INI-only, like the
    // proximity distances above.
    void setOnPaceMarginMs(int ms) {
        m_onPaceMarginMs = std::clamp(ms, 0, 10000);
    }
    int getOnPaceMarginMs() const { return m_onPaceMarginMs; }

    // ---- settings ([Spotter] INI section; game thread) ---------------------
    // Enabled/subtitles/categories are game-thread-only state (event tap,
    // settings UI, INI load all run there); the audio worker never reads
    // them. Volume/rate ARE read by the worker, via published atomic copies
    // (publishAudioSettings) so the settings tab's stepper can bind plain
    // int pointers like every other stepped control.
    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool on) { m_enabled = on; }
    bool isSubtitlesEnabled() const { return m_subtitles; }
    void setSubtitlesEnabled(bool on) { m_subtitles = on; }
    bool isCategoryEnabled(SpotterPhrase::Category cat) const {
        return (m_categoryMask & (1u << static_cast<unsigned>(cat))) != 0;
    }
    void setCategoryEnabled(SpotterPhrase::Category cat, bool on);
    uint32_t getCategoryMask() const { return m_categoryMask; }
    void setCategoryMask(uint32_t mask) { m_categoryMask = mask; }
    // Volume 0..100 maps straight onto SAPI's scale. SPEED is a multiplier
    // (1.0 = as recorded) rather than SAPI's -10..10, because it has to mean
    // the same thing to BOTH backends: the wav paths time-stretch by it
    // (pitch-preserving, spotter_stretch.h) and SAPI gets it mapped back
    // onto its own integer scale. The worker applies both per utterance, so
    // a change lands on the next cue.
    int getVolume() const { return m_volume; }
    void setVolume(int v);
    float getSpeed() const { return m_speed; }
    void setSpeed(float s);
    // SAPI's integer rate for a multiplier: its scale is roughly
    // exponential with 10 ~ 3x, so this is the inverse of 3^(rate/10).
    static int sapiRateForSpeed(float speed);
    // Clamp the plain members and push them to the worker-facing atomics.
    // Called by the setters and by the settings tab after a stepper writes
    // through the raw pointers below.
    void publishAudioSettings();

    // Raw pointers for the settings tab's generic toggle/stepper/checkbox
    // machinery (same long-lived-member lifetime contract as HUD pointers).
    //
    // NO enabledPtr()/subtitlesPtr(), and not merely because nothing called
    // them: writing those two through a raw pointer would SKIP setEnabled() and
    // setSubtitlesEnabled(), which is where the audio settings are republished
    // to the worker and the pack is (re)loaded. The three below are plain values
    // the publish step reads afterwards; the two masters are not. The tab drives
    // them through the setters.
    uint32_t* categoryMaskPtr() { return &m_categoryMask; }
    int* volumePtr() { return &m_volume; }
    float* speedPtr() { return &m_speed; }
    // The proximity gate's three distances, for the settings steppers.
    // Stepped in place like the others; each setter's clamp is the same range
    // its stepper enforces, so a hand-edited INI and a click agree.
    //
    // behindOn and alongsideOn each own a hysteresis band with their release
    // threshold, and only the setters know that contract — so their steppers
    // re-enter the setter from postStep rather than trusting the raw write.
    // Deliberately no derivation of the release from the trigger: shortening
    // the trigger is how you ask for FEWER calls, and dragging the release
    // down with it would hand back more behind/clear pairs instead.
    float* lateralMetersPtr() { return &m_hazardCfg.lateralMeters; }
    float* behindOnMetersPtr() { return &m_hazardCfg.behindOnMeters; }
    float* alongsideOnMetersPtr() { return &m_hazardCfg.alongsideOnMeters; }
    // The forward half of the alongside window. No hysteresis partner of its
    // own (the release band is symmetric), so this one steps in place.
    float* alongsideAheadMetersPtr() { return &m_hazardCfg.alongsideAheadMeters; }

    // Speak one fixed demonstration line through whatever pack and voice are
    // selected right now — what the settings menu plays when you cycle either,
    // so choosing a voice is a listening decision rather than a guess from its
    // folder name. Deliberately the SAME line every time: it is a comparison
    // between voices, so the only thing that may change is the voice.
    //
    // ttsOnly skips the pack's audio and speaks through Windows instead. The
    // TTS-voice cycler needs it: with a pack selected the ladder would play the
    // pack's clips, and cycling Windows voices would sound identical every time
    // — a voice_preview that demonstrates the wrong thing is worse than none.
    void previewVoice(bool ttsOnly = false);

    // ---- cue pack ([Spotter] pack=; game thread) ----------------------------
    // A pack overrides phrases / maps wavs per cue; see spotter_cue_pack.h for
    // the format. A missing or unreadable pack degrades to the SHIPPED
    // pack's wording WITHOUT rewriting the stored name (the
    // stored-by-name invariant: restoring the folder restores the choice).
    void setPackName(const std::string& name);
    const std::string& getPackName() const { return m_packName; }
    // Re-read the active pack from disk — the RELOAD_CONFIG hotkey path, so a
    // pack author can edit a phrase and hear it without restarting.
    void reloadCuePack();
    // Directory names under mxbmrp3_data/spotters/, sorted (tab picker).
    std::vector<std::string> listAvailablePacks() const;

    // The active pack's human title for the picker: its optional [pack] name,
    // else the folder name. Never the identity -- getPackName() is that, and is
    // what gets stored and cycled.
    const std::string& getPackDisplayName() const { return m_packDisplayName; }

    // ---- TTS voice ([Spotter] tts_voice=; game thread) ---------------------
    // Which Windows voice speaks when no pack is selected (or a cue falls
    // back to TTS). Empty = the system default. Stored by NAME and resolved
    // against the live list, so an uninstalled voice degrades to the default
    // without rewriting the setting (see spotter_tts_voice.h).
    const std::string& getTtsVoice() const { return m_ttsVoice; }
    void setTtsVoice(const std::string& name);
    // Installed SAPI voice names, sorted (tab picker). Reads the registry —
    // cheap (a handful of keys) and only called by the settings UI. Empty on
    // a machine with no SAPI voices, which includes every Wine prefix.
    std::vector<std::string> listTtsVoices() const;

#if defined(MXBMRP3_TEST_BUILD)
    // Inject pack CONTENT directly (parsed from text) — the harness stages no
    // asset tree, same rationale as AssetManager::installSyntheticTheme. The
    // pack dir is set to `dir` so wav cue paths are observable if needed.
    void testInstallPack(const std::string& iniText, const std::string& dir) {
        m_pack = SpotterCuePack::parse(iniText);
        m_packDir = dir;
    }
#endif

    // ---- cue log (subtitle widget + tests; any thread) ---------------------
    // Copies under the lock — callers keep the snapshot.
    std::deque<SpotterLogEntry> getCueLog() const;
    // Just the newest line, which is all the subtitle widget wants. getCueLog()
    // copies the whole 96-entry ring of std::string under the lock to reach
    // .back() — once per cue rather than per frame, so never a budget problem,
    // but a copy of ninety-five strings nobody reads.
    std::string getLatestCueText() const;
    uint32_t getCueLogRevision() const { return m_cueLogRevision.load(std::memory_order_relaxed); }

    // Queue a phrase for TTS (UTF-8). Non-blocking; starts the worker on
    // first use. Drops the oldest pending cue if the queue is full.
    // `perishable` marks a cue whose value is that it describes NOW — the
    // proximity calls. The queue drops those rather than speaking them stale
    // (spotter_queue.h); everything else waits its turn.
    void say(const std::string& utf8Text, bool perishable = false);

    // Queue a .wav file (path relative to the game working directory, e.g.
    // "plugins/mxbmrp3_data/spotters/am_michael/clear.wav"). Non-blocking.
    void playWav(const std::string& path, bool perishable = false);

    // Join the worker and stop any playing audio. Called from the
    // orchestrated PluginManager::shutdown() only.
    void shutdown();

private:
    SpotterManager() = default;
    ~SpotterManager();
    SpotterManager(const SpotterManager&) = delete;
    SpotterManager& operator=(const SpotterManager&) = delete;

    void enqueue(SpotterCue cue);
    void workerThread();

    // Shared emission path for event cues and detector cues: pack phrase
    // override / explicit mute / mix / wav resolution, cue log, then audio.
    // riderNum/timeComposed/timeTenths/penaltySecs are the numeric
    // placeholder values the chunk mixer needs (-1 = no value; a mix
    // needing one falls back — except {penalty_seconds}, optional by design).
    // `vars` carries only what the EVENT knows (rider, time, secs, laps);
    // emitCue fills the ambient half — your position, gaps, the clock — from
    // PluginData so every variable works in every cue (spotter_vars.h). The
    // numeric trailer is the wav mixer's, which can only stitch values it has
    // number chunks for and so takes them as numbers rather than words.
    void emitCue(const char* key,
                 SpotterPhrase::Category cat, SpotterVars::Vars vars,
                 int sessionTimeMs, int riderNum = -1, int timeComposed = -1,
                 int timeTenths = -1, int penaltySecs = -1,
                 int lapsLeft = -1, int posValue = -1);

    // Fill the always-available half of `vars` from live state. Called by
    // emitCue for every cue; separate so the voice_preview can use it too.
    void fillAmbientVars(SpotterVars::Vars& vars) const;

#if defined(MXBMRP3_TEST_BUILD)
public:
    // Which route the last cue took, as "<key>|<route>": "mix:a.wav+b.wav",
    // "wav:green.wav", "tts", "silent" (it resolved to nothing at all) or
    // "muted" (its category is off). FIVE, not the three this said while the
    // last two were being added — every exit FROM emitCue records, so a value here
    // is this cue's answer and never the previous one's.
    //
    // FROM emitCue, which is the limit worth knowing: an emitter that gates before
    // calling it leaves this field alone, so a cue suppressed upstream (a
    // detector's own early-out) still reads as whatever last reached here. And
    // "muted" keys on the cue KEY while the audible values key on the chosen
    // VARIANT, because the category gate fires before a variant is picked.
    //
    // The cue LOG carries only the words, so nothing else headless can tell a
    // recorded clip from the TTS that stands in for it when the lookup misses —
    // which is the difference between a pack working and being inaudible on
    // Wine/Proton.
    const std::string& testLastAudioRoute() const { return m_lastAudioRoute; }

    // Force the variant pick: -1 (the default) rolls, 0 always takes the base
    // key, N takes the Nth alternate and clamps to what the pack has.
    //
    // WHY A TEST NEEDS THIS. The shipped pack carries alternates, so a cue's
    // wording is a coin toss — and most of these tests assert the exact words
    // that reached the cue log, against the shipped file on purpose. Without a
    // pin, adding a `_2` to any cue silently turns every assertion on it into
    // a one-in-N flake, which is the worst possible way to find out.
    //
    // Pinned to 0 rather than seeding the RNG: the base row is the cue's
    // canonical wording, so a test that pins reads as "the shipped words for
    // this cue", and stays true when an alternate is added later. Seeding
    // would make the answer depend on how many variant picks happened earlier
    // in the process, which changes whenever any other case moves.
    void testPinVariant(int idx) { m_pinVariant = idx; }

    // See m_workerParked. A test that wants the worker in its steady state
    // enqueues a cue and then polls this - the flag starts false, so polling
    // before the first cue would read "not yet started" as "not parked".
    bool testWorkerParked() const {
        return m_workerParked.load(std::memory_order_acquire);
    }
private:
    std::string m_lastAudioRoute;   // mt-plain: game thread only (emitCue)
    int m_pinVariant = -1;          // mt-plain: game thread only (emitCue)
#endif

    // Wipe everything a session must not carry into the next. Called on
    // SessionStarted — including while the spotter is switched OFF, which is
    // the whole reason it is a function; see both call sites.
    void resetSessionState();

    // Settings. BOTH default off — cue intake runs when either is on
    // (subtitles-only is a real mode), so a default-on subtitle switch would
    // make callout text appear after an upgrade unasked, the text version of
    // the audio jump scare. m_enabled gates only the audio dispatch.
    bool m_enabled = false;    // mt-plain: game thread only (tap, UI, INI)
    bool m_subtitles = false;  // mt-plain: game thread only (tap, UI, INI)
    // Active cue pack (game thread only): the persisted NAME, the parsed
    // content, and the on-disk folder wav filenames resolve against.
    // Defaults to the SHIPPED pack rather than empty so the out-of-box
    // WORDING is a file people can read and edit (the shipped pack is text
    // only — see the header note on the audio ladder; recorded packs are a
    // separate download). HudManager::initialize() loads it after the
    // settings pass; a missing folder degrades to the shipped pack's wording
    // without rewriting the name, per the stored-by-name invariant.
    std::string m_packName = "default";
    std::string m_packDisplayName;  // game thread only; label for the picker
    SpotterCuePack::Pack m_pack;
    std::string m_packDir;
    // Proximity/hazard detector state + tuning (game thread only).
    SpotterHazard::Detector m_detector;
    SpotterHazard::Config m_hazardCfg;
    // Held true only across the lap report's own emitCue, so fillAmbientVars
    // leaves {last_lap_time} empty for it -- see flushDeferredCues. A flag
    // rather than clearing the field, because fill() only writes an EMPTY
    // field, so clearing it is exactly what invites the refill.
    bool m_lapReportHideTime = false;   // mt-plain: game thread only
    // Held true across the deferred-report flush (lap_invalidated,
    // lap_completed, position_gained/lost), so fillAmbientVars serves NO
    // cached neighbour gap or trend there: those readings are up to a lap old
    // at a crossing, and the report presented one as the crossing's fact --
    // then the fresh gap cue resolved seconds later with a different number.
    // The crossing's own measureAheadGap values ride the vars and are kept.
    bool m_lapReportFreshGapsOnly = false;  // mt-plain: game thread only
    // Minimum margin for the on_pace_* cues (game thread only). 0.2s: below
    // that the remaining sector decides it, so the call would be noise.
    int m_onPaceMarginMs = 200;
    // Session-progress milestones (game thread only).
    SpotterMilestones::State m_milestones;
    // Pace reports (game thread only). The tracker holds every rider's recent
    // timing-point crossings itself (see spotter_pace.h) — not PerRider<>,
    // which is PluginData's registry and this is not PluginData, so entries
    // are not evicted when a rider leaves. Three things bound what a stale one
    // can do: it is cleared at every session start, it is overwritten on that
    // number's next crossing, and a gap is only produced when a stored (lap,
    // point) key equals the one the focused rider just crossed. A departed
    // rider also drops out of the classification order, so they cannot be
    // picked as the neighbour at all. The residual case is the project's known
    // race-number reuse (CLAUDE.md): a NEW rider takes a departed number, is
    // immediately the rider ahead, and has not yet crossed a point — then one
    // gap could be measured against the old rider's crossing. It is one cue,
    // on one lap, within the tracker's 30s cap; evicting from here would mean
    // a tap on PluginData::removeRaceEntry, which is not worth that.
    SpotterPace::Tracker m_pace;

    // The stopwatch gap to the rider ahead at ONE of your timing points, into
    // the variables the cue for that point will speak. Called from both places
    // a timing point exists — your S/F crossing and each split — which is why
    // it is a function rather than the same fourteen lines twice.
    // `myPosition`/`order` are the classification at that instant; a missing
    // measurement leaves the variables empty, so the optional group drops.
    void measureAheadGap(int myPosition, const std::vector<int>& order,
                         long long pointKey, int nowMs, SpotterVars::Vars& out);

    // Emit the gap half of a pace report ("Ahead, two point one, gaining.").
    void emitGapCue(const SpotterPace::Gap& gap, int sessionTimeMs);
    // Variant-pick RNG (xorshift32; game thread only). Seeded non-zero —
    // xorshift's one fixed point is 0.
    // The last resolved pace report either side, so {trend_ahead} /
    // {gained_on_ahead} can be read by ANY cue rather than only the gap cue that
    // produced them. Game thread only, like the tracker they come from.
    // Previous cumulative split on the focused rider's current lap, so a
    // SECTOR time can be recovered from the game's cumulative ones. Reset by
    // the lap number changing rather than by a lap event, so a mid-lap join
    // simply skips the first sector instead of reporting a whole-lap figure.
    int m_lastSplitLap = -1;
    int m_lastSplitRider = -1;
    int m_lastSplitCumMs = 0;

    // Fuel warnings are once-per-session downward edges; re-armed on a green
    // flag with the other once-per-session state.
    int m_sessionState = 0;   // see onSessionState

    // The lap crossing's report, measured then, spoken once the
    // classification catches up (see flushDeferredCues). vars carries what the
    // crossing knew and standings do not — the stopwatch gap to the rider
    // ahead.
    struct PendingLap {
        bool armed = false;
        int raceNum = -1;
        int lapTimeMs = -1;
        int nowMs = 0;
        bool lapValid = true;
        // A lap-quality cue (personal_best / record_beaten / session_best)
        // already spoke this lap's TIME at the crossing. Carried rather than
        // read at flush time because the ladder latch is cleared at the top of
        // onRaceLapCompleted, long before the report flushes.
        bool timeAlreadySpoken = false;
        SpotterVars::Vars vars;
    } m_pendingLap;

    // The fastest lap of the session, held one classification tick before it
    // speaks — and ONE slot, not one per rider, which is the whole point.
    //
    // Joining a lobby mid-session replays every rider's whole lap history in a
    // single instant, and each lap that improved the overall best fired a cue:
    // a real join announced rider ten three times ("one twenty two point
    // five", "one thirteen point eight", "one oh three point six"), then two
    // more riders twice each — seven callouts in one millisecond, all of them
    // about laps set before the player was watching. Consecutive lap numbers,
    // same rider, same timestamp: a state sync, not seven moments.
    //
    // The replay arrives inside ONE callback batch, so "held since the last
    // classification" identifies it, with no notion of joining to detect and
    // no clock arithmetic. Comparing session clocks was the first version and
    // it WEDGES: getSessionElapsedTime returns a timed session's full length
    // until the first classification sets the clock, so a cue armed before
    // that waits forever for a bigger number. Ordering on a value that
    // legitimately jumps backwards was the wrong shape.
    //
    // That the game never delivers a classification INSIDE the replay is an
    // assumption about its delivery, not something the plugin controls, and it
    // is not one the logs can confirm (classifications are not logged
    // individually). It is taken because its failure is the MILD one: a
    // classification landing mid-replay splits it into two callouts instead of
    // one, where a clock guard's failure is a cue held silently forever. Two
    // is a bad day; never is a bug.
    //
    // Because the game only raises the event when the OVERALL best improves,
    // the last one in the replay is the session's fastest — so a single slot
    // that the newest arrival overwrites collapses the whole history to the
    // one line worth hearing. In normal running only ever one is pending, so
    // nothing changes but up to a second of latency on a cue nobody acts on.
    struct PendingFastest {
        bool armed = false;
        int raceNum = -1;
        int focusedRaceNum = -1;
        int nowMs = 0;
        EventNumbers nums;
    } m_pendingFastest;
    // Set while flushing the above, so the re-entry emits instead of re-arming.
    bool m_emittingPendingFastest = false;  // mt-plain: game thread only

    // The session wrap-up, held one classification when a RACE completes
    // before the subject's own finish reaches us. For the WINNER the game
    // delivers [SessionComplete, RiderFinished] in one batch, which spoke
    // "That's Race 2 done, P one" and THEN "That's the flag, P one" — the
    // flag after the wrap-up, position twice. Held to the next classification
    // (the lap report's pattern), the finish cue speaks first. All game
    // thread only, reset with the session.
    struct PendingSessionEnd {
        bool armed = false;
        int raceNum = -1;
        int nowMs = 0;
        EventNumbers nums;
    } m_pendingSessionEnd;
    bool m_emittingPendingSessionEnd = false;  // mt-plain: game thread only
    // Once per session: RACE_OVER and FINISHED both map to SessionComplete.
    bool m_sessionEndSpoken = false;           // mt-plain: game thread only
    // finished_you spoke this session, so the wrap-up withholds {position}
    // rather than reading the same number back two breaths later.
    bool m_finishSpokenFocused = false;        // mt-plain: game thread only
    bool m_sessionEndHidePosition = false;     // mt-plain: game thread only
    // Cut the utterance in progress short. Set by a settings click (the voice
    // preview replaces whatever is speaking), consumed by the worker's polling
    // loop, which is the only thing that can purge SAPI mid-sentence.
    std::atomic<bool> m_interruptSpeech{false};
    // Speak the held lap. Called only from flushDeferredCues, which the
    // classification handler already drives — no new tap, and nothing on the
    // render path.
    void flushPendingFastestLap();

    // The fuel warning's edge, so it fires on the way down and once each.
    // Written by onRaceLapCompleted (a game callback) and reset by the
    // session-started cue on the same thread; the speech worker never sees
    // them.
    // Your penalties this session, as the spotter has announced them. The
    // standings column is authoritative but a beat behind the penalty cue that
    // needs it — see the Penalty branch in onRaceEvent. Game thread only.
    int m_penaltyTotalMs = 0;
    // Up between a penalty CLEAR/REVISION and the next classification: the
    // standings column still holds the pre-revision figure in that window,
    // and max()ing against it resurrects a total the game just took back.
    bool m_penaltyColumnStale = false;  // mt-plain: game thread only
    bool m_fuelLowSaid = false;       // mt-plain: game thread only
    bool m_fuelCriticalSaid = false;  // mt-plain: game thread only

    // The lap-quality ladder that keeps "New personal best" and "Fastest lap"
    // from both reading out the same time. Holds the TIME of the lap that
    // already had its cue, not just that one did, so the suppression cannot
    // reach a different lap. Armed by onPersonalBest, read by the FastestLap
    // intake, cleared by onRaceLapCompleted.
    int m_higherLapCueTimeMs = -1;

    // True when the subject's best lap is their opening lap — a gate start or
    // an out-lap, so a fact but not a reference. Gates the best-lap
    // COMPARISONS only; see the definition for why it stops at the spotter.
    bool bestLapIsFirstLap() const;

    // True once the subject's own race has ended (flag, retirement, DSQ, DNS).
    // Gates the cues that describe an ongoing race of yours; see the definition
    // for what deliberately keeps talking.
    bool subjectRaceOver() const;

    // The last position spoken by the lap report, and whose it was. The
    // reference for position_gained/_lost — see flushDeferredCues for why neither
    // of PluginData's two position references answers this question.
    int m_lastReportedPos = 0;
    int m_lastReportedPosNum = -1;

    SpotterPace::Gap m_lastAhead;
    SpotterPace::Gap m_lastBehind;

    uint32_t m_rngState = 0x9E3779B9u;
    uint32_t m_categoryMask = 0x1F;  // game thread only; every category on
    int m_volume = 100;        // game thread only; SAPI SetVolume 0..100
    float m_speed = 1.0f;      // game thread only; playback multiplier
    // Worker-facing copies of volume/rate (the only settings the audio
    // thread reads), refreshed by publishAudioSettings().
    std::atomic<int> m_volumePublished{100};
    std::atomic<float> m_speedPublished{1.0f};
    // The active pack's `[Mix] gap_ms`, or the plugin default. Published
    // like the two above because the worker assembles the mix; refreshed by
    // reloadCuePack(), since it is a property of the pack, not a setting.
    std::atomic<int> m_mixGapPublished{60};
    // The chosen TTS voice (game thread) and the worker's copy. A string
    // can't ride an atomic, so the published one is guarded like the cue log
    // and copied under the lock at each utterance.
    std::string m_ttsVoice;
    // The chosen voice's DISPLAY NAME (empty = system default), which the
    // worker matches against a live SAPI enumeration before SetVoice. It was a
    // resolved registry key until an engine that produces its tokens through
    // an enumerator turned out to have no key at all — see the note on
    // setTtsVoice for why handing over a name is safe here and was not in the
    // markup version that first tried it.
    std::string m_ttsVoicePublished MXB_GUARDED_BY(m_mutex);

    // Cue history ring for the subtitle widget and headless assertions.
    // Revision counter lets the widget poll cheaply (one atomic) per frame.
    // Sized so a whole demo-tape session's cues fit (the transcript dump
    // reads the ring once at the end of a replay).
    static constexpr size_t kCueLogCapacity = 96;
    std::deque<SpotterLogEntry> m_cueLog MXB_GUARDED_BY(m_mutex);
    std::atomic<uint32_t> m_cueLogRevision{0};

    mutable Mutex m_mutex;
    SpotterCueQueue m_queue MXB_GUARDED_BY(m_mutex);
    std::condition_variable m_cv;

    // Lazy start/stop is game-thread only (enqueue via the Draw/hotkey path,
    // shutdown via PluginManager); the thread object itself is never touched
    // by the worker, so it needs no guard.
    // joined-by: shutdown() (PluginManager::shutdown); the destructor's
    // spinThenDetach is only the no-Shutdown() unload backstop.
    std::thread m_workerThread;
    std::atomic<bool> m_running{false};    // cleared to stop the worker
    // LATCHED at shutdown() and never cleared. m_running alone cannot say
    // whether the worker has not started yet or has already been joined, and
    // enqueue() must refuse the second case: restarting a worker after the
    // orchestrated join is the "must not outlive the DLL" shape.
    std::atomic<bool> m_shutdown{false};
    std::atomic<bool> m_finished{false};   // worker's last store; see dtor
    // True only while the worker is parked in m_cv.wait with an empty queue,
    // i.e. it has finished every lazy first-cue setup (CoInitializeEx, the
    // CoCreateInstance that LOADS the engine's DLLs) and is holding no state
    // mid-flight. Read by teardown_test to reach that state deliberately
    // instead of racing it: the loader lock a first CoCreateInstance needs is
    // the one the destructing thread holds, so a DLL unload landing inside it
    // detaches a worker that cannot observe m_running (see m_abandonComCleanup
    // below, which covers the teardown half of the same lock).
    std::atomic<bool> m_workerParked{false};
    // Set ONLY by the destructor (the unload-without-Shutdown() backstop),
    // never by shutdown(): tells the worker to SKIP its COM teardown
    // (pVoice->Release() + CoUninitialize) and store m_finished immediately.
    // Releasing a SAPI voice waits on the engine's own speech thread, and
    // CoUninitialize can unload COM DLLs -- both need the LOADER LOCK the
    // destructing thread is holding, so on that path the flag would never
    // land and spinThenDetach would detach a live thread into unmapped code
    // (thread_detach_grace.h requires the flag be stored within instructions
    // of the stop signal). The process is tearing the DLL down anyway; a
    // leaked voice object is free compared to that.
    std::atomic<bool> m_abandonComCleanup{false};

    // The last stitched callout's WAV bytes. PlaySound(SND_MEMORY|SND_ASYNC)
    // reads this WHILE playing, so it must outlive the call — it lives here
    // and is only replaced after the previous sound is stopped. Worker
    // thread only (created lazily, cleared after the shutdown join).
    std::vector<uint8_t> m_activeMixBuffer;
};
