// ============================================================================
// core/spotter_manager.cpp
// See the header for the design; this file is the worker thread and the two
// audio backends (SAPI TTS + winmm PlaySound).
// ============================================================================
#include "spotter_manager.h"

#include "fuel_estimate.h"
#include "plugin_data.h"
#include "plugin_utils.h"
#include "hud_manager.h"
#include "../hud/fuel_widget.h"
#if GAME_HAS_RECORDS_PROVIDER
#include "../hud/records_hud.h"   // the track record, MX Bikes only
#endif
#include "stats_manager.h"
#include "spotter_mix.h"
#include "spotter_tts_voice.h"
#include "thread_detach_grace.h"
#include "../diagnostics/logger.h"
#include <set>

#include <windows.h>
#include <sapi.h>
#include <mmsystem.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <deque>
#include <fstream>
#include <sstream>

namespace {

// SAPI class/interface GUIDs defined locally so neither toolchain needs
// sapi.lib (MSVC) or uuid quirks (mingw) — CoCreateInstance only needs the
// values, and these are ABI constants that cannot change.
const CLSID kCLSID_SpVoice = {0x96749377, 0x3391, 0x11D2,
                              {0x9E, 0xE3, 0x00, 0xC0, 0x4F, 0x79, 0x73, 0x96}};
const IID kIID_ISpVoice = {0x6C44DF74, 0x72B9, 0x4992,
                           {0xA1, 0xEC, 0xEF, 0x99, 0x6E, 0x04, 0x22, 0xD4}};
// ...and the token object, which is how a specific voice is selected.
const CLSID kCLSID_SpObjectToken = {0xEF411752, 0x3736, 0x4CB4,
                                    {0x9C, 0x8C, 0x8E, 0xF4, 0xCC, 0xB5, 0x8E, 0xFE}};
const IID kIID_ISpObjectToken = {0x14056589, 0xE16C, 0x11D2,
                                 {0xBB, 0x90, 0x00, 0xC0, 0x4F, 0x8E, 0xE6, 0xC0}};
// ...and the CATEGORY, which is how SAPI is ASKED for the voices rather than
// having its registry read. The difference is not academic: see
// enumerateTtsVoicesViaSapi.
const CLSID kCLSID_SpObjectTokenCategory = {0xA910187F, 0x0C7A, 0x45AC,
                                            {0x92, 0xCC, 0x59, 0xED, 0xAF, 0xB7, 0x7B, 0x53}};
const IID kIID_ISpObjectTokenCategory = {0x2D3D3845, 0x39AF, 0x4850,
                                         {0xBB, 0xF9, 0x40, 0xB4, 0x97, 0x80, 0x01, 0x1D}};

// Packs live next to the other user asset packs; file I/O is relative to the
// game working directory like the web root and the temp-hook wav path.
constexpr const char* kPackRoot = "plugins\\mxbmrp3_data\\spotters\\";
// The shipped pack, which is the ONLY place the spotter's wording lives —
// every other pack is an overlay on it. See reloadCuePack.
constexpr const char* kBasePackName = "default";

// Default join between stitched chunks, for a pack that does not state one.
// The offline demos used 60-90ms; the low end reads best once the radio
// compression flattens the joins. A pack tunes this — including negative, for
// overlapping joins — with `[Mix] gap_ms`; see spotter_cue_pack.h.
constexpr int kMixGapMs = 60;

std::string wideToUtf8(const wchar_t* wide) {
    if (!wide || !*wide) return std::string();
    const int need = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0,
                                         nullptr, nullptr);
    if (need <= 1) return std::string();
    std::string out(static_cast<size_t>(need - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, &out[0], need, nullptr, nullptr);
    return out;
}

// The same catalogue, ASKED FOR rather than read. SAPI's own enumeration
// returns voices that have no registry key at all — a third-party engine can
// register a token ENUMERATOR for the category and produce its voices on
// demand, which is exactly how the adapters that expose the Windows 11 neural
// voices to Windows 10 work. A logged Windows 10 machine had "Microsoft Ryan"
// listed by Narrator and absent from both hives:
//
//   reg query "HKLM\SOFTWARE\Microsoft\Speech\Voices\Tokens" /s /f Ryan
//   End of search: 0 match(es) found.
//
// so no walk could ever have found it. The walk stays as the fallback for the
// case this cannot serve — COM refusing to initialise — and because it is what
// answers on a machine with no SAPI at all (every Wine prefix).
//
// COM ON THE GAME THREAD, scoped to this call. It runs when the settings menu
// asks for the list, not per frame, and all three CoInitializeEx outcomes are
// real: S_OK (ours to undo), S_FALSE (already initialised here, still ours to
// balance), RPC_E_CHANGED_MODE (the game holds a different apartment — use it,
// and do NOT uninitialise something we did not start).
std::vector<std::pair<std::string, std::string>> enumerateTtsVoicesViaSapi() {
    std::vector<std::pair<std::string, std::string>> voices;
    const HRESULT coHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(coHr) && coHr != RPC_E_CHANGED_MODE) return voices;
    const bool ownsCom = SUCCEEDED(coHr);

    // Both categories, for the same reason the walk reads both hives.
    for (const wchar_t* category : { SPCAT_VOICES,
                                     L"HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft"
                                     L"\\Speech_OneCore\\Voices" }) {
        ISpObjectTokenCategory* cat = nullptr;
        if (FAILED(CoCreateInstance(kCLSID_SpObjectTokenCategory, nullptr,
                                    CLSCTX_ALL, kIID_ISpObjectTokenCategory,
                                    reinterpret_cast<void**>(&cat)))) {
            continue;
        }
        IEnumSpObjectTokens* tokens = nullptr;
        if (SUCCEEDED(cat->SetId(category, FALSE)) &&
            SUCCEEDED(cat->EnumTokens(nullptr, nullptr, &tokens)) && tokens) {
            ISpObjectToken* token = nullptr;
            while (tokens->Next(1, &token, nullptr) == S_OK && token) {
                // The token's DEFAULT string value is its display name — the
                // same string the registry walk reads, so the two agree and a
                // stored name resolves either way.
                WCHAR* desc = nullptr;
                if (SUCCEEDED(token->GetStringValue(nullptr, &desc)) && desc) {
                    const std::string name = wideToUtf8(desc);
                    CoTaskMemFree(desc);
                    bool seen = false;
                    for (const auto& have : voices) {
                        if (have.first == name) { seen = true; break; }
                    }
                    // The ID is kept for the log only. Selection matches on
                    // the NAME against a live enumeration (see the worker),
                    // because a dynamically produced token has no registry
                    // path for SetId to resolve.
                    if (!seen && !name.empty()) {
                        WCHAR* id = nullptr;
                        std::string idStr;
                        if (SUCCEEDED(token->GetId(&id)) && id) {
                            idStr = wideToUtf8(id);
                            CoTaskMemFree(id);
                        }
                        voices.emplace_back(name, idStr);
                    }
                }
                token->Release();
                token = nullptr;
            }
            tokens->Release();
        }
        cat->Release();
    }
    if (ownsCom) CoUninitialize();
    std::sort(voices.begin(), voices.end());
    return voices;
}

// The token for a voice DISPLAY NAME, or null. Caller releases; COM must
// already be live on this thread, which it is — the only caller is the audio
// worker, after its own CoInitializeEx.
// How long the speak loop blocks per poll before re-checking for shutdown or an
// interrupt, and how long it waits for a PURGED engine to actually fall idle. The
// poll is a responsiveness knob; the drain is a correctness one -- see its use.
constexpr unsigned long kSpeakPollMs  = 50;
constexpr unsigned long kPurgeDrainMs = 250;
// How many drain slices before the engine is declared WEDGED (drainVerdict in
// spotter_tts_voice.h): generous while the session is live, tight during
// shutdown, where boundedness is the point of the async speak loop.
constexpr int kPurgeDrainSlices         = 10;   // 2.5s
constexpr int kPurgeDrainSlicesShutdown = 2;    // 0.5s

// SAPI CALLS THAT DISPATCH INTO A THIRD-PARTY VOICE ENGINE, behind SEH.
//
// A user cycling voices crashed the game at espeak-ng.dll+0x74ed8 -- a string scan
// dereferencing address 0x1, reached through sapi.dll -> EspeakSAPI.dll on our TTS
// worker. Every plugin frame on that stack was residue; the live fault was entirely
// inside the engine. We cannot fix eSpeak, and we cannot tell in advance which of a
// user's installed voices is going to do this, so the only lever is to not let it
// take the process with it.
//
// CATCHING AN ACCESS VIOLATION AND CARRYING ON IS NOT FREE, and it is worth being
// honest about the trade: the engine's own state after a fault is unknown, so we
// stop using that voice for the session rather than pretending it recovered. What
// is certain is the alternative -- the whole game dies mid-session, which is what
// the dump shows.
//
// THE GUARDS ONLY COVER CODE THAT EXECUTES ON THIS THREAD, and the third crash
// dump is the proof that this needs arranging, not assuming: same faulting
// instruction as the first two, but reached through combase's cross-apartment
// dispatch ON THE GAME'S MAIN THREAD. EspeakSAPI registers ThreadingModel=
// Apartment, and with the worker in the MTA, COM homed the engine in the
// process's main STA -- the game thread -- so every dispatch into it executed
// where no guard of ours exists. Two consequences, both load-bearing:
//   1. the worker initializes COM APARTMENT-threaded (see workerThread), so an
//      apartment engine created by our calls is homed HERE; and
//   2. the worker pumps its STA queue only via sapiGuardedPump below, so a
//      marshaled call into that engine -- which is where the engine's Speak
//      actually runs -- executes behind the same SEH as the direct calls.
//
// Its own function with no C++ objects: __try/__except cannot coexist with anything
// needing unwinding (C2712). MSVC-only, like the rest of the SEH in this codebase;
// the mingw test build compiles the plain call.
#if defined(_MSC_VER)
static HRESULT sapiGuardedSetVoice(ISpVoice* voice, ISpObjectToken* token, bool* faulted) {
    __try {
        return voice->SetVoice(token);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *faulted = true;
        return E_FAIL;
    }
}
static HRESULT sapiGuardedSpeak(ISpVoice* voice, const wchar_t* text, DWORD flags,
                                ULONG* stream, bool* faulted) {
    __try {
        return voice->Speak(text, flags, stream);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *faulted = true;
        return E_FAIL;
    }
}
static HRESULT sapiGuardedWait(ISpVoice* voice, ULONG ms, bool* faulted) {
    __try {
        return voice->WaitUntilDone(ms);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *faulted = true;
        return E_FAIL;
    }
}
// For dropping a voice whose engine ALREADY faulted: Release() cascades into
// that same engine's teardown, walking the state the fault just proved
// corrupt -- unguarded, it dies one line after the guard that caught the
// original fault. If the release faults too, the object is leaked; a leak per
// faulted engine per session, on an engine already retired.
static void sapiGuardedRelease(ISpVoice* voice) {
    __try {
        voice->Release();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}
// Deliver whatever cross-apartment calls COM has queued for THIS thread. An
// apartment engine homed here receives its Speak as a message posted by SAPI's
// own speaking thread, so the pump is where that third-party code EXECUTES --
// which is exactly why it runs behind the same SEH as the direct calls. MSG is
// POD, so no C2712.
static void sapiGuardedPump(bool* faulted) {
    __try {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *faulted = true;
    }
}
#else
static HRESULT sapiGuardedSetVoice(ISpVoice* voice, ISpObjectToken* token, bool*) {
    return voice->SetVoice(token);
}
static HRESULT sapiGuardedSpeak(ISpVoice* voice, const wchar_t* text, DWORD flags,
                                ULONG* stream, bool*) {
    return voice->Speak(text, flags, stream);
}
static HRESULT sapiGuardedWait(ISpVoice* voice, ULONG ms, bool*) {
    return voice->WaitUntilDone(ms);
}
static void sapiGuardedRelease(ISpVoice* voice) {
    voice->Release();
}
static void sapiGuardedPump(bool*) {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}
#endif

// Voices that faulted this session. Names, not tokens: the token is re-enumerated
// per selection (see findVoiceToken), and a name is what the settings tab shows.
static std::set<std::string>& faultedVoices() {
    static std::set<std::string> s;
    return s;
}

ISpObjectToken* findVoiceToken(const std::string& displayName) {
    if (displayName.empty()) return nullptr;
    for (const wchar_t* category : { SPCAT_VOICES,
                                     L"HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft"
                                     L"\\Speech_OneCore\\Voices" }) {
        ISpObjectTokenCategory* cat = nullptr;
        if (FAILED(CoCreateInstance(kCLSID_SpObjectTokenCategory, nullptr,
                                    CLSCTX_ALL, kIID_ISpObjectTokenCategory,
                                    reinterpret_cast<void**>(&cat)))) {
            continue;
        }
        IEnumSpObjectTokens* tokens = nullptr;
        ISpObjectToken* found = nullptr;
        if (SUCCEEDED(cat->SetId(category, FALSE)) &&
            SUCCEEDED(cat->EnumTokens(nullptr, nullptr, &tokens)) && tokens) {
            ISpObjectToken* token = nullptr;
            while (!found && tokens->Next(1, &token, nullptr) == S_OK && token) {
                WCHAR* desc = nullptr;
                if (SUCCEEDED(token->GetStringValue(nullptr, &desc)) && desc) {
                    const bool match = wideToUtf8(desc) == displayName;
                    CoTaskMemFree(desc);
                    if (match) { found = token; break; }   // keep the ref
                }
                token->Release();
                token = nullptr;
            }
            tokens->Release();
        }
        cat->Release();
        if (found) return found;
    }
    return nullptr;
}

// What everything else calls. SAPI's own enumeration and nothing else: a
// registry walk of the two voice hives used to back this up, and it was
// deleted because it can only ever be a SUBSET — the voices worth reaching
// with this menu, the neural ones a third-party engine supplies, have no
// registry entry at all.
//
// It did serve one case this cannot, and the case is why it is worth naming
// rather than pretending it did not exist: if CoInitializeEx fails on the
// GAME thread, this returns empty while the audio worker — which does its own
// init — could still have spoken. The list would be empty rather than wrong,
// on a machine where COM is broken enough that the rest of the game is in
// trouble too. A second implementation of the same question, kept for that,
// is a thing waiting to disagree with the first.
std::vector<std::pair<std::string, std::string>> enumerateTtsVoices() {
    auto viaSapi = enumerateTtsVoicesViaSapi();
    // ONCE per run, not per call. Cycling the voice setting calls this on
    // every click, and the full catalogue is fifteen lines — which buried the
    // one line that says what was actually chosen. The list is what you want
    // when a voice is MISSING, and that question is asked once.
    static bool logged = false;
    const bool first = !logged;
    logged = true;
    if (!viaSapi.empty()) {
        if (first) {
            DEBUG_INFO_F("Spotter: TTS voices - %zu from SAPI", viaSapi.size());
            for (const auto& v : viaSapi) {
                DEBUG_INFO_F("Spotter:   voice '%s'", v.first.c_str());
            }
        }
        return viaSapi;
    }
    if (first) DEBUG_INFO("Spotter: SAPI listed no voices");
    return viaSapi;
}

std::wstring utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return std::wstring();
    const int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
                                        static_cast<int>(utf8.size()), nullptr, 0);
    if (len <= 0) return std::wstring();
    std::wstring wide(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()),
                        &wide[0], len);
    return wide;
}

}  // namespace

namespace {

// Your best ever on this track/bike, from the persisted stats rather than a
// session-start snapshot: the spotter wants the CURRENT record of it, and a
// lap that has just beaten it should read as the reference from then on.
int allTimeBestLapTime() {
    const StatsPersonalBestData* pb = StatsManager::getInstance().getPersonalBest();
    return (pb && pb->isValid()) ? pb->lapTime : -1;
}

// The provider's record for this track. MX Bikes only — everywhere else the
// HUD does not exist, and the variables simply stay empty.
int trackRecordLapTime() {
#if GAME_HAS_RECORDS_PROVIDER
    return HudManager::getInstance().getRecordsHud().getFastestRecordLapTime();
#else
    return -1;
#endif
}

}  // namespace

SpotterManager& SpotterManager::getInstance() {
    static SpotterManager instance;
    return instance;
}

// ============================================================================
// THE SPOTTER PROBE — ON IN EVERY BUILD, DELIBERATELY.
//
// It writes the paired logs the spotter is read from: "what did the spotter
// say" beside "what did the standings table hold at that instant".
//
// It used to be gated - off unless a build set MXBMRP3_SPOTTER_PROBE - on the
// grounds that a debug probe must not ship. That was right while the spotter
// was being written and wrong for its first release: the thing we most need
// after it reaches players is their opinion of the DEFAULT wording and pacing,
// and every one of those reports arrives as a log. A log without the transcript
// cannot answer "what did it actually say, and when", which is the whole
// question. So the gate is gone rather than documented, and this is a decision
// with an expiry: once the defaults settle, the probe goes entirely.
//
// A SWITCH RATHER THAN SIX SITES, because the site list was the actual hazard:
// it has been miscounted three times, always low ("both", "four", "five"
// against a real six), and the miss that never announced itself was the
// forward declaration — delete the definition without it and a static is
// declared and never defined, an error on the cross build. Set this to 0 and
// every site compiles out together; there is nothing left to enumerate and
// nothing to forget. That is also how it gets removed later: flip it, confirm
// the build, then delete what the compiler stops reaching.
//
// The tape tooling depends on it too: tests/integration/spotter_transcript_driver
// .cpp replays a recorded weekend and greps SPOTTER SAY to produce a readable
// transcript, which is the only way the wording and ordering get reviewed.
//
// WHAT IT COSTS: event-rate, not frame-rate (about four splits, a lap report
// and a handful of cues per lap), but each line is a mutex-held write with an
// explicit flush on the GAME THREAD, so each is a frame hitch where it lands —
// and run_perf.sh drives no cues, so it cannot see them. It also puts the
// spoken transcript and every neighbour's gap in the user's own log, which is
// the point, and is worth knowing before pasting one into a public thread.
// ============================================================================
#ifndef MXBMRP3_SPOTTER_PROBE
#  define MXBMRP3_SPOTTER_PROBE 1
#endif

#if MXBMRP3_SPOTTER_PROBE
static void debugLogStandingsAt(const PluginData& pd, const char* where);
#endif

// Everything one session must not carry into the next. Called from the
// SessionStarted branch of onRaceEvent — and, separately, from that function's
// spotter-is-off early return, which is why this is a function at all rather
// than the block it used to be inline.
void SpotterManager::resetSessionState() {
    m_pendingSessionEnd = PendingSessionEnd{};
    m_sessionEndSpoken = false;
    m_finishSpokenFocused = false;
    m_sessionEndHidePosition = false;
    // A fresh green flag re-arms the once-per-session milestones (the time
    // path also self-resets on a clock rewind, but the lap path has no clock
    // to notice) and wipes the pace tracker's crossings — stale timing points
    // must not resolve gaps across sessions.
    m_milestones.reset();
    m_pace.reset();
    m_penaltyTotalMs = 0;
    m_penaltyColumnStale = false;
    m_fuelLowSaid = m_fuelCriticalSaid = false;
    m_lastReportedPos = 0;
    m_lastReportedPosNum = -1;
    // Everything else the previous session left behind. m_pace.reset() wipes
    // the TRACKER's memory, but these are the manager's own caches of the same
    // run, and each of them speaks if it survives:
    //   - the last resolved gaps still carry hasTrend, so race 2's opening
    //     cue reports race 1's "gaining";
    //   - the last split is what the next sector time is subtracted from, so a
    //     matching lap number across a restart yields a plausible, wrong
    //     sector (or a negative one, and silence);
    //   - a lap armed at the end of one session and never flushed (no
    //     classification followed it) flushes into the next session's first
    //     one, reporting the old lap's time and position.
    m_lastAhead = SpotterPace::Gap{};
    m_lastBehind = SpotterPace::Gap{};
    m_lastSplitLap = -1;
    m_lastSplitRider = -1;
    m_lastSplitCumMs = 0;
    m_pendingLap.armed = false;
    m_pendingFastest = PendingFastest{};
}

void SpotterManager::onRaceEvent(EventLogType type, int raceNum,
                                 int focusedRaceNum, int sessionTimeMs,
                                 const EventNumbers& nums) {
    // Subtitles-only mode is real: intake runs for either switch, and only
    // the audio dispatch at the end of emitCue() checks m_enabled.
    if (!m_enabled && !m_subtitles) {
        // ...with the session wipe as the ONE exception, for the same reason
        // the category gate is applied in emitCue rather than here: both
        // switches are live settings, so turning the spotter off, crossing a
        // session boundary and turning it back on would otherwise carry the
        // previous session's pace, gaps, split and pending lap into the new
        // one — the three bugs resetSessionState()'s own comment enumerates,
        // reached by a different door.
        if (type == EventLogType::SessionStarted) resetSessionState();
        return;
    }

#if MXBMRP3_SPOTTER_PROBE
    // TEMP-DEBUG(spotter-vs-standings): the FINISH is the one moment the split
    // and S/F probes never reach — flushDeferredCues returns before it once your
    // race is over — and it is where {gap_to_leader} is spoken twice
    // (finished_you, then session_ended). What the official column says at that
    // instant is the open question the next log has to answer.
    if (raceNum >= 0 && raceNum == focusedRaceNum &&
        (type == EventLogType::RiderFinished ||
         type == EventLogType::SessionComplete)) {
        debugLogStandingsAt(PluginData::getInstance(),
                            type == EventLogType::RiderFinished
                                ? "your finish" : "session complete");
    }
#endif

    // Filed by SUBJECT first (see SpotterPhrase::Category): your own pit
    // call is not "other riders", and a rival's fastest lap is.
    const SpotterPhrase::Category cat = SpotterPhrase::categoryFor(
        type, SpotterPhrase::subjectOf(raceNum, focusedRaceNum));
    // NO early return on the category here, deliberately. emitCue applies the
    // gate at the end, and returning up here would skip the SessionStarted
    // branch below — the ONLY place per-session state is wiped (milestones,
    // the pace tracker, the cached gaps either side, the last split, the
    // pending lap and fastest-lap holds, the fuel latches, the last reported
    // position). SessionStarted is a General cue, so muting General used to
    // carry every one of those across a session boundary and reintroduce the
    // bugs that block's own comments enumerate: race 2's first crossing
    // reporting race 1's trend, a sector subtracted across a restart, a stale
    // lap flushing into the next session.
    //
    // The work between here and there is per-EVENT, not per-frame, so nothing
    // is bought by skipping it.

    const bool focused = raceNum >= 0 && raceNum == focusedRaceNum;

    // The lap-quality LADDER, which the on-screen notices have had all along
    // (race_lap_handler.cpp: "All-time PB supersedes fastest lap and session
    // PB") and the spotter sat outside of. Only your own fastest lap leaks: it
    // arrives through the EVENT LOG, which logs it unconditionally because the
    // race feed wants it, while the notices are chosen by the ladder. So a lap
    // that was both spoke twice, back to back, saying the same time — "New
    // personal best, one oh six point six." / "Fastest lap, nice work, one oh
    // six point six." Both mxbclub and the demo weekend do it.
    //
    // Keyed by the LAP TIME rather than a bare flag. Within one crossing the
    // handler calls onPersonalBest, then logs FastestLap, then
    // onRaceLapCompleted — set, consume, clear — so a flag would work, but it
    // would work only for as long as that order holds, and nothing enforces
    // the order. Matching the time means a latch that somehow outlived its lap
    // can still only suppress a lap of exactly the same time, which is the lap
    // it was set for.
    if (type == EventLogType::FastestLap && focused &&
        m_higherLapCueTimeMs > 0 &&
        nums.lapTimeMs == m_higherLapCueTimeMs) {
        return;
    }

    // HELD until the next classification, and only one is held (see
    // PendingFastest).
    // Joining a lobby mid-session replays every rider's whole lap history in a
    // single instant; each replayed lap that improved the overall best spoke,
    // so a real join announced seven fastest laps in one millisecond, none of
    // them a moment the player was there for. Holding one collapses the replay
    // to the line that is still true — the session's fastest — because a
    // classification cannot arrive inside a callback batch.
    //
    // BELOW the ladder check, not above it: that latch is set and cleared
    // within one crossing (onPersonalBest, then this, then
    // onRaceLapCompleted), so by the time a held cue flushes it is long gone
    // and a lap that is both a PB and the fastest would announce itself twice.
    // Everything above this point is a decision about the instant the lap
    // arrived; only the emission waits.
    if (type == EventLogType::FastestLap && !m_emittingPendingFastest) {
        m_pendingFastest = { true, raceNum, focusedRaceNum, sessionTimeMs,
                             nums };
        return;
    }

    if (type == EventLogType::SessionComplete && !m_emittingPendingSessionEnd) {
        // ONCE per session: RACE_OVER and FINISHED both map to SessionComplete
        // in the session handler, so a session that walks both states would
        // speak "That's Race 2 done" twice, seconds apart.
        if (m_sessionEndSpoken || m_pendingSessionEnd.armed) return;
        // In a race the game can complete the session BEFORE the subject's own
        // finish reaches us -- for the WINNER the two arrive in one batch, in
        // tape order [SessionComplete, RiderFinished], which spoke "That's
        // Race 2 done, P one" and then "That's the flag, P one" -- the flag
        // after the wrap-up, with the position twice. Held to the next
        // classification (the lap report's pattern), the finish cue speaks
        // first and the wrap-up follows it. Non-race sessions have no finish
        // cue and speak immediately, as before.
        if (PluginData::getInstance().isRaceSession() && !subjectRaceOver()) {
            m_pendingSessionEnd = { true, raceNum, sessionTimeMs, nums };
            return;
        }
    }

    const char* key = SpotterCuePack::cueKeyFor(type, focused);
    if (!key) return;  // never-spoken, never-overridable (Director)

    // Session-kind refinements the event alone cannot make: non-race green
    // flags get their own keys, and the P1 rider taking the flag (not you) is
    // its own moment. Both are a change of KEY — the words for each live in
    // the shipped pack like every other cue's.
    if (type == EventLogType::SessionStarted) {
        resetSessionState();
        // session_started now means "this session is active", for every
        // session kind — the generic key says which via {session_name}, so a
        // pack writes ONE line. The
        // per-kind keys stay as optional refinements (they fall back to
        // session_started), for a recorded voice that cannot stitch a session
        // name, or wording you want different per kind.
        //
        // It no longer means "green flag": a standing start holds on the grid
        // after this fires, and the gate falling is gate_drop.
        switch (Game::Adapter::toCanonicalSession(
            PluginData::getInstance().getSessionData().session,
            PluginData::getInstance().getSessionData().eventType)) {
            case Unified::Session::Practice:
                key = "practice_started";
                break;
            case Unified::Session::PreQualify:
            case Unified::Session::QualifyPractice:
            case Unified::Session::Qualify:
                key = "quali_started";
                break;
            case Unified::Session::Warmup:
                key = "warmup_started";
                break;
            default:
                break;  // races and everything else keep session_started
        }
    } else if (type == EventLogType::SessionStateChange) {
        // "Waiting" is not a state anything entered — it is what
        // getSessionStateString() returns when NO known bit is set, i.e. the
        // idle gap between sessions. A race that had just said "Race 2
        // complete, you finished P eleven" followed it ten seconds later with
        // "Race 2, WAITING", which is the plugin narrating its own enum. The
        // states worth hearing all have a bit, and most have a cue of their
        // own; this leaves Cancelled as the one session_state still speaks for.
        namespace St = PluginConstants::SessionState;
        if (!(m_sessionState & (St::CANCELLED | St::RACE_OVER | St::PRE_START |
                                St::SIGHTING_LAP | St::FINISHED |
                                St::IN_PROGRESS))) {
            return;
        }
        // "Session update" told you something changed but not to what, which
        // is the only interesting part; {session_state} carries it, and
        // {session_name} the label the Session HUD shows.
    } else if (type == EventLogType::RiderFinished && !focused &&
               nums.position == 1) {
        key = "finished_leader";
    } else if (type == EventLogType::RiderFinished && focused) {
        // finished_you is about to speak, and it carries the position -- the
        // session wrap-up that follows moments later must not read it back.
        m_finishSpokenFocused = true;
    } else if (type == EventLogType::SessionComplete) {
        m_sessionEndSpoken = true;
        m_sessionEndHidePosition = m_finishSpokenFocused;
    }

    const SpotterPhrase::LapTimeParts timeParts =
        SpotterPhrase::lapTimePartsMs(nums.lapTimeMs);
    const int penaltySecs = SpotterPhrase::penaltyWholeSeconds(nums);
    // The BONUS lap count — the one number that is exactly true wherever the
    // leader is when the clock expires. The template says what it counts
    // ("after this one"); see SpotterPhrase::lapsWords.
    const int lapsLeft = nums.bonusLaps;
    SpotterVars::Vars vars;
    vars.eventRider = SpotterPhrase::riderRef(raceNum, focusedRaceNum);
    vars.eventTime = SpotterPhrase::lapTimeWordsMs(nums.lapTimeMs);
    // A rival's lap measured against yours. Only for ANOTHER rider's lap:
    // for your own the {gap_to_*} family already answers it, and comparing
    // your new fastest lap with the best it just replaced is a race against
    // yourself that reads as noise.
    if (!focused && timeParts.valid && timeParts.totalMs > 0) {
        const PluginData& pdata = PluginData::getInstance();
        const StandingsData* me = pdata.getStanding(pdata.getDisplayRaceNum());
        // -1 while your best is your opening lap: a gate start is not a
        // reference, however true the arithmetic. See bestLapIsFirstLap().
        const int myBest =
            (me && !bestLapIsFirstLap()) ? me->bestLap : -1;
        const IdealLapData* myIdeal = pdata.getIdealLapData();
        const int myLast = myIdeal ? myIdeal->lastLapTime : -1;
        auto against = [&](int ref, std::string& out) {
            if (ref <= 0) return;
            const int d = timeParts.totalMs - ref;
            // A dead heat says nothing: "zero point zero slower" is the exact
            // wording the reference() helper suppresses for your own laps,
            // and a rival matching you to the millisecond earns the same
            // silence — the optional group drops.
            if (d == 0) return;
            out = SpotterPhrase::gapWordsMs(d) +
                  (d < 0 ? " quicker" : " slower");
        };
        against(myBest, vars.eventGapToBestLap);
        against(myLast, vars.eventGapToLastLap);
    }
    vars.penaltySeconds = SpotterPhrase::secondsWords(penaltySecs);
    vars.overtimeLaps = SpotterPhrase::lapsWords(lapsLeft);

    // YOUR PENALTY TOTAL, at the instant the penalty lands — which the
    // standings cannot give you, because this callback IS how the plugin
    // learns about it and the classification column catches up a beat later.
    // fillAmbientVars serves that column for every other cue, and it is right
    // there; here it would be short by exactly the penalty being announced.
    //
    // MAX, not addition: the classification is authoritative when it HAS
    // absorbed the penalty, and our own tally is when it has not, and a total
    // only ever grows. Adding unconditionally would double-count whenever the
    // classification won the race; taking the column alone under-reports
    // whenever it lost, which is the common case. This is right either way,
    // and self-corrects on the next penalty if a session was joined with some
    // already served.
    if (type == EventLogType::Penalty && focused && nums.penaltyMs > 0) {
        const StandingsData* me = PluginData::getInstance().getStanding(raceNum);
        // After a CLEAR or a REVISION the column is stale until the next
        // classification absorbs it — a fresh 5s penalty right after a clear
        // took max(stale 10s, 5s) and announced "ten seconds in total" for a
        // real total of five. While the latch is up the tally is the only
        // truth; the flush drops the latch once a classification has landed.
        const int column =
            (me && !m_penaltyColumnStale) ? me->penalty : 0;
        const int tallied = m_penaltyTotalMs + nums.penaltyMs;
        m_penaltyTotalMs = column > tallied ? column : tallied;
        // Only once there is more than the one just announced: on a first
        // penalty the total IS the amount, and "five seconds, five seconds in
        // total" is exactly the redundancy an optional group exists to drop.
        if (m_penaltyTotalMs > nums.penaltyMs) {
            vars.penaltyTotal =
                SpotterPhrase::secondsWords((m_penaltyTotalMs + 500) / 1000);
        }
    }
    // A total that only grows is right for penalties and wrong for the two
    // events that take one back, so both hand authority to the standings
    // again: a CLEAR zeroes the column by definition, and a REVISION settles
    // it to whatever the game decided. Without this the max() above would
    // defend a figure that no longer exists.
    if (type == EventLogType::PenaltyClear && focused) {
        m_penaltyTotalMs = 0;
        m_penaltyColumnStale = true;
    }
    if (type == EventLogType::PenaltyChange && focused) {
        const StandingsData* me = PluginData::getInstance().getStanding(raceNum);
        m_penaltyTotalMs = me && me->penalty > 0 ? me->penalty : 0;
        m_penaltyColumnStale = true;
    }
    emitCue(key, cat, std::move(vars), sessionTimeMs,
            (raceNum >= 0 && raceNum <= 999) ? raceNum : -1,
            timeParts.valid ? timeParts.composed : -1,
            timeParts.valid ? timeParts.tenths : -1, penaltySecs, lapsLeft);
    m_sessionEndHidePosition = false;
}

void SpotterManager::onRaceLapCompleted(int raceNum, int completedLaps,
                                        int lapTimeMs, bool lapValid) {
    if (!m_enabled && !m_subtitles) return;
    // Closes the lap-quality ladder armed by onPersonalBest / onSessionBest:
    // this runs last of the three calls one crossing makes, so the latch never
    // outlives the lap it was set for. See onRaceEvent's FastestLap branch.
    // Read before clearing -- the lap REPORT flushes later still, and needs to
    // know whether this lap's time has already been said out loud.
    const bool lapTimeAlreadySpoken =
        m_higherLapCueTimeMs > 0 && m_higherLapCueTimeMs == lapTimeMs;
    m_higherLapCueTimeMs = -1;
    const PluginData& pd = PluginData::getInstance();

    // Only the RACE-shaped parts of this callback are race-gated — the same
    // split onRaceSplit needed. Your position and your lap time are as real in
    // practice and qualifying as they are in a race (position is the index in
    // classification order, which every session has), and those are the
    // sessions you are most likely to want them read back in. Gating the whole
    // callback silenced lap_completed and the position gained/lost pair
    // everywhere except a race.
    //
    // The gap cues and the halfway milestone stay race-only below: a gap to
    // the rider behind is a race notion, and a practice session has no
    // half-distance.

    // Lap-race halfway_point rides the LEADER's crossings (a timed race gets its
    // milestones from the clock instead — see onTrackPositions).
    const int position = pd.getPositionForRaceNum(raceNum);
    if (pd.isRaceSession() && position == 1 &&
        pd.getSessionData().sessionLength <= 0 &&
        isCategoryEnabled(SpotterPhrase::Category::Timing)) {
        if (const char* cue = m_milestones.updateLaps(
                completedLaps, pd.getSessionData().sessionNumLaps)) {
            emitCue(cue, SpotterPhrase::Category::Timing, {},
                    pd.getSessionElapsedTime());
        }
    }

    // Every rider's S/F crossing is a timing point: record it, and see
    // whether it resolves a pending behind-gap report.
    const int nowMs = pd.getSessionElapsedTime();
    const long long sfKey = SpotterPace::pointKey(completedLaps,
                                                  SpotterPace::kSfPoint);
    const bool wantPace = pd.isRaceSession() &&
                          isCategoryEnabled(SpotterPhrase::Category::Timing);
    if (wantPace && raceNum == m_pace.pendingBehind()) {
        SpotterPace::Gap gap;
        if (m_pace.behindPoint(raceNum, sfKey, nowMs, gap)) {
            emitGapCue(gap, nowMs);
        }
    }
    if (pd.isRaceSession()) m_pace.otherPoint(raceNum, sfKey, nowMs);

    // The pit-board moment. Everything here is ONE event — your crossing —
    // so it is one set of variables handed to the cues that mark it, rather
    // than a separate cue per number.
    //
    // The gap to the rider ahead used to be its own cue (gap_ahead, plus a
    // gaining and a losing variant). It fired at exactly this instant and
    // marked no moment of its own, so it was three keys for what is a value:
    // it is now {gap_to_ahead} / {trend_ahead} / {gained_on_ahead} on the cues
    // below. The BEHIND gap is not like that and stays a cue — it resolves
    // when that rider reaches a point you already crossed, which is a moment
    // nothing else marks.
    if (raceNum != pd.getDisplayRaceNum() || raceNum < 0) return;

    // Fuel. Checked HERE rather than per frame: the estimate only moves when a
    // lap completes, so a lap crossing is both the cheapest place to look and
    // the moment the number actually changed.
    //
    // Read from FuelWidget rather than tracked here, so the laps spoken are the
    // laps shown — one history, not two that could drift apart. Same thresholds
    // as its colours, so the voice and the amber/red agree.
    //
    // ABOVE the Timing gate on purpose: fuel is a General cue, and it sat below
    // the lap report until that report moved to a deferred flush and took
    // this block out with it. Both keys stayed registered, documented and
    // shipped, firing never. `every key in the registry is emitted by something`
    // (test_spotter_pack_census.cpp) is what notices next time.
    {
        // Same reach as the track record above: a HUD accessor on the game
        // thread, which is where every caller of this function already is.
        const float laps =
            HudManager::getInstance().getFuelWidget().getLapsRemaining();
        if (laps >= 0.0f) {
            const bool critical = laps < FuelEstimate::kCriticalLaps;
            const bool low = laps < FuelEstimate::kWarnLaps;
            // `fuel_critical` ships commented out, so on the stock pack it has
            // no phrase and emitting it says nothing -- while still latching
            // BOTH flags, which used to swallow the `fuel_low` that would have
            // spoken. Pick the key that actually exists, so a pack decides what
            // it hears rather than what it is silenced by.
            const bool haveCritical = m_pack.phrases.count("fuel_critical") ||
                                      m_pack.wavs.count("fuel_critical") ||
                                      m_pack.mixes.count("fuel_critical");
            const bool sayCritical = critical && haveCritical;
            // Edge-triggered, and only ever downward: a warning that repeated
            // every lap would be the noisiest cue there is, and one that
            // re-fired after a splash of fuel would be lying about direction.
            if ((sayCritical && !m_fuelCriticalSaid) || (low && !m_fuelLowSaid)) {
                if (sayCritical) m_fuelCriticalSaid = true;
                m_fuelLowSaid = true;
                SpotterVars::Vars fv;
                // lapsWords, not numberWords: it carries the noun and agrees
                // with it. The template used to spell " laps" itself, so a
                // one-lap warning said "Fuel getting low, one laps."
                fv.fuelLaps = SpotterPhrase::lapsWords(static_cast<int>(laps));
                emitCue(sayCritical ? "fuel_critical" : "fuel_low", SpotterPhrase::Category::General, std::move(fv), nowMs);
            }
        }
    }

    if (!isCategoryEnabled(SpotterPhrase::Category::Timing)) return;
    if (position <= 0) return;
    const StandingsData* st = pd.getStanding(raceNum);
    if (st && pd.getSessionData().isRiderFinished(
                  st->numLaps, st->numLapsAtLeaderFinish)) {
        return;  // the checkered-flag cue owns the last crossing
    }

    // Measure the gap ahead FIRST, so the cues below carry the stopwatch value
    // — time between their crossing of this line and yours — rather than the
    // live estimate fillAmbientVars would otherwise supply.
    SpotterVars::Vars lapVars;
    const std::vector<int>& order = pd.getClassificationOrder();
    if (wantPace) {
        m_pace.myPoint(sfKey, nowMs);
        measureAheadGap(position, order, sfKey, nowMs, lapVars);
    }

    // DEFERRED, not emitted here. Position and the standings-derived gaps come
    // from the classification order, and at THIS callback that order has not
    // yet been rebuilt for the lap you just completed — so reading it now
    // gives the standings from before the crossing. That is a whole lap stale,
    // and it sounds plausible: "P four" when you just took third.
    //
    // The plugin already knew. The "finished P#" event log entry was moved out
    // of this same handler into batchUpdateStandings for exactly this reason,
    // with a comment saying so — the spotter was doing what that comment warns
    // against.
    //
    // What IS measured here is the ahead gap, above: that comes from the
    // spotter's own timing points, not from standings, and it is a stopwatch
    // reading that belongs to this instant. Measure at the crossing, speak
    // after the classification.
    m_pendingLap.armed = true;
    m_pendingLap.raceNum = raceNum;
    m_pendingLap.lapTimeMs = lapTimeMs;
    m_pendingLap.nowMs = nowMs;
    m_pendingLap.vars = lapVars;
    m_pendingLap.lapValid = lapValid;
    m_pendingLap.timeAlreadySpoken = lapTimeAlreadySpoken;

    if (!wantPace) return;
    if (static_cast<size_t>(position) < order.size()) {
        m_pace.armBehind(order[position]);
    }
}


#if MXBMRP3_SPOTTER_PROBE
// TEMP-DEBUG(spotter-vs-standings): dump what the STANDINGS TABLE holds at a
// timing point — the OFFICIAL, split-derived gaps the classification carries,
// not the live position-derived estimate. The live figure is printed beside it
// (rt=) purely for contrast: they are different measurements and are meant to
// differ, and seeing both is the point of the probe.
//
// Paired with the SPOTTER SAY lines, so one log answers "the spotter said X —
// what did the table say at that instant?". Three rows only (ahead / you /
// behind): a full grid is unreadable and the neighbours are what the cues talk
// about.
//
// KEPT ON PURPOSE while the spotter's timing is still being retraced against the
// standings table -- the decision is recorded at the SPOTTER SAY line this pairs
// with.
//
// REMOVE THIS FUNCTION, ITS FORWARD DECLARATION above onRaceEvent, and its THREE
// call sites (the event tap, the split tap, the lap report): grep
// TEMP-DEBUG(spotter-vs-standings).
//
// THE FORWARD DECLARATION IS THE ONE THAT KEEPS GETTING MISSED, and it is named
// here rather than only at the other note because this is the copy a remover
// reads first -- they arrive at the function, not at the log line. Leave it
// behind and a static is declared and never defined, which the cross build takes
// as an error, so the mistake is at least loud. Every count so far has been low
// ("two", then "four", then "five"); the grep is the authority.
static void debugLogStandingsAt(const PluginData& pd, const char* where) {
    const int me = pd.getDisplayRaceNum();
    if (me <= 0) return;
    const std::vector<int>& order = pd.getClassificationOrder();
    int myIdx = -1;
    for (size_t i = 0; i < order.size(); ++i) {
        if (order[i] == me) { myIdx = static_cast<int>(i); break; }
    }
    if (myIdx < 0) return;

    auto row = [&](int idx, const char* label, char* buf, size_t cap) {
        if (idx < 0 || idx >= static_cast<int>(order.size())) {
            snprintf(buf, cap, " | %s -", label);
            return;
        }
        const int rn = order[idx];
        const StandingsData* s = pd.getStanding(rn);
        if (!s) { snprintf(buf, cap, " | %s #%d ?", label, rn); return; }
        snprintf(buf, cap, " | %s P%d #%d laps=%d gap=%d gapLaps=%d rt=%d",
                 label, idx + 1, rn, s->numLaps, s->gap, s->gapLaps,
                 s->realTimeGap);
    };
    char aheadBuf[128] = {0}, meBuf[128] = {0}, behindBuf[128] = {0};
    row(myIdx - 1, "ahead", aheadBuf, sizeof(aheadBuf));
    row(myIdx, "you", meBuf, sizeof(meBuf));
    row(myIdx + 1, "behind", behindBuf, sizeof(behindBuf));
    DEBUG_INFO_F("STANDINGS [%s]%s%s%s", where, meBuf, aheadBuf, behindBuf);
}
#endif

void SpotterManager::onRaceSplit(int raceNum, int lapNum, int splitIndex,
                                 int splitTimeMs) {
    if (!m_enabled && !m_subtitles) return;
    if (splitIndex < 0 || splitIndex >= SpotterPace::kSfPoint) return;
    const PluginData& pd = PluginData::getInstance();
    const int nowMs = pd.getSessionElapsedTime();
    const long long key = SpotterPace::pointKey(lapNum, splitIndex);

#if MXBMRP3_SPOTTER_PROBE
    // TEMP-DEBUG(spotter-vs-standings)
    if (raceNum == pd.getDisplayRaceNum()) {
        char where[32];
        snprintf(where, sizeof(where), "S%d lap %d", splitIndex + 1, lapNum);
        debugLogStandingsAt(pd, where);
    }
#endif

    // The PACE half is race-only, and only that half. A gap to the rider
    // behind is a race notion — in practice the field is scattered across
    // out-laps and cool-downs, where "behind by four seconds" means nothing.
    // The SECTOR cue below is the opposite: practice and qualifying are
    // exactly where you want your sector times read back, so gating the whole
    // callback on isRaceSession() (which this used to do) silenced it in the
    // sessions it is for.
    if (pd.isRaceSession()) {
        if (isCategoryEnabled(SpotterPhrase::Category::Timing) &&
            raceNum == m_pace.pendingBehind()) {
            SpotterPace::Gap gap;
            if (m_pace.behindPoint(raceNum, key, nowMs, gap)) {
                emitGapCue(gap, nowMs);
            }
        }
        m_pace.otherPoint(raceNum, key, nowMs);
    }
    if (raceNum != pd.getDisplayRaceNum()) return;
    SpotterVars::Vars sectorPace;
    if (pd.isRaceSession()) {
        m_pace.myPoint(key, nowMs);
        // The gap ahead AT THIS SPLIT, measured the same way the lap crossing
        // measures it: their crossing time of this exact point against yours.
        // Same call, same per-rider trend memory — a split IS a timing point,
        // and the tracker already records every rider's. So
        // {gap_to_ahead} and {gained_on_ahead} work on the sector cues too,
        // three or four times a lap instead of once.
        measureAheadGap(pd.getPositionForRaceNum(raceNum),
                        pd.getClassificationOrder(), key, nowMs, sectorPace);
    }

    // Sector cue. The split times the game sends are CUMULATIVE from the lap
    // start, so this sector is the difference from the previous split — and
    // the comparison is against your best of THAT sector, which is the same
    // number IdealLapHud shows. Default-quiet in the shipped pack: three or
    // four of these a lap is a spotter talking every twenty seconds.
    if (splitTimeMs <= 0 || !isCategoryEnabled(SpotterPhrase::Category::Timing)) {
        return;
    }
    // Your splits keep arriving on the cool-down lap. They are not sectors of
    // a race you are still in, and reading them back against your best is a
    // comparison of a roll-in to a flying lap.
    if (subjectRaceOver()) return;
    // Cumulative-to-sector needs the PREVIOUS split of the same rider's same
    // lap. Watching a different rider now (a spectate cut, or the director
    // moving on) makes the stored figure theirs, and lap numbers collide
    // across riders — so the rider is part of what has to match.
    const bool sameRun = m_lastSplitLap == lapNum && m_lastSplitRider == raceNum;
    const int prevCumulative = sameRun ? m_lastSplitCumMs : 0;
    const int sectorMs = splitTimeMs - prevCumulative;
    const bool firstOfLap = splitIndex == 0;
    m_lastSplitLap = lapNum;
    m_lastSplitRider = raceNum;
    m_lastSplitCumMs = splitTimeMs;
    // Sector 1 IS the cumulative figure, so it is right even with nothing
    // stored; any later sector without its predecessor would be a cumulative
    // time announced as a sector time, which is a plausible-sounding wrong
    // number rather than a missing one.
    if (!sameRun && !firstOfLap) return;
    if (sectorMs <= 0) return;   // out-of-order split: say nothing

    // Carries the split's own ahead-gap measurement (above), so the sector
    // cues can name it just like the lap crossing does.
    SpotterVars::Vars sv = std::move(sectorPace);
    sv.sectorNumber = SpotterPhrase::numberWords(splitIndex + 1);
    // {event_time} is the ACCUMULATED time at this split — the elapsed lap
    // time, which is what TimingHud shows ("S2: 60.00") and what a rider reads
    // a split as. The sector on its own is {sector_duration}, for a pack that
    // wants F1-style sector times instead.
    sv.eventTime = SpotterPhrase::gapWordsMs(splitTimeMs);
    sv.sectorDuration = SpotterPhrase::gapWordsMs(sectorMs);

    // Every reference stores INDIVIDUAL sectors, so its accumulated time at
    // this split is the running sum of sectors 1..N. Comparing accumulated to
    // accumulated is what makes the spoken delta the same number the screen
    // shows for this crossing.
    auto accumulated = [splitIndex](int s1, int s2, int s3) {
        const int parts[3] = { s1, s2, s3 };
        int total = 0;
        for (int i = 0; i <= splitIndex && i < 3; ++i) {
            if (parts[i] <= 0) return -1;   // a gap in the reference: no answer
            total += parts[i];
        }
        return total;
    };
    // WITH ITS DIRECTION. gapWordsMs absolute-values, which is right for a gap
    // — you are never "minus two seconds behind" someone — and wrong for a
    // comparison: "zero point three" read identically whether the sector was
    // three tenths up or three tenths down, which is the only thing a rider
    // wants to know. Every other comparison in this file says which way (see
    // the fastest-lap and reference-lap deltas); only sector_delta_best_lap
    // escaped it, and only because its CUE KEY carries the direction instead.
    auto sectorDelta = [splitTimeMs](int ref, std::string& out) {
        if (ref <= 0) return;
        const int d = splitTimeMs - ref;
        out = SpotterPhrase::gapWordsMs(d) + (d < 0 ? " quicker" : " slower");
    };

    // The OPENING lap as the session's only reference is the gate/out-lap --
    // a fact, not a reference (bestLapIsFirstLap's own doc). The lap-level
    // comparisons are already guarded; these split-level doors were not, and
    // the first flying lap announced "Best sector one... Best sector two...
    // On for your session best, up sixty five point zero" against a 3:05
    // opener. Everything derived from the opener stays silent until a real
    // lap exists to compare with.
    const bool openerIsOnlyRef = bestLapIsFirstLap();
    int bestLapRef = -1;
    if (const LapLogEntry* bl = pd.getBestLapEntry()) {
        bestLapRef = accumulated(bl->sector1, bl->sector2, bl->sector3);
        if (!openerIsOnlyRef) sectorDelta(bestLapRef, sv.sectorDeltaBestLap);
    }
    // Your best for THIS SECTOR ALONE, from completed laps. Kept for the
    // sector_best cue below; every other reference here is accumulated.
    int sectorBestMs = -1;
    if (const IdealLapData* ideal = pd.getIdealLapData()) {
        const int bests[4] = { ideal->bestSector1, ideal->bestSector2,
                               ideal->bestSector3, ideal->bestSector4 };
        if (splitIndex >= 0 && splitIndex < 4) sectorBestMs = bests[splitIndex];
        sectorDelta(accumulated(ideal->bestSector1, ideal->bestSector2,
                                ideal->bestSector3),
                    sv.sectorDeltaIdeal);
        sectorDelta(accumulated(ideal->lastLapSector1, ideal->lastLapSector2,
                                ideal->lastLapSector3),
                    sv.sectorDeltaLastLap);
    }
    int alltimeRef = -1;
    if (const StatsPersonalBestData* pb =
            StatsManager::getInstance().getPersonalBest()) {
        if (pb->isValid()) {
            alltimeRef = accumulated(pb->sector1, pb->sector2, pb->sector3);
            sectorDelta(alltimeRef, sv.sectorDeltaAlltime);
        }
    }
    int recordRef = -1;
#if GAME_HAS_RECORDS_PROVIDER
    {
        int r1 = -1, r2 = -1, r3 = -1, r4 = -1;
        if (HudManager::getInstance().getRecordsHud().getFastestRecordSectors(
                r1, r2, r3, r4)) {
            recordRef = accumulated(r1, r2, r3);
            sectorDelta(recordRef, sv.sectorDeltaRecord);
        }
    }
#endif

    // Faster or slower than your best LAP at this point, which is the
    // comparison the screen's green/red is making — not "best ever in this
    // sector alone", which is a different (and rarer) claim.
    const char* cueKey = "sector_completed";
    if (bestLapRef > 0 && !openerIsOnlyRef) {
        cueKey = splitTimeMs < bestLapRef ? "sector_completed_faster"
                                          : "sector_completed_slower";
    }
    // DEFAULT-QUIET: three or four of these a lap is a spotter talking every
    // twenty seconds, so the shipped pack leaves the row commented out.
    //
    // The split time's parts go with it, or a recorded pack's
    // `sector_completed_mix = ... {event_time}` has nothing to resolve and
    // every sector call drops to TTS.
    // SECONDS decomposition, the same ruler the text above uses: the words
    // are gapWordsMs ("seventy five point three", matching TimingHud's
    // accumulated display), so the trailer must be too. lapTimePartsMs folds
    // into minutes past 60s -- composed 115 for 75.3s -- and a recorded
    // pack's sector mix then stitched "one fifteen point three" against its
    // own subtitle saying seventy-five. One decomposition, both backends.
    const int secWhole = splitTimeMs / 1000;
    const int secTenth = (splitTimeMs % 1000) / 100;
    emitCue(cueKey, SpotterPhrase::Category::Timing, std::move(sv), nowMs, -1,
            secWhole <= 999 ? secWhole : -1, secTenth);

    // ---- the two cues that mark a MOMENT rather than describing every split --
    // Both ship ENABLED, unlike sector_completed above: that one fires three or
    // four times a lap whatever happens, these fire on something happening.

    // 1. A NEW BEST FOR THIS SECTOR ALONE.
    //
    // sector_completed compares ACCUMULATED time against your best lap, so a
    // blinding sector inside an otherwise scrappy lap never surfaces there —
    // you are still down on the lap, so it says "slower". That is exactly the
    // sector worth being told about, and it is a different question from the
    // one every other comparison in this function asks.
    //
    // SESSION SCOPE, and not by choice: bestSectorN lives in PluginData, which
    // is the live session cache, so it clears with the session. There is no
    // all-time best-per-sector anywhere in the plugin to compare against —
    // StatsPersonalBestData::sectorN is the sectors OF your best lap, which is
    // a different number. Session is also the honest comparison: another
    // session is another setup, another surface, maybe another bike.
    //
    // bestSectorN is written by updateIdealLap on the LAP event, so during
    // this lap it still holds the figure to beat rather than one this lap
    // already set. `> 0` is therefore the "you have a prior best" floor: it is
    // -1 until a valid lap completes, so lap one is silent instead of
    // announcing every sector as a best.
    //
    // KNOWN IMPRECISION, deliberate: validity is only known at the lap event,
    // so a sector set on a lap that is later invalidated is called here and
    // then not recorded. Announcing it a lap late would be worse — the news is
    // the sector, and it is stale by the time the flag lands.
    if (sectorBestMs > 0 && sectorMs < sectorBestMs && !openerIsOnlyRef) {
        SpotterVars::Vars bv;
        bv.sectorNumber = SpotterPhrase::numberWords(splitIndex + 1);
        bv.sectorDuration = SpotterPhrase::gapWordsMs(sectorMs);
        bv.sectorBestDelta = SpotterPhrase::gapWordsMs(sectorBestMs - sectorMs);
        emitCue("sector_best", SpotterPhrase::Category::Timing, std::move(bv),
                nowMs);
    }

    // 2. ON PACE, called at the LAST SPLIT BEFORE THE LINE — late enough that
    // the claim means something, early enough to be worth hearing while you can
    // still act on it. That split is GAME_SECTOR_COUNT - 2 (index 1 of three
    // sectors in MX Bikes, index 2 of four in GP Bikes), so this stays one
    // expression instead of a per-game table.
    //
    // ESCALATING: the STRONGEST reference you are actually beating wins. Being
    // up on record pace means being up on your own two as well, and "on for the
    // track record" buried under "on for a session best" would be the wrong
    // headline. Each tier is the same accumulated-vs-accumulated comparison the
    // deltas above make.
    //
    // The margin is why this is not just `splitTimeMs < ref`: a few hundredths
    // up with a sector to go is not news, and without a floor this fires on
    // most laps of any decent run. [Spotter] on_pace_margin_ms tunes it.
    //
    // Note it is your best LAP that is the reference, never the ideal: the
    // ideal is by construction no slower than your best lap, so a cue keyed on
    // beating it would essentially never fire.
    //
    // This sits BELOW the same-run guard above, so a last split whose earlier
    // splits this run never saw says nothing — even though the comparison
    // itself needs only the cumulative time. That is deliberate, not leftover
    // placement: the case it silences is a mid-lap join, where the lap did not
    // start at the line and so was never going to count as a best anyway.
    if (splitIndex == GAME_SECTOR_COUNT - 2) {
        struct Tier { int ref; const char* cue; };
        const Tier tiers[] = { { recordRef,  "on_pace_record" },
                               { alltimeRef, "on_pace_personal_best" },
                               { openerIsOnlyRef ? -1 : bestLapRef,
                                 "on_pace_session_best" } };
        for (const Tier& t : tiers) {
            if (t.ref <= 0 || splitTimeMs + m_onPaceMarginMs >= t.ref) continue;
            SpotterVars::Vars pv;
            pv.paceMargin = SpotterPhrase::gapWordsMs(t.ref - splitTimeMs);
            emitCue(t.cue, SpotterPhrase::Category::Timing, std::move(pv),
                    nowMs);
            break;
        }
    }
}

void SpotterManager::onSessionBest(int sessionTimeMs, int lapTimeMs) {
    if (!m_enabled && !m_subtitles) return;
    if (!isCategoryEnabled(SpotterPhrase::Category::Timing)) return;
    SpotterVars::Vars v;
    v.eventTime = SpotterPhrase::lapTimeWordsMs(lapTimeMs);
    // The parts as well as the words: without them a pack's
    // `session_best_mix = ... {event_time}` can never resolve and every such
    // cue drops to TTS. Same for personal_best/record_beaten below.
    const SpotterPhrase::LapTimeParts sbParts =
        SpotterPhrase::lapTimePartsMs(lapTimeMs);
    emitCue("session_best", SpotterPhrase::Category::Timing,
            std::move(v), sessionTimeMs, -1,
            sbParts.valid ? sbParts.composed : -1,
            sbParts.valid ? sbParts.tenths : -1);
    // ARM THE LADDER, exactly as onPersonalBest does. Without this a session
    // best and the fastest lap of the session BOTH spoke, back to back, saying
    // the same number -- "Session best, forty point seven." / "That's the
    // fastest of the session, forty point seven." The ladder in
    // race_lap_handler only picks between the on-screen NOTICES; the spotter's
    // fastest lap arrives separately through the event log, which logs it
    // unconditionally because the race feed wants it. Offline that is every
    // improved lap, since isFastestLap needs isOnline() and so is never true.
    m_higherLapCueTimeMs = lapTimeMs;
}

void SpotterManager::onRiderCrash(int raceNum, int focusedRaceNum,
                                  int sessionTimeMs) {
    if (!m_enabled && !m_subtitles) return;
    const bool you = raceNum >= 0 && raceNum == focusedRaceNum;
    // Filed by subject like every other cue: your own crash is your status,
    // someone else's is news about the field.
    const SpotterPhrase::Category cat =
        you ? SpotterPhrase::Category::General
            : SpotterPhrase::Category::Opponents;
    if (!isCategoryEnabled(cat)) return;
    SpotterVars::Vars v;
    v.eventRider = SpotterPhrase::riderRef(raceNum, focusedRaceNum);
    emitCue(you ? "crashed_you" : "crashed_other", cat,
            std::move(v), sessionTimeMs,
            (raceNum >= 0 && raceNum <= 999) ? raceNum : -1);
}

// The held fastest lap, spoken. There is no condition beyond "one is held":
// the CALLER is the condition, because it runs on the classification and a
// held cue is by definition everything that arrived since the last one. See
// PendingFastest for why that identifies a join replay.
void SpotterManager::flushPendingFastestLap() {
    if (!m_pendingFastest.armed) return;
    const PendingFastest p = m_pendingFastest;
    m_pendingFastest = PendingFastest{};
    // Focus RESOLVED AT FLUSH, exactly as the lap report re-checks its rider:
    // the camera can cut between the arm and the classification, and "you're
    // quickest" about the rider the director just left is the wrong subject.
    const int focusedNow = PluginData::getInstance().getDisplayRaceNum();
    if (p.raceNum >= 0 && p.raceNum == focusedNow) {
        // Your OPENING lap as the session's fastest is the out-lap/gate lap
        // every offline session banks first -- the notice ladder deliberately
        // shows nothing for it (race_lap_handler's hadPreviousBest), and the
        // spotter said "Fastest lap, nice work, three oh five point one" at
        // the first crossing of every session. A fact, not news.
        if (bestLapIsFirstLap()) return;
        // After your flag the finish cue has already read your best lap back
        // ("That's the flag, P one, best lap one thirty three point eight");
        // the held fastest followed with the same number. Your race is over
        // -- the same principle that silences the other post-race cues.
        if (subjectRaceOver()) return;
        // The lap REPORT flushes right after this, in the same call. When the
        // held fastest is the lap being reported, its time is spoken HERE --
        // so tell the report, or a lap_completed variant naming
        // {last_lap_time} reads the same number again one line later. The
        // crossing-time ladder latch cannot cover this: online, the fastest
        // lap has no session-best rung to arm it (isFastestLap is
        // online-only), so the report believed the time was never said.
        if (m_pendingLap.armed && m_pendingLap.raceNum == p.raceNum &&
            m_pendingLap.lapTimeMs == p.nums.lapTimeMs) {
            m_pendingLap.timeAlreadySpoken = true;
        }
    }
    m_emittingPendingFastest = true;
    onRaceEvent(EventLogType::FastestLap, p.raceNum, focusedNow, p.nowMs,
                p.nums);
    m_emittingPendingFastest = false;
}

// Called after the classification order is rebuilt, and the home of BOTH
// deferred cues — they are deferred to the same moment for different reasons.
void SpotterManager::flushDeferredCues() {
    // A classification has landed, so the standings' penalty column has had
    // its chance to absorb any clear/revision — the tally may trust it again.
    m_penaltyColumnStale = false;
    // The fastest lap first, so cues stay in the order they happened, and
    // ABOVE the lap-report guard below rather than after it: a rival's fastest
    // lap never arms a lap report, so anything below that early return would
    // never flush one at all.
    flushPendingFastestLap();
    // The held session wrap-up next, ABOVE the lap-report guard for the same
    // reason the fastest lap is: nothing below this line runs unless a report
    // is armed, and a session can end without one.
    if (m_pendingSessionEnd.armed) {
        const PendingSessionEnd pse = m_pendingSessionEnd;
        m_pendingSessionEnd = PendingSessionEnd{};
        m_emittingPendingSessionEnd = true;
        onRaceEvent(EventLogType::SessionComplete, pse.raceNum,
                    PluginData::getInstance().getDisplayRaceNum(), pse.nowMs,
                    pse.nums);
        m_emittingPendingSessionEnd = false;
    }
    if (!m_pendingLap.armed) return;
    m_pendingLap.armed = false;
    if (!m_enabled && !m_subtitles) return;
    if (!isCategoryEnabled(SpotterPhrase::Category::Timing)) return;

    const PluginData& pd = PluginData::getInstance();
    const int raceNum = m_pendingLap.raceNum;
    // Re-checked rather than trusted from the crossing: the camera may have
    // moved on in between, and a report about a rider you are no longer
    // watching is not yours to hear.
    if (raceNum != pd.getDisplayRaceNum()) return;
    // The cool-down lap crosses the line too, and this used to report a
    // position and a gap for it — a race you had already finished.
    if (subjectRaceOver()) return;
    const int position = pd.getPositionForRaceNum(raceNum);
    if (position <= 0) return;

    const int nowMs = m_pendingLap.nowMs;

#if MXBMRP3_SPOTTER_PROBE
    // TEMP-DEBUG(spotter-vs-standings): the S/F half of the probe. Here the
    // classification HAS been rebuilt for the lap just finished (that is what
    // flushDeferredCues waits for), so this is the freshest the table ever is.
    {
        const StandingsData* mineDbg = pd.getStanding(raceNum);
        char where[32];
        snprintf(where, sizeof(where), "S/F lap %d",
                 mineDbg ? mineDbg->numLaps : -1);
        debugLogStandingsAt(pd, where);
    }
#endif

    // The lap did not count. Spoken FIRST, because it changes what the rest of
    // the report means: a time you are about to hear read back is not a time
    // you can compare with anything.
    //
    // This is the only feedback there is outside a race. A race tells you by
    // issuing a penalty (penalty_you, off the communication callback), but
    // practice and qualifying issue none — the lap is simply struck, silently,
    // and you find out by looking at the timing screen.
    // FRESH MEASUREMENTS ONLY for everything this flush speaks: the cached
    // neighbour gaps (m_lastAhead/m_lastBehind) are up to a lap old here, and
    // a report that reads one presents it as the crossing's fact -- both real
    // tapes show "two point zero to rider fifty six" at the line, contradicted
    // seconds later by the fresh behind cue resolving at four point zero. The
    // crossing's own measurement (measureAheadGap, already in the vars) is
    // untouched; only the ambient refill of what it could NOT measure is held
    // back, so the optional groups drop and the dedicated gap cues -- which
    // resolve moments later with the real number -- stand alone.
    m_lapReportFreshGapsOnly = true;
    if (!m_pendingLap.lapValid) {
        SpotterVars::Vars v = m_pendingLap.vars;
        emitCue("lap_invalidated", SpotterPhrase::Category::Timing, std::move(v), nowMs);
    }

    // ONE cue for the crossing, not two. This used to be `position_report`
    // (your position, on by default) plus `lap_completed` (your lap time, off
    // by default because the other one already spoke at the same instant) —
    // two keys for one moment, where the only real difference was WHAT each
    // template happened to say. Since every variable works in every cue, that
    // difference belongs to the template, not to the key: a pack that wants
    // the time says {last_lap_time}, one that wants the position says
    // {position}, and one that wants both says both.
    //
    // `lap_completed` is the name that survived because it names the MOMENT.
    // "position_report" named a particular report — a content choice — which
    // is exactly the thing being made configurable, and the pack format's own
    // rule is that a cue is a moment and everything else is a variable.
    SpotterVars::Vars lapVarsOut = m_pendingLap.vars;
    lapVarsOut.eventTime = SpotterPhrase::lapTimeWordsMs(m_pendingLap.lapTimeMs);
    // THE THIRD TELLING. A lap that was a personal or session best has already
    // had its time spoken by that cue; a lap_completed variant naming
    // {last_lap_time} then said the same number again, immediately after. With
    // the fastest-lap duplicate above that made three in one crossing.
    //
    // Suppressed rather than given its own cue key: every variant that names
    // the time keeps it in an [optional group] precisely so it can drop, so
    // this costs no pack change and a pack that never mentions the time is
    // unaffected. An ordinary lap still reports it -- that is the case the
    // variable is for.
    m_lapReportHideTime = m_pendingLap.timeAlreadySpoken;
    emitCue("lap_completed", SpotterPhrase::Category::Timing, std::move(lapVarsOut), nowMs, -1,
            -1, -1, -1, -1, position);
    m_lapReportHideTime = false;

    // Places made up or lost ON THIS LAP: the position reported at the previous
    // crossing against the one just reported, with the grid standing in for the
    // opening lap. Kept here rather than read from PluginData because both of
    // its references answer a different question:
    //
    //  - getRaceStartPosition is the GRID, so measuring from it made this a
    //    running total announced every lap. On a real Farm 14 race that said
    //    "Up thirteen" on lap 1, then "Up twelve" on lap 2 — a lap the player
    //    had LOST a place on, P7 to P8 — then "Up twelve" again on a lap where
    //    nothing moved. A total is a number, not a moment;
    //    {positions_since_start} is where that number lives.
    //  - getSfReferencePosition is sampled at the crossing itself, from the
    //    classification standing at that instant. A place lost mid-lap is
    //    already in it, so the same Farm 14 lap 2 fell silent instead of
    //    saying "Down one" — the change had happened, just not in the window
    //    that reference measures.
    //
    // Comparing consecutive REPORTS has neither problem, and needs no clock.
    // Tied to the rider it was taken for: the camera can move between laps
    // while spectating, and the previous subject's position is not a reference
    // for this one's.
    int refPos = (m_lastReportedPosNum == raceNum) ? m_lastReportedPos : 0;
    if (refPos <= 0) refPos = pd.getRaceStartPosition(raceNum);
    m_lastReportedPos = position;
    m_lastReportedPosNum = raceNum;
    if (refPos > 0 && refPos != position) {
        SpotterVars::Vars v = m_pendingLap.vars;
        const int delta = refPos - position;
        // Held in a LOCAL, and not read back off `v` in the emitCue call.
        // emitCue takes its Vars BY VALUE, so `std::move(v)` gutts v when that
        // parameter is constructed — and the order function arguments are
        // evaluated in is unspecified, so whether the text below sees
        // the number or an empty moved-from string is up to the compiler.
        // MSVC evaluates right to left and saw the empty one: a shipped log
        // reads `SPOTTER SAY [position_gained] Up .` Every Linux gate passed,
        // because gcc happened to evaluate left to right.
        const std::string changed =
            SpotterPhrase::numberWords(delta < 0 ? -delta : delta);
        v.positionsChanged = changed;
        const bool gained = delta > 0;
        emitCue(gained ? "position_gained" : "position_lost", SpotterPhrase::Category::Timing, std::move(v), nowMs);
    }
    m_lapReportFreshGapsOnly = false;
}

void SpotterManager::onSessionState(int sessionState) {
    m_sessionState = sessionState;
}

// The gate physically dropping, which is NOT the same moment as the session
// becoming active: a standing start holds on the grid after the session flips
// to running, and the gate falls some seconds later. Practice and pit-start
// sessions never have one, which is exactly why it deserves its own key
// rather than a session_started that means two different things.
void SpotterManager::onGateDrop() {
    if (!m_enabled && !m_subtitles) return;
    if (!isCategoryEnabled(SpotterPhrase::Category::General)) return;
    emitCue("gate_drop", SpotterPhrase::Category::General, {},
            PluginData::getInstance().getSessionElapsedTime());
}

void SpotterManager::speakHotkeyCue() {
    // Entirely user-defined: nothing happens until a pack
    // writes `hotkey_triggered = ...`. That is the point — it is a line you
    // compose out of the variables and then hear on demand, which makes it the
    // fastest way to check a template without waiting for the race to produce
    // the event it belongs to.
    if (!m_enabled && !m_subtitles) return;
    emitCue("hotkey_triggered", SpotterPhrase::Category::General, {},
            PluginData::getInstance().getSessionElapsedTime());
}

void SpotterManager::onSpectateTarget(int raceNum) {
    if (!m_enabled && !m_subtitles) return;
    if (raceNum <= 0 || raceNum > 999) return;
    SpotterVars::Vars v;
    v.eventRider = SpotterPhrase::riderRef(raceNum, -1);
    // Opponents, not General: it is about another rider, and that is where the
    // registry, the shipped pack's heading and the reference have always put
    // it. Only the emitter said General, so muting Opponents did not stop it.
    emitCue("spectate_target", SpotterPhrase::Category::Opponents, std::move(v),
            PluginData::getInstance().getSessionElapsedTime(), raceNum);
}

// A rider's most recent lap, in the lap-time words, or "" when they have not
// completed one.
//
// PluginData already keeps a lap log PER RIDER (handleRaceLap fires for
// everyone, not just you), newest first, so this is a read rather than new
// detection -- the same store StandingsHud's Last column reads. Which is also
// why it costs nothing to say: the number is already on screen for anyone who
// turned that column on.
static std::string lastLapWordsFor(int raceNum) {
    const std::deque<LapLogEntry>* log = PluginData::getInstance().getLapLog(raceNum);
    if (!log || log->empty()) return std::string();
    return SpotterPhrase::lapTimeWordsMs(log->front().lapTime);
}

// The BEHIND gap only. The ahead gap stopped being a cue when it turned out
// to fire at exactly the same instant as the lap report — it is variables on
// lap_completed now. This one is a real event: it resolves when the rider behind
// reaches a timing point you already crossed, so the number is a stopwatch
// rather than an estimate, and no other cue marks that moment.
void SpotterManager::measureAheadGap(int myPosition,
                                     const std::vector<int>& order,
                                     long long pointKey, int nowMs,
                                     SpotterVars::Vars& out) {
    if (myPosition < 2 || static_cast<size_t>(myPosition - 2) >= order.size()) {
        return;   // you are leading, or the order does not have you placed
    }
    SpotterPace::Gap gap;
    if (!m_pace.aheadGap(order[myPosition - 2], pointKey, nowMs, gap)) return;
    m_lastAhead = gap;
    out.gapToAhead = SpotterPhrase::gapWordsMs(gap.gapMs);
    // Same rule as emitGapCue: the sentence names the rider the stopwatch
    // timed, not whoever the classification lists in that slot by the time
    // the deferred report speaks.
    out.riderAhead = SpotterPhrase::riderRef(gap.raceNum, -1);
    out.lastLapAhead = lastLapWordsFor(gap.raceNum);
    if (gap.hasTrend) {
        out.trendAhead = SpotterPhrase::trendAheadWord(gap.deltaMs);
        out.gainedOnAhead = SpotterPhrase::gapWordsMs(gap.deltaMs);
    }
}

void SpotterManager::emitGapCue(const SpotterPace::Gap& gap, int sessionTimeMs) {
    m_lastBehind = gap;
    // The rider behind keeps reaching your timing points on their own last lap
    // after you have finished, so this went on calling a gap in a race that was
    // over for you. Recorded, not suppressed: the trend variables stay current
    // for anything that asks, it is only the CUE that has nothing to say.
    if (subjectRaceOver()) return;
    // Keyed by trend so a recorded pack can voice "closing" and "dropping
    // back" differently; both fall back to gap_behind, so a text pack writes
    // one line with {trend_behind} in it.
    const char* key = "gap_behind";
    if (gap.hasTrend) {
        key = gap.deltaMs < 0 ? "gap_behind_closing" : "gap_behind_dropping";
    }
    // sec/tenth are the wav mixer's numeric arguments; the spoken string is the
    // same two numbers, so it comes from the one helper that phrases them.
    const int sec = gap.gapMs / 1000;
    const int tenth = (gap.gapMs % 1000) / 100;
    const std::string words = SpotterPhrase::gapWordsMs(gap.gapMs);
    SpotterVars::Vars vars;
    vars.gapToBehind = words;
    // The NAME travels with the number. {rider_behind} used to come from the
    // live order at emit time, while the stopwatch was taken against whoever
    // was behind when the report armed -- a rider passed between the arm and
    // the resolve spoke as "Behind you, rider twelve, two point one" with 2.1
    // measured against rider forty-seven. Gap::raceNum is who the stopwatch
    // actually timed, so that is who the sentence names.
    vars.riderBehind = SpotterPhrase::riderRef(gap.raceNum, -1);
    vars.lastLapBehind = lastLapWordsFor(gap.raceNum);
    if (gap.hasTrend) {
        vars.trendBehind = SpotterPhrase::trendBehindWord(gap.deltaMs);
        vars.gainedOnBehind = SpotterPhrase::gapWordsMs(gap.deltaMs);
    }
    emitCue(key, SpotterPhrase::Category::Timing, std::move(vars), sessionTimeMs,
            -1, sec, tenth);
}

void SpotterManager::onPersonalBest(int sessionTimeMs, int lapTimeMs) {
    if (!m_enabled && !m_subtitles) return;
    if (!isCategoryEnabled(SpotterPhrase::Category::Timing)) return;
    SpotterVars::Vars v;
    v.eventTime = SpotterPhrase::lapTimeWordsMs(lapTimeMs);

    // A lap that beats the TRACK RECORD is its own moment, and the tier above
    // an all-time PB (it is nearly always both). Highest-applicable-only, the
    // same way the notice ladder in the lap handler works — hearing "personal
    // best" and "track record" back to back would be the redundancy that
    // ladder exists to prevent.
    const int record = trackRecordLapTime();
    const bool beatsRecord = record > 0 && lapTimeMs > 0 && lapTimeMs < record;
    const SpotterPhrase::LapTimeParts pbParts =
        SpotterPhrase::lapTimePartsMs(lapTimeMs);
    emitCue(beatsRecord ? "record_beaten" : "personal_best",
            SpotterPhrase::Category::Timing, std::move(v), sessionTimeMs, -1,
            pbParts.valid ? pbParts.composed : -1,
            pbParts.valid ? pbParts.tenths : -1);
    // Arm the ladder: whatever else THIS lap turns out to be, it has now had
    // its moment. Stored as the lap's time so the suppression can only apply
    // to that lap. See onRaceEvent's FastestLap branch.
    m_higherLapCueTimeMs = lapTimeMs;
}

// True when the subject's best lap of this session is their FIRST lap — which
// is never a lap they could repeat. In a race it carries the gate hold and a
// standing start; in practice it is the roll out of the pits. Farm 14's opener
// was 3:05.1 against a 1:26 field.
//
// It is a real lap time and a real personal best — the game says so, and every
// HUD shows the same figure — so this does NOT touch the data. What it stops is
// treating it as a REFERENCE, which produces a true number that means nothing:
// three rivals' laps were reported as "ninety six point six quicker than your
// best", "ninety seven point five quicker", "ninety eight point six quicker".
// The moment a repeatable lap replaces it the comparison comes back, which is
// exactly when a rider expects it to.
//
// Deliberately NOT applied outside the spotter. The session best comes from the
// game's own classification, and the standings, the timing HUD and the web
// overlay all show what the game reports; a plugin that quietly disagreed with
// the game about your best lap would be the worse bug. This is a judgement
// about what is worth SAYING, which is the spotter's job alone.
bool SpotterManager::bestLapIsFirstLap() const {
    const PluginData& pd = PluginData::getInstance();
    // The classification's own index, which the game fills and documents as
    // 1-based (unified_types.h) and which already excludes invalid laps.
    // Deliberately NOT the lap log's entry: LapLogEntry::lapNum says "1-based"
    // in its comment but race_lap_handler builds it from
    // completedLapNumZeroIndexed, so an opening lap reads 0 there.
    const StandingsData* mine = pd.getStanding(pd.getDisplayRaceNum());
    return mine && mine->bestLapNum == 1;
}

// True once the rider the cues are ABOUT has stopped racing — took the flag,
// retired, was disqualified or never started. Everything a spotter says about
// your race is a lie after that point, and the tapes are full of it: the demo
// weekend spoke "P two, twenty point zero to rider ninety nine, losing" and
// "Behind, five point five, dropping back" AFTER the checkered flag, on the
// cool-down lap; Farm 14 kept calling gaps after the player had retired.
//
// The mirror image of the pre-start grid grace in onTrackPositions, and quiet
// for the same reason — not racing, so the numbers describe nothing.
//
// Deliberately NOT applied to session-level cues (the leader's flag, the final
// lap, the session ending) or to other riders' news: those stay true whatever
// your own race did. And absent standings mean absent evidence, not a finish.
bool SpotterManager::subjectRaceOver() const {
    const PluginData& pd = PluginData::getInstance();
    const int me = pd.getDisplayRaceNum();
    if (me <= 0) return false;
    const StandingsData* s = pd.getStanding(me);
    if (!s) return false;
    if (s->finishTime >= 0 || s->sessionFinished) return true;
    // Note the deliberate absence of a pit clause, which is what makes this a
    // third question rather than a copy of PluginData's two rider-state
    // predicates: isRiderExcludedFromDetection asks who can be flagged as a
    // hazard and isRiderSpectatable who the camera can land on, and both count
    // the pits as out. You can sit in the pits and still be racing.
    return s->state == static_cast<int>(Unified::EntryState::DNS) ||
           s->state == static_cast<int>(Unified::EntryState::Retired) ||
           s->state == static_cast<int>(Unified::EntryState::DSQ);
}

// The always-available half of the variable set. Read from live state at cue
// time — that is the whole contract (spotter_vars.h): a template may name any
// of these in any cue, and one that does not apply right now resolves to
// nothing rather than to a literal "{gap_to_ahead}".
//
// COST: a handful of standings lookups per CUE, which is a few per lap, not
// per frame. Nothing here belongs on the Draw path.
//
// The live gaps here and the gap_* CUES are deliberately different numbers and
// both are right: a cue reports the STOPWATCH value from the last shared
// timing point (spotter_pace.h), which is what a spotter says at the line;
// {gap_to_ahead} is where the gap is NOW, which is what a template asking mid-cue
// wants. The trend pair comes from the pace tracker, so it moves at crossings.
void SpotterManager::fillAmbientVars(SpotterVars::Vars& v) const {
    // NEVER overwrite a value the emitter already set. Where both have an
    // answer the EVENT's is the better one: the gap cues carry the stopwatch
    // gap from the last shared timing point, and clobbering that with the live
    // estimate computed below would quietly replace a measurement with a
    // guess. Assign through this rather than to the field directly.
    auto fill = [](std::string& field, std::string value) {
        if (field.empty()) field = std::move(value);
    };
    const PluginData& pd = PluginData::getInstance();
    const SessionData& sd = pd.getSessionData();

    if (sd.trackName[0] != '\0') fill(v.trackName, sd.trackName);
    // The same label the Session HUD shows — "Race 1", "Warmup", "Qualify
    // Practice" — rather than a second, coarser mapping of the same enum.
    if (const char* label = PluginUtils::getSessionString(sd.eventType,
                                                         sd.session)) {
        fill(v.sessionName, label);
    }
    // "Waiting", "Sighting Lap", "Pre-Start", "In Progress", "Complete",
    // "Race Over", "Cancelled" — the same words the Session HUD shows.
    if (const char* st = PluginUtils::getSessionStateString(m_sessionState)) {
        fill(v.sessionState, st);
    }

    // Session length and what is left of it, in TWO shapes. A session is
    // measured in laps or in time depending on how it was set up, so the
    // specific variables answer only when they apply, while the generic pair
    // answers in whichever unit this session actually uses — that is what a
    // template can rely on everywhere.
    auto minuteWords = [](int ms) {
        const int minutes = ms / 60000;
        // Whole minutes is how session lengths are set and how a rider thinks
        // about them — but the truncation has a floor, and below it the answer
        // is nonsense: a short qualifying session announced itself as "Qualify
        // underway, ZERO MINUTES". Under a minute, say the seconds.
        if (minutes <= 0) return SpotterPhrase::durationWords(ms);
        return SpotterPhrase::numberWords(minutes) +
               (minutes == 1 ? " minute" : " minutes");
    };
    // A gap is in SECONDS while you are on the same lap and in LAPS once you
    // are not — which is what a spotter says, and the only reading of the
    // number that is true.
    auto gapWords = [](int laps, int ms) {
        return laps > 0 ? SpotterPhrase::lapsWords(laps)
                        : SpotterPhrase::gapWordsMs(ms);
    };
    // THREE session shapes, not two, and the third is the common one in MX
    // Bikes: a race is usually "10 minutes + 2 laps" — a clock, then that many
    // bonus laps once it expires. Reporting only the clock drops half the
    // format, so both halves are spoken, the same way the event log writes
    // "03:00 + 2L".
    //
    // In a time+laps race sessionNumLaps is the BONUS lap count, not the race
    // distance. That is what makes {laps_remaining} below a lap-race-only
    // figure: subtracting laps completed from a bonus count is arithmetic on
    // two different things.
    const bool timed = sd.sessionLength > 0;
    const bool lapped = sd.sessionNumLaps > 0;
    // Nothing is REMAINING until the session is running. On the grid the full
    // distance is still ahead, so the subtraction below returns it verbatim and
    // a template reads it as a countdown: leaving the pits before a four-lap
    // race, "Pit exit, up to speed. Race 2, FOUR LAPS LEFT" — forty-five
    // seconds before the start, and directly after session_prestart had just
    // said "Race 2 starting, four laps". {session_length} is the variable for
    // that sentence; the remaining trio answers only once there is a difference
    // between the two.
    const bool running =
        (sd.sessionState & PluginConstants::SessionState::IN_PROGRESS) != 0;
    auto withBonusLaps = [&](const std::string& timePart) {
        if (!lapped) return timePart;
        // Bonus laps are a SUFFIX on a clock, never a figure of their own. With
        // no time part they read as the whole answer, which is how a fresh
        // 8min+1lap race announced itself as "eight minutes plus one lap, ONE
        // LAP LEFT" — the clock had not arrived yet, so {session_remaining}
        // fell through to the bonus count. Empty in, empty out: the optional
        // group drops and the line simply omits what is not known yet.
        if (timePart.empty()) return std::string();
        return timePart + " plus " + SpotterPhrase::lapsWords(sd.sessionNumLaps);
    };
    if (timed) {
        fill(v.sessionLength, withBonusLaps(minuteWords(sd.sessionLength)));
        const int remainMs = pd.getSessionTime();
        if (running && remainMs > 0) fill(v.timeRemaining, minuteWords(remainMs));
        fill(v.sessionRemaining, withBonusLaps(v.timeRemaining));
    } else if (lapped) {
        fill(v.sessionLength, SpotterPhrase::lapsWords(sd.sessionNumLaps));
    }

    const int me = pd.getDisplayRaceNum();
    if (me <= 0) return;

    // Your name as the game has it. UTF-8, so a name with accents reads
    // correctly through text-to-speech but garbles in the SUBTITLE — the
    // in-game font is a byte-indexed CP1252 table (see CLAUDE.md), which is a
    // renderer limit rather than something to fix here.
    if (const RaceEntryData* entry = pd.getRaceEntry(me)) {
        if (entry->name[0] != '\0') {
            // Rating servers prefix the display name ("B1 | Thomas"), and TTS
            // read the tag out. The name is what follows the LAST " | "; a name
            // without the separator is untouched.
            //
            // DELIBERATELY NOT matchRiderName's rule, which splits on the FIRST
            // separator and strips only when the prefix is alphanumeric. That
            // one answers "is this entry the player?", where a wrong strip
            // misidentifies a rider, so it is conservative. This one answers
            // "what should be SPOKEN", where the cost of not stripping is the
            // tag read aloud: for "Team A | B1 | Thomas" the conservative rule
            // strips nothing (the first prefix has a space, so it fails the
            // alphanumeric test) and TTS would say the pipes. Same input,
            // different question -- so the rules differ on purpose. Identity
            // matching is not done here at all; the rider is already resolved
            // by getDisplayRaceNum().
            const char* name = entry->name;
            if (const char* sep = std::strstr(name, " | ")) {
                while (const char* more = std::strstr(sep + 3, " | ")) sep = more;
                if (sep[3] != '\0') name = sep + 3;
            }
            fill(v.riderName, name);
        }
    }

    const int pos = pd.getPositionForRaceNum(me);
    // {position} withheld from the session wrap-up when the finish cue has
    // already read it out this session -- "Checkered flag, P two, ..." then
    // "That's Race 1 done, P two" was the same number twice in two breaths.
    // The optional group drops and the wrap-up closes the session, not the
    // scoreboard.
    if (pos > 0 && !m_sessionEndHidePosition) {
        fill(v.position, SpotterPhrase::numberWords(pos));
    }

    const StandingsData* mine = pd.getStanding(me);
    if (mine) {
        fill(v.lapNumber, SpotterPhrase::numberWords(mine->numLaps + 1));
        fill(v.bestLapTime, SpotterPhrase::lapTimeWordsMs(mine->bestLap));
        // The standings' penalty column, in the same words as one penalty's
        // own amount. Rounded the way the web snapshot rounds it, so the two
        // never disagree by a second on the same figure.
        if (mine->penalty > 0) {
            fill(v.penaltyTotal,
                 SpotterPhrase::secondsWords((mine->penalty + 500) / 1000));
        }
        // A LAPPED rider's gap in milliseconds says nothing (standings carry
        // the real answer in gapLaps), so a seconds figure there would be a
        // confidently wrong number rather than a missing one.
        //
        // RACE ONLY, for the same reason the ahead/behind gaps are — see the
        // block below. A logged warmup ended with "Warmup complete, you
        // finished P eleven, SIXTY SIX POINT FOUR off the lead", and the
        // fastest lap of that warmup was one oh six point four: with no valid
        // lap of your own the column had handed back a figure of the leader's
        // lap time, not a gap to anybody.
        if (pos > 1 && pd.isRaceSession()) {
            fill(v.gapToLeader, gapWords(mine->gapLaps, mine->gap));
        }
        // Lap races ONLY: there sessionNumLaps is the distance, so laps left
        // is distance minus laps done. In a time+laps race it is the bonus
        // count instead, and that subtraction would report a number that
        // means nothing (2 - 5 on lap five). Overtime supplies the real
        // figure below, from the leader.
        if (running && lapped && !timed) {
            const int left = sd.sessionNumLaps - mine->numLaps;
            if (left > 0) {
                fill(v.lapsRemaining, SpotterPhrase::numberWords(left));
                fill(v.sessionRemaining, SpotterPhrase::lapsWords(left));
            }
        }
    }
    // Overtime overrides the arithmetic above: once the clock has expired the
    // laps remaining are the leader's, not a lap count nobody is running to.
    const int toGo = pd.getLeaderLapsToGo();
    if (toGo > 0) {
        fill(v.lapsRemaining, SpotterPhrase::numberWords(toGo));
        fill(v.sessionRemaining, SpotterPhrase::lapsWords(toGo));
    }

    // The setup you are on. The game reports a stock setup as "Default" and an
    // unnamed one as empty, which mean the same thing to a rider — so empty is
    // normalised rather than left blank, otherwise the one case worth warning
    // about is the one that says nothing.
    if (sd.setupFileName[0] != '\0') {
        // The game hands this over as a raw setup FILE name, and it arrives
        // prefixed: a real session logged ":Husk f". The part before the colon
        // is empty here and is not the setup's name in any case, so only the
        // human half is spoken. Conservative on purpose — a LEADING colon is
        // the artefact, so a setup genuinely called "Husk: fast" keeps its
        // name, and nothing is stripped from a name without one.
        const char* name = sd.setupFileName;
        if (*name == ':') {
            ++name;
            while (*name == ' ') ++name;
        }
        // A name that was ONLY the prefix falls through to "Default" below,
        // same as an empty one: both mean "not a setup I chose".
        if (*name != '\0') fill(v.setupName, name);
    }
    // The game reports a stock setup as "Default" and an unnamed one as empty,
    // which mean the same thing to a rider — so empty is normalised rather
    // than left blank, otherwise the one case worth warning about is the one
    // that says nothing.
    fill(v.setupName, "Default");

    // Your total race time, present only once you have actually finished:
    // it is what the classification carries at the flag, not a running clock.
    if (mine && mine->finishTime > 0) {
        fill(v.finishTime, SpotterPhrase::durationWords(mine->finishTime));
    }

    const IdealLapData* ideal = pd.getIdealLapData();
    if (ideal && !m_lapReportHideTime) {
        fill(v.lastLapTime, SpotterPhrase::lapTimeWordsMs(ideal->lastLapTime));
    }

    // Laps left in the tank, from the same history the Fuel widget shows —
    // documented as an ambient variable ("read from the live race at the
    // moment a cue fires") but only ever set by the fuel warnings, so
    // `hotkey_triggered = ...[, {fuel_laps} in the tank]` always dropped its
    // group. One source, one wording (lapsWords carries the noun).
    {
        const float fuelLaps =
            HudManager::getInstance().getFuelWidget().getLapsRemaining();
        if (fuelLaps >= 0.0f) {
            fill(v.fuelLaps,
                 SpotterPhrase::lapsWords(static_cast<int>(fuelLaps)));
        }
    }

    // The five reference laps TimingHud compares against, as times and as
    // your last lap's gap to each. Resolved from the same sources it uses so
    // the spotter cannot disagree with what is on screen; any of them being
    // absent (a track you have never ridden, an ideal lap with a sector
    // missing, a game with no records provider) leaves both variables empty,
    // which an optional group then drops.
    const int lastLap = ideal ? ideal->lastLapTime : -1;
    auto reference = [&](int refMs, std::string& timeOut, std::string& gapOut) {
        if (refMs <= 0) return;
        timeOut = SpotterPhrase::lapTimeWordsMs(refMs);
        if (lastLap <= 0) return;
        const int d = lastLap - refMs;
        // A lap compared with ITSELF says nothing. When the lap that just
        // finished IS the new best, the reference has already absorbed it and
        // the difference is zero -- which spoke as "zero point zero slower than
        // your best" on the very lap the session-best cue was celebrating.
        // Empty instead, so the optional group drops and the good news stands
        // on its own. bestLapIsFirstLap() guards the other end of the same
        // idea; this is the tie.
        if (d == 0) return;
        // The direction word rides WITH the number: spoken aloud, a bare
        // "point three" does not say which way it went, and making every
        // template add the word would be the same clause written many times.
        gapOut = SpotterPhrase::gapWordsMs(d) +
                 (d < 0 ? " quicker" : " slower");
    };
    // The lap BEFORE the last one, which is what "gap to last lap" compares
    // your latest against. Walked back through the log rather than kept as a
    // member: invalid laps are skipped, the same way TimingHud's Last Lap row
    // skips them, so the comparison is always against a lap that counted.
    if (const std::deque<LapLogEntry>* log = pd.getLapLog()) {
        int seen = 0;
        for (auto it = log->rbegin(); it != log->rend(); ++it) {
            // Both halves of "a lap that counted": a time exists AND the lap
            // was valid. Race-session cut laps arrive with the time preserved
            // and isValid=false, and skipping only the timeless ones made a
            // cut 1:20 the reference — the next clean lap read "twenty eight
            // point four slower" as {gap_to_last_lap}.
            if (it->lapTime <= 0 || !it->isValid) continue;
            if (++seen == 2) {
                // {last_lap_time} is the LAST lap; this one is the lap
                // before it, so only the gap half is wanted here.
                std::string unused;
                reference(it->lapTime, unused, v.gapToLastLap);
                break;
            }
        }
    }
    // {best_lap_time} is a FACT and is always filled; {gap_to_best_lap} is a
    // comparison, and an opening lap is not something to compare against
    // (bestLapIsFirstLap). Routed through a scratch string rather than skipped
    // so the time still reaches templates that ask for it.
    {
        std::string unusedGap;
        std::string& gapOut = bestLapIsFirstLap() ? unusedGap : v.gapToBestLap;
        if (const LapLogEntry* pb = pd.getBestLapEntry()) {
            reference(pb->lapTime, v.bestLapTime, gapOut);
        } else if (mine) {
            reference(mine->bestLap, v.bestLapTime, gapOut);
        }
    }
    if (ideal) reference(ideal->getIdealLapTime(), v.idealLapTime, v.gapToIdeal);
    reference(pd.getOverallBestLapTime(), v.overallBestTime, v.gapToOverall);
    reference(allTimeBestLapTime(), v.alltimeBestTime, v.gapToAlltime);
    reference(trackRecordLapTime(), v.recordTime, v.gapToRecord);

    // The riders either side, by classification order.
    //
    // ONE RULER for a gap in seconds: the stopwatch — their crossing of a
    // timing point against yours (spotter_pace.h). There used to be a second,
    // the difference between two riders' official gaps to the leader, served
    // here whenever the stopwatch had nothing. It was never the same
    // measurement, and the differences are not academic:
    //
    //   - each rider's gap column is refreshed at THEIR own crossing, so
    //     subtracting two of them only means something while both are fresh
    //     and on the same lap — which nothing here can check. A logged race
    //     had you at 566 and a rider one lap up at 31378 on the same table;
    //   - outside a race it is not a rider-to-rider figure at all (a warmup
    //     showed it NEGATIVE at -61925, then frozen at 2407/2259/2757 across
    //     six crossings, with gapLaps flat 0 for a rider two laps up), which
    //     is why it needed a race gate the stopwatch never did;
    //   - and the two cannot be compared, so a trend could not span them. Of
    //     three lap reports in one logged race, two spoke the estimate and one
    //     the stopwatch, and the trend was silent all race because of it.
    //
    // So the cached MEASUREMENT is what a cue between timing points serves,
    // and only while it still describes the rider currently there — which is
    // what Gap::raceNum is for. No measurement, or a new neighbour since it,
    // means the variable stays empty and the optional group drops whole,
    // rather than quoting a softer number in the same sentence shape.
    //
    // {rider_ahead}, {position_ahead} and their behind counterparts are just
    // the classification order and stay filled either way — they are as true
    // in practice as in a race. Only the seconds come from the stopwatch.
    auto lapsDown = [](const StandingsData* s) { return s ? s->gapLaps : 0; };
    const std::vector<int>& order = pd.getClassificationOrder();
    // Being a LAP down is a different question from being N seconds back, and
    // the standings answer it directly rather than by subtraction. Race-only
    // for the reason above; a false 0 there costs a phrase, never a wrong one.
    const bool lapsAreReal = pd.isRaceSession();
    auto measured = [](const SpotterPace::Gap& g, int num) {
        return g.raceNum == num && g.gapMs > 0
                   ? SpotterPhrase::gapWordsMs(g.gapMs) : std::string();
    };
    int aheadNum = -1, behindNum = -1;
    if (pos >= 2 && static_cast<size_t>(pos - 2) < order.size()) {
        const int num = order[pos - 2];
        aheadNum = num;
        fill(v.positionAhead, SpotterPhrase::numberWords(pos - 1));
        fill(v.riderAhead, SpotterPhrase::riderRef(num, -1));
        // Their last lap, from the same rider the NAME came from -- the live
        // order here, since this path has no stopwatch reading to be loyal to.
        // Unguarded by m_lapReportFreshGapsOnly on purpose: that switch is about
        // a cached GAP being read as a fresh crossing's fact, and a completed lap
        // time is not an estimate that goes stale between timing points.
        fill(v.lastLapAhead, lastLapWordsFor(num));
        const int lapDiff = lapsDown(mine) - lapsDown(pd.getStanding(num));
        if (lapsAreReal && lapDiff > 0) {
            fill(v.gapToAhead, SpotterPhrase::lapsWords(lapDiff));
        } else if (!m_lapReportFreshGapsOnly) {
            // Cached stopwatch reading, identity-guarded by Gap::raceNum --
            // right for a cue BETWEEN timing points (a hotkey ask mid-lap),
            // and held back for the lap report, which speaks at a crossing
            // and must not read a lap-old number as that crossing's fact.
            fill(v.gapToAhead, measured(m_lastAhead, num));
        }
    }
    if (pos >= 1 && static_cast<size_t>(pos) < order.size()) {
        const int num = order[pos];
        behindNum = num;
        fill(v.positionBehind, SpotterPhrase::numberWords(pos + 1));
        fill(v.riderBehind, SpotterPhrase::riderRef(num, -1));
        fill(v.lastLapBehind, lastLapWordsFor(num));
        const int lapDiff = lapsDown(pd.getStanding(num)) - lapsDown(mine);
        if (lapsAreReal && lapDiff > 0) {
            fill(v.gapToBehind, SpotterPhrase::lapsWords(lapDiff));
        } else if (!m_lapReportFreshGapsOnly) {
            fill(v.gapToBehind, measured(m_lastBehind, num));
        }
    }

    // Trends come from the pace tracker's last resolved report, so they mean
    // "since the last shared timing point" rather than "since the last frame"
    // — a per-frame trend on a noisy estimate would flip constantly.
    //
    // GUARDED BY raceNum, exactly as the gaps above are. A cached reading
    // outlives the crossing that produced it, and the rider ahead changes; with
    // no guard the DELTA came from the last rider measured while the NAME came
    // from the live standings, so the two described different people. A logged
    // race said "two point four gaining on rider twenty nine" at five
    // consecutive splits across two laps -- one reading taken against rider
    // twelve, re-attributed four times. A gap delta cannot repeat exactly, so
    // the tell was there to hear; nothing in the code could see it, because
    // Gap::raceNum was carried for this and only the gaps consulted it.
    if (!m_lapReportFreshGapsOnly &&
        m_lastAhead.hasTrend && m_lastAhead.raceNum == aheadNum) {
        fill(v.trendAhead, SpotterPhrase::trendAheadWord(m_lastAhead.deltaMs));
        fill(v.gainedOnAhead, SpotterPhrase::gapWordsMs(m_lastAhead.deltaMs));
    }
    if (!m_lapReportFreshGapsOnly &&
        m_lastBehind.hasTrend && m_lastBehind.raceNum == behindNum) {
        fill(v.trendBehind, SpotterPhrase::trendBehindWord(m_lastBehind.deltaMs));
        fill(v.gainedOnBehind, SpotterPhrase::gapWordsMs(m_lastBehind.deltaMs));
    }

    if (!order.empty()) fill(v.leaderName, SpotterPhrase::riderRef(order[0], me));

    // Places against each of StandingsHud's three PosGain references, so a
    // template names the one it means. Each is -1 where it does not apply —
    // no grid outside a race, no crossing before your first one — and stays
    // empty rather than reporting a change against nothing.
    auto places = [&](int refPos, std::string& out) {
        if (refPos <= 0 || pos <= 0 || refPos == pos) return;
        const int d = refPos - pos;
        fill(out, SpotterPhrase::numberWords(d < 0 ? -d : d));
    };
    places(pd.getRaceStartPosition(me), v.positionsSinceStart);
    places(pd.getSfReferencePosition(me), v.positionsSinceLap);
    places(pd.getSplitReferencePosition(me), v.positionsSinceSector);
}

void SpotterManager::emitCue(const char* key, SpotterPhrase::Category cat,
                             SpotterVars::Vars vars, int sessionTimeMs,
                             int riderNum, int timeComposed, int timeTenths,
                             int penaltySecs, int lapsLeft, int posValue) {
    // THE category gate. It lived at each emitter instead, which is to say it
    // was reimplemented twenty times — and three of those drifted: fuel never
    // checked at all, the hotkey cue skipped the check its sibling gate_drop
    // applies, and spectate_target checked General while the registry, the
    // shipped pack and the generated reference all filed it under Opponents.
    // Every one of them left a switch in the settings menu that did not
    // silence what it named.
    //
    // Here it cannot drift: the category a cue is emitted AS is now the same
    // one that mutes it, by construction. Emitters may still gate early to
    // skip expensive work — that is an optimisation now, not the contract.
    if (!isCategoryEnabled(cat)) {
#if defined(MXBMRP3_TEST_BUILD)
        // Swept with the other two returns rather than left because no test reads
        // it today: a route left standing from the last audible cue is the same
        // stale-seam class either way, and "no test reads it" is a fact about
        // today's tests, not about the seam.
        m_lastAudioRoute = std::string(key) + "|muted";
#endif
        return;
    }
    // The ambient half, read from live state rather than carried by the event
    // — this is what makes every variable usable in every cue. Filled here so
    // no emitter has to remember to, and so a new variable reaches every
    // template at once (spotter_vars.h).
    if (posValue > 0 && vars.position.empty()) {
        vars.position = SpotterPhrase::numberWords(posValue);
    }
    // ONE carve-out from that: penalty_you owns {penalty_total} outright,
    // including owning it when it decides to be EMPTY (see the Penalty branch
    // in onRaceEvent — a first penalty would only repeat the amount). The
    // ambient half reads the same standings column the tally exists to
    // outrun, and "empty" is indistinguishable from "unset" to it, so it
    // would put back exactly what that branch just declined to say.
    const bool ownsPenaltyTotal = std::strcmp(key, "penalty_you") == 0;
    std::string ownPenaltyTotal;
    if (ownsPenaltyTotal) ownPenaltyTotal = vars.penaltyTotal;
    fillAmbientVars(vars);
    if (ownsPenaltyTotal) vars.penaltyTotal = std::move(ownPenaltyTotal);
    // Variant pick: the base key plus any <key>_2.. alternates the pack
    // defines, chosen per firing (xorshift — statistical variety, not
    // crypto). With no pack variants this collapses to the base key.
    const std::vector<std::string> variants =
        SpotterCuePack::variantKeys(m_pack, key);
    std::string chosen = key;
    if (variants.size() > 1) {
        m_rngState ^= m_rngState << 13;
        m_rngState ^= m_rngState >> 17;
        m_rngState ^= m_rngState << 5;
        size_t pick = m_rngState % variants.size();
#if defined(MXBMRP3_TEST_BUILD)
        // Rolled anyway above, so the RNG advances identically whether or not
        // a test is pinning — a pinned case cannot shift what a later
        // unpinned one sees (see testPinVariant).
        if (m_pinVariant >= 0) {
            pick = static_cast<size_t>(m_pinVariant) < variants.size()
                 ? static_cast<size_t>(m_pinVariant)
                 : variants.size() - 1;
        }
#endif
        chosen = variants[pick];
    }

    // Text resolution: the chosen variant's phrase, else the base key's (a
    // wav-only variant inherits its subtitle). An empty pack value is an
    // explicit mute. The subtitle shows this text whichever backend plays.
    std::string text;
    auto it = m_pack.phrases.find(chosen);
    if (it == m_pack.phrases.end() && chosen != key) {
        it = m_pack.phrases.find(key);
    }
    // ...then the key this one refines, so a pack that wrote `gap_ahead` with
    // {trend_ahead} in it covers the trend cases too (spotter_cue_pack.h).
    if (it == m_pack.phrases.end()) {
        if (const char* base = SpotterCuePack::fallbackCueKey(key)) {
            it = m_pack.phrases.find(base);
        }
    }
    if (it != m_pack.phrases.end()) {
        text = SpotterCuePack::expand(it->second, vars);
    }
    // No row anywhere means SILENCE. There is deliberately no built-in to fall
    // back to — see reloadCuePack for what removing that copy bought.

    // Audio resolution ladder: mix (chunk stitch, when every placeholder has
    // a value) > wav > TTS — the chosen variant's OWN audio only (inherited
    // audio would defeat the variation). Chunk EXISTENCE is checked on the
    // worker (no file I/O here); the recipe carries the text as its fallback.
    //
    // Both lookups take the SAME refinement fallback the phrase takes, and for
    // the same contract (spotter_cue_pack.h: "a pack that says only
    // `session_started` means it for all of them"). Without it a pack that
    // wrote the general key got the general WORDS with no audio and dropped to
    // TTS — silence under Wine, which is the one place the recorded clip is
    // not a luxury.
    //
    // ONLY WHEN NO VARIANT WAS PICKED, which is the whole difference between
    // the two axes. The refinement axis is practice_started → session_started;
    // the VARIANT axis is practice_started_2, and the paragraph above says a
    // variant never borrows. Applied to a chosen variant this walked PAST the
    // variant's own parent to the general key — so a pack with
    // `practice_started_wav` and a `practice_started_2` text variant played the
    // session_started clip under the practice_started_2 subtitle, which is the
    // one outcome worse than falling back to TTS. When a variant is in play the
    // pack authored variants for this key and owns their audio; no audio for
    // the one that came up means TTS, deliberately.
    const char* base = (chosen == key) ? SpotterCuePack::fallbackCueKey(key) : nullptr;
    std::vector<std::string> mixFiles;
    auto mx = m_pack.mixes.find(chosen);
    if (mx == m_pack.mixes.end() && base) mx = m_pack.mixes.find(base);
    if (mx != m_pack.mixes.end()) {
        mixFiles = SpotterMix::resolveTokens(mx->second, riderNum,
                                             timeComposed, timeTenths,
                                             penaltySecs, lapsLeft, posValue);
    }
    const std::string* wavName = nullptr;
    auto wv = m_pack.wavs.find(chosen);
    if (wv == m_pack.wavs.end() && base) wv = m_pack.wavs.find(base);
    if (wv != m_pack.wavs.end() && !wv->second.empty()) {
        wavName = &wv->second;
    }
    // A template whose every variable came back empty expands to its own
    // PUNCTUATION — "[{rider_behind} is on your tail]." leaves ".", and
    // "P {position}." leaves "P." — because expand() tidies spacing but has
    // nothing to remove the fixed characters around the hole. Silence is what
    // an all-empty line means, so anything with no letter or digit in it is
    // treated as the empty string rather than queued for a voice to read.
    if (!text.empty()) {
        bool speakable = false;
        for (unsigned char c : text) {
            if (std::isalnum(c) || c >= 0x80) { speakable = true; break; }
        }
        if (!speakable) text.clear();
    }
#if defined(MXBMRP3_TEST_BUILD)
    // RECORDED HERE, above both returns, because both are answers: the silent
    // return below is the "this cue plays nothing" case, and the spoken-audio
    // gate further down is subtitles-only mode. Written after them, "silent" was
    // unreachable and a muted-audio session reported whatever the last audible
    // cue had chosen — a seam that goes stale is worse than no seam, because a
    // test reads it as an answer.
    m_lastAudioRoute = chosen + "|";
    if (!mixFiles.empty()) {
        m_lastAudioRoute += "mix:";
        for (size_t i = 0; i < mixFiles.size(); ++i) {
            if (i) m_lastAudioRoute += "+";
            m_lastAudioRoute += mixFiles[i];
        }
    } else if (wavName) {
        m_lastAudioRoute += "wav:" + *wavName;
    } else if (!text.empty()) {
        m_lastAudioRoute += "tts";
    } else {
        m_lastAudioRoute += "silent";
    }
#endif
    if (text.empty() && mixFiles.empty() && !wavName) return;  // silent

#if MXBMRP3_SPOTTER_PROBE
    // TEMP-DEBUG(spotter-vs-standings): every spoken cue, interleaved with the
    // regular log so a session reads as a transcript against the events that
    // produced it. Normally #ifdef MXBMRP3_TEST_BUILD; unguarded on purpose so
    // it appears in a RELEASE build alongside the STANDINGS probe below, which
    // is the whole point of the pairing — you cannot compare what the spotter
    // said with what the table showed if only one of them logs.
    // DELIBERATELY STILL HERE, by the author's decision: the spotter's timing is
    // still being retraced against the standings table, and this pairing is how
    // that is read. Reviews have flagged it as an oversight on four consecutive
    // passes, so it is written down — a choice with a known cost, not a leftover.
    //
    // REMOVE BEFORE RELEASE. The grep is the authority; this is the list, because
    // a bare number has been wrong three times running ("both", then "four", then
    // "five") and each time LOW:
    //
    //   1. this line (SPOTTER SAY)
    //   2. debugLogStandingsAt's FORWARD DECLARATION, above onRaceEvent — the one
    //      every count so far has missed, and the one that does not fail quietly:
    //      delete the definition without it and a static function is declared and
    //      never defined, which the cross build takes as an error.
    //   3. its definition
    //   4. its call in the event tap (the finish)
    //   5. its call in the split tap
    //   6. its call in the lap report
    //
    // COST, since these are staying a while: it is EVENT-rate, not frame-rate —
    // roughly four splits, a lap report and a handful of cues per lap — so the
    // 480fps steady state is not the worry. Each line is a mutex-held write with
    // an explicit flush on the GAME THREAD, so each one is a frame hitch where it
    // lands, and run_perf.sh drives no cues and cannot see them.
    DEBUG_INFO_F("SPOTTER SAY [%s] %s", chosen.c_str(), text.c_str());
#endif

    if (!text.empty()) {
        {
            MutexLock lock(m_mutex);
            m_cueLog.push_back({text, cat, sessionTimeMs});
            if (m_cueLog.size() > kCueLogCapacity) m_cueLog.pop_front();
        }
        m_cueLogRevision.fetch_add(1, std::memory_order_relaxed);
    }

    // Audio only with the spoken-audio master on: the cue log above is what
    // the subtitle widget shows, so subtitles-only mode ends here.
    if (!m_enabled) return;

    // Where a rider IS decays; what a rider DID does not. The proximity calls
    // are marked so the queue drops them rather than delivering them behind a
    // backlog — see isPerishableCue and spotter_queue.h.
    const bool perish = SpotterCuePack::isPerishableCue(key);

    if (!mixFiles.empty()) {
        SpotterCue cue;
        cue.kind = SpotterCue::Kind::MixSpec;
        cue.payload = text;            // the TTS fallback, if a chunk is missing
        cue.mixDir = m_packDir;
        cue.mixChunks = std::move(mixFiles);
        cue.perishable = perish;
        cue.enqueuedMs = GetTickCount64();
        enqueue(std::move(cue));
    } else if (wavName) {
        playWav(m_packDir + "\\" + *wavName, perish);
    } else {
        say(text, perish);
    }
}

// The voice_preview's fixed sample data. Not arbitrary: the point of a voice_preview is to
// expose what actually DIFFERS between packs, which is the stitching, not the
// timbre of a single word. A three-digit rider number and a lap time both come
// out of the number chunks, and since every pack ships the num_0..99 split set,
// 965 becomes "nine"+"sixty five" and 132 becomes "one"+"thirty two" — four
// joins in one line, which is exactly where a pack's `[Mix] gap_ms` is audible.
// A round number like 100 or a time like 1:30.0 would demonstrate none of it.
constexpr int kPreviewRider = 965;      // -> rider nine sixty five
constexpr int kPreviewComposed = 132;   // -> one thirty two  (1:32)
constexpr int kPreviewTenths = 4;       // -> point four

void SpotterManager::previewVoice(bool ttsOnly) {
    // Silent modes stay silent: this fires from a settings click, and a click
    // that makes noise with spoken audio off would be a bug, not a voice_preview.
    if (!m_enabled) return;

    // A preview REPLACES the one before it. Clicking through the voice list is
    // one click per voice and the previews queued behind each other, so a walk
    // down fifteen voices left the spotter reading fifteen samples out — in
    // fifteen different voices — long after the menu was closed. Nobody wants
    // to hear the ones they clicked past; they want the one they landed on.
    //
    // The queue is dropped whole rather than filtered for previews: nothing
    // else can be pending at a settings click except cues from a race that is
    // paused behind the menu, and those are stale by the time it closes.
    //
    // Both under ONE lock, with the flag set beside the clear rather than
    // after it. Apart, there is a window: the worker pops a normal cue and
    // releases the lock, then this runs before the worker reaches its own
    // clear — which then wipes the flag and speaks that cue to the end, with
    // the preview queued behind it. Together the two orderings are mutually
    // exclusive: either the worker popped first (the flag survives and
    // correctly interrupts what it just started) or this ran first (there is
    // nothing left to pop). The flag stays ATOMIC regardless — the poll loop
    // in speak() reads it without the lock, because it cannot hold one while
    // speaking. The lock orders it against the QUEUE, not against itself.
    {
        MutexLock lock(m_mutex);
        m_queue.clear();
        // ...and the one already SPEAKING is cut off, which the queue cannot do.
        m_interruptSpeech.store(true, std::memory_order_release);
    }

    const std::string riderWords =
        SpotterPhrase::riderRef(kPreviewRider, /*focusedRaceNum=*/-1);
    const std::string timeWords =
        SpotterPhrase::numberWords(kPreviewComposed) + " point " +
        SpotterPhrase::numberWords(kPreviewTenths);

    // A pack may write its own sample line, with its own recorded segments —
    // `voice_preview` / `preview_mix`, ordinary cue rows. Absent, the assembly below
    // uses ONLY chunks the pack format guarantees (rider.wav, num_*, point.wav),
    // so a voice_preview works for a pack whose author never heard of the key.
    // The voice_preview is a cue like any other, so it sees the same variables —
    // including the live ones. Writing `voice_preview = You're {position}, {gap_to_ahead}
    // to {rider_ahead}.` and hearing it with the real numbers is how you
    // check a template without waiting for the event that fires it.
    SpotterVars::Vars vars;
    vars.eventRider = riderWords;
    vars.eventTime = timeWords;
    fillAmbientVars(vars);

    std::string text;
    const auto ph = m_pack.phrases.find("voice_preview");
    if (ph != m_pack.phrases.end()) {
        text = SpotterCuePack::expand(ph->second, vars);
    } else {
        text = riderWords + ", " + timeWords + ".";
    }
    if (text.empty()) return;   // a pack may mute its own voice_preview

    std::vector<std::string> tokens;
    const auto mx = m_pack.mixes.find("voice_preview");
    if (ttsOnly) {
        // leave empty: fall through to say() below
    } else if (mx != m_pack.mixes.end()) {
        tokens = mx->second;
    } else if (!m_pack.wavs.empty() || !m_pack.mixes.empty()) {
        // A RECORDED pack with no voice_preview row of its own: stitch one from the
        // chunks the format guarantees. Gated on the pack having audio at all,
        // so the bundled text-only pack does not send the worker looking for
        // wav files that were never meant to exist.
        tokens = { "{event_rider}", "{event_time}" };
    }
    const std::vector<std::string> mixFiles = SpotterMix::resolveTokens(
        tokens, kPreviewRider, kPreviewComposed, kPreviewTenths,
        /*penaltySecs=*/-1, /*lapsLeft=*/-1, /*posValue=*/-1);

    // No cue-log entry and no subtitle: this is a settings-menu noise, not
    // something that happened in the race, and it must not land in the feed
    // the subtitle widget replays.
    if (!mixFiles.empty()) {
        SpotterCue cue;
        cue.kind = SpotterCue::Kind::MixSpec;
        cue.payload = text;            // the TTS fallback, if a chunk is missing
        cue.mixDir = m_packDir;
        cue.mixChunks = mixFiles;
        enqueue(std::move(cue));
    } else {
        say(text);   // no pack (TTS mode), or a pack that resolved to nothing
    }
}

void SpotterManager::onTrackPositions(
    int numVehicles, const Unified::TrackPositionData* positions) {
    if ((!m_enabled && !m_subtitles) || !positions || numVehicles <= 0) {
        return;
    }
    const bool wantProx =
        isCategoryEnabled(SpotterPhrase::Category::Proximity);
    const bool wantHaz = isCategoryEnabled(SpotterPhrase::Category::Hazard);

    const PluginData& pd = PluginData::getInstance();

    // The timed-session milestones ride this callback for its CLOCK, not for
    // its positions, and they are TIMING cues. They sat below the early return
    // that guards the proximity and hazard work, so muting the two chattier
    // categories silently took "ten minutes to go" and "halfway there" with
    // them — a Timing switch overruled by an Opponents one.
    // (A lap race's halfway_point rides the leader's crossings in
    // onRaceLapCompleted instead.)
    //
    // NOT gated on isRaceSession(). Qualifying is timed and "five minutes
    // remaining" is the moment it is FOR; practice is timed too. This branch
    // un-gated the sector cue and the lap report for exactly that reason (see
    // onRaceSplit and onRaceLapCompleted) and missed this one, while
    // allCueKeys() has always described it as "ten minutes of the session
    // left" with no race qualifier. The lap-race halfway_point stays race-only
    // in onRaceLapCompleted, correctly: it needs a leader and a distance.
    // ...and only while the session is RUNNING. Position batches keep
    // arriving while riders roll back after an early Race Over, and a clock
    // still counting there would cross "five minutes left" about a session
    // that has already ended.
    const bool sessionRunning =
        (pd.getSessionData().sessionState &
         PluginConstants::SessionState::IN_PROGRESS) != 0;
    if (sessionRunning && isCategoryEnabled(SpotterPhrase::Category::Timing)) {
        const int nowMs = pd.getSessionElapsedTime();
        if (const char* cue = m_milestones.updateTime(
                nowMs, pd.getSessionData().sessionLength)) {
            emitCue(cue, SpotterPhrase::Category::Timing, {}, nowMs);
        }
    }

    if (!wantProx && !wantHaz) return;
    const int focused = pd.getDisplayRaceNum();
    if (focused <= 0) return;

    SpotterHazard::Inputs in;
    // Elapsed, never the raw clock: the detectors' cooldown arithmetic needs
    // a monotonic ascending clock, and the raw clock counts DOWN in timed
    // sessions — with it, every cooldown "never expires" and blue-flag /
    // hazard cues would fire at most once per timed race.
    in.nowMs = pd.getSessionElapsedTime();

    // Proximity ("rider behind"/"clear"): nearest non-excluded rider behind
    // the focused rider along the racing line, from THIS batch — the game
    // sends the ~10 closest riders, which is exactly the set that matters.
    // Suppressed on the pre-start grid and during the launch (a packed grid
    // is not a rival closing on you), same grace the hazard detector uses.
    // Also quiet once YOUR race is over — the same reasoning at the other end.
    // Riders still racing sweep past you on the cool-down lap and the detector
    // read every one of them as a rival on your tail: the demo weekend called
    // "Rider behind", "Rider right" and "Clear" after the checkered flag. The
    // hazard half below is deliberately NOT gated: a rider down ahead is still
    // something to avoid on the lap back to the pits.
    const float trackLen = pd.getSessionData().trackLength;
    const bool raceOver = subjectRaceOver();
    const bool gridQuiet =
        (pd.isRaceSession() &&
         !(pd.getSessionData().sessionState &
           PluginConstants::SessionState::IN_PROGRESS)) ||
        pd.isInGridStartGrace() || raceOver;
    // "The scan ALLOWED", which is not the same as "the scan produced
    // inputs" — see proxScanned below.
    const bool proxAllowed = wantProx && trackLen > 0.0f && !gridQuiet;
    bool proxScanned = false;
    if (proxAllowed) {
        const Unified::TrackPositionData* focusedPos = nullptr;
        for (int i = 0; i < numVehicles; ++i) {
            if (positions[i].raceNum == focused) {
                focusedPos = &positions[i];
                break;
            }
        }
        if (focusedPos) {
            // The rival's offset ACROSS the track. Headings are compass-style
            // (degrees from north, clockwise — the track builder's convention:
            // forward = (sin, cos) in the X/Z plane, +90deg = the rider's
            // right), so the lateral offset is rel . (cos, -sin): positive =
            // your RIGHT.
            const float yawRad =
                focusedPos->yaw * 3.14159265358979323846f / 180.0f;
            const float cosYaw = std::cos(yawRad);
            const float sinYaw = std::sin(yawRad);
            auto lateralOf = [&](const Unified::TrackPositionData& p) {
                return (p.posX - focusedPos->posX) * cosYaw -
                       (p.posZ - focusedPos->posZ) * sinYaw;
            };

            float nearestBehind = -1.0f;
            float nearestLeft = SpotterHazard::kNoRival;
            float nearestRight = SpotterHazard::kNoRival;
            // Whether a lap difference means anything here. Outside a race
            // everyone is on their own schedule -- an out lap beside a hot lap
            // is not "a lap down" in any sense worth acting on -- so the
            // filter below applies to races only, the same basis
            // fillAmbientVars uses for the lap-count gaps.
            const bool lapsAreReal = pd.isRaceSession();
            const StandingsData* mine = pd.getStanding(focused);
            const int myLaps = mine ? mine->gapLaps : 0;
            for (int i = 0; i < numVehicles; ++i) {
                const Unified::TrackPositionData& p = positions[i];
                if (p.raceNum == focused || p.crashed) continue;
                const StandingsData* st = pd.getStanding(p.raceNum);
                if (st && pd.isRiderExcludedFromDetection(*st)) continue;
                // Is this somebody you are actually racing? A rider a lap up
                // or down shares the track with you and nothing else, and the
                // two cases that DO matter already have their own cues:
                // blue_flag when they are lapping you, lapping_traffic when
                // you are lapping them. "Rider behind" for a backmarker you
                // are about to pass is noise on top of a call you already got.
                //
                // Deliberately NOT applied to the alongside cues below. Those
                // are contact-imminent: a rider a lap down who is level with
                // your front wheel can still take you out, and which lap they
                // are on has nothing to do with it.
                const bool sameLap =
                    !lapsAreReal || !st || st->gapLaps == myLaps;
                // Along the racing line is only HALF of proximity, and on its
                // own it is the half that lies: a rider ten metres back down
                // the centreline can be right across a wide straight from you.
                // Both cues take the same lateral gate — see Config.
                const float lat = lateralOf(p);
                const float latAbs = lat < 0.0f ? -lat : lat;
                if (latAbs > m_hazardCfg.lateralMeters) continue;
                // Positive = p is behind us.
                const float alongM =
                    alongTrackDelta(focusedPos->trackPos, p.trackPos) * trackLen;
                if (sameLap && alongM > 0.0f &&
                    (nearestBehind < 0.0f || alongM < nearestBehind)) {
                    nearestBehind = alongM;
                }
                // The nearest overlapping rival ON EACH SIDE, so being boxed
                // in is expressible. The dead band |lat| < 1m is "directly in
                // line, no side" — a rider you are nose-to-tail with is
                // behind, not beside — and they simply land on neither side
                // rather than, as before, occupying the one nearest slot and
                // silencing a rider genuinely alongside.
                //
                // Kept SIGNED (positive = behind), because the announce
                // window is asymmetric: the detector must be able to tell a
                // rival level with your shoulder from one three metres up the
                // road that you can see perfectly well.
                const float alongAbs = alongM < 0.0f ? -alongM : alongM;
                // As far as the WIDEST window the detector may apply — see
                // alongsideScanReach. Bounding this by the release band alone
                // capped the forward announce window at it.
                if (alongAbs > SpotterHazard::alongsideScanReach(m_hazardCfg)) {
                    continue;
                }
                if (latAbs < 1.0f) continue;
                // NEAREST is not the right test on its own. The scan reaches
                // further than the announce window (it has to, for the hold
                // band), so a rival just outside the window can be closer than
                // one squarely inside it — and taking the closer of the two
                // hands the detector a "nobody here" and masks a real call. A
                // rival IN the window always wins the slot; among equals, the
                // nearest.
                float& side = lat > 0.0f ? nearestRight : nearestLeft;
                const bool candIn =
                    SpotterHazard::alongsideInWindow(m_hazardCfg, alongM);
                const bool heldIn =
                    SpotterHazard::alongsideInWindow(m_hazardCfg, side);
                const float sideAbs = side < 0.0f ? -side : side;
                if (candIn != heldIn ? candIn : alongAbs < sideAbs) {
                    side = alongM;
                }
            }
            in.nearestBehindMeters = nearestBehind;
            in.alongsideLeftMeters = nearestLeft;
            in.alongsideRightMeters = nearestRight;
            // Only NOW is the scan a fact. Permission was not enough: with the
            // focused rider absent from the batch the loop above never ran, the
            // inputs kept their "nobody anywhere" defaults, the detector read
            // that as a release and "Clear." escaped the guard — the same stray
            // clear as before, one level further in. RadarHud guards the same
            // state for the same reason.
            proxScanned = true;
        }
    }

    // Lapping traffic (you closing on a backmarker) is Opponents-facing;
    // same cached detection NoticesHud polls for its LAPPING notice.
    if (wantProx && !raceOver) {
        in.lappingTraffic = pd.isRiderLapping(focused);
    }

    // Hazard/blue-flag edges over PluginData's existing (lazily cached)
    // detection — the same queries NoticesHud polls.
    if (wantHaz) {
        // Blue flags go with the proximity calls: nobody is being held up by a
        // rider who has already finished, so the flag is a leftover.
        in.blueFlagged = !raceOver && pd.isRiderBlueFlagged(focused);
        in.hazardAhead = pd.isHazardAhead();
        if (in.hazardAhead) {
            for (int rn : pd.getHazardRaceNums()) {
                if (pd.getRiderHazardType(rn) == HazardType::WrongWay) {
                    in.hazardIsWrongWay = true;
                    break;
                }
            }
        }
    }

    using Cue = SpotterHazard::Cue;
    const int t = in.nowMs;
    switch (m_detector.update(in, m_hazardCfg)) {
        // The proximity pair re-checks that the scan actually RAN, not just
        // that its category is on. A tick with no scan leaves nearest = -1,
        // which the detector reads as a release — and that synthetic "Clear."
        // must not escape. Two ways in, and only the first was guarded:
        // turning Opponents off while a rival is tracked, and the scan being
        // suppressed on the grid or once your race is over. The second is how
        // a "Clear." still followed the checkered flag after the alongside
        // cooldown fixed the OTHER stray one — same symptom, different path.
        //
        // The detector's own state still advances; it is only the CUE that is
        // withheld, so nothing is left latched for the next session.
        //
        // Hazard cues need no re-check — their inputs are zeroed above when
        // the category is off, so no edge can rise.
        case Cue::RiderLeft:
            if (proxScanned) {
                emitCue("rider_left", SpotterPhrase::Category::Proximity, {}, t);
            }
            break;
        case Cue::RiderRight:
            if (proxScanned) {
                emitCue("rider_right", SpotterPhrase::Category::Proximity, {}, t);
            }
            break;
        case Cue::RidersBothSides:
            if (proxScanned) {
                emitCue("riders_both_sides", SpotterPhrase::Category::Proximity, {}, t);
            }
            break;
        case Cue::RiderBehind:
            if (proxScanned) {
                emitCue("rider_behind", SpotterPhrase::Category::Proximity, {}, t);
            }
            break;
        case Cue::Clear:
            if (proxScanned) {
                emitCue("rider_behind_clear", SpotterPhrase::Category::Proximity, {}, t);
            }
            break;
        // The two lapping cues NAME their subject, which the rest of the
        // detector cues cannot: both sides of that pairing are already in the
        // blue-flag cache, so the number costs a lookup rather than new
        // detection. It goes in {event_rider} (the cue IS about them) and is
        // passed as riderNum too, so a recorded pack's _mix can stitch it from
        // its number clips instead of dropping to speech.
        //
        // -1 is the normal answer, not an error — the flag can be up with the
        // pairing already recomputed away — so riderRef() yields "" and the
        // pack's optional group drops. Deliberately NOT the same as naming the
        // hazard cues' subject: getHazardRaceNums() is not distance-ordered,
        // so with three riders down it would announce an arbitrary one.
        case Cue::BlueFlag: {
            const int lapper = pd.getRiderLappedBy(focused);
            SpotterVars::Vars v;
            v.eventRider = SpotterPhrase::riderRef(lapper, focused);
            emitCue("blue_flag", SpotterPhrase::Category::Hazard, v, t, lapper);
            break;
        }
        case Cue::LappingTraffic:
            if (wantProx) {
                const int backmarker = pd.getRiderLappingTarget(focused);
                SpotterVars::Vars v;
                v.eventRider = SpotterPhrase::riderRef(backmarker, focused);
                emitCue("lapping_traffic", SpotterPhrase::Category::Proximity, v,
                        t, backmarker);
            }
            break;
        case Cue::HazardAhead:
            emitCue("hazard_ahead", SpotterPhrase::Category::Hazard, {}, t);
            break;
        case Cue::WrongWayAhead:
            emitCue("wrong_way_ahead", SpotterPhrase::Category::Hazard, {}, t);
            break;
        case Cue::None:
            break;
    }
}

// The eight hazard setters below all clamp, and three of them clamp to the
// same cooldown range. std::clamp says what the hand-rolled nested ternary
// said, in the vocabulary the rest of the plugin already uses for this.
namespace {
constexpr int kCooldownMinMs = 5000, kCooldownMaxMs = 300000;
}

void SpotterManager::setBehindOnMeters(float m) {
    // The SAME range the stepper enforces, which is what spotter_manager.h
    // promises: a hand-edited behind_on_m=90 used to load as 90 and then snap
    // to 60 on the first click, so the INI and the UI disagreed about what a
    // valid value was.
    m_hazardCfg.behindOnMeters = std::clamp(m, 2.0f, 60.0f);
    if (m_hazardCfg.clearMeters < m_hazardCfg.behindOnMeters + 5.0f) {
        m_hazardCfg.clearMeters = m_hazardCfg.behindOnMeters + 5.0f;
    }
}

void SpotterManager::setClearMeters(float m) {
    // The hysteresis band is the contract: clear must sit above behind-on.
    m_hazardCfg.clearMeters =
        std::clamp(m, m_hazardCfg.behindOnMeters + 5.0f, 200.0f);
}

void SpotterManager::setAlongsideOnMeters(float m) {
    m_hazardCfg.alongsideOnMeters = std::clamp(m, 1.0f, 20.0f);
    if (m_hazardCfg.alongsideClearMeters <
        m_hazardCfg.alongsideOnMeters + 2.0f) {
        m_hazardCfg.alongsideClearMeters =
            m_hazardCfg.alongsideOnMeters + 2.0f;
    }
}

void SpotterManager::setAlongsideAheadMeters(float m) {
    // 0 is meaningful and is the point of the knob: call nobody you could see
    // by turning your head. The ceiling is the old symmetric behaviour, for
    // anyone who preferred it.
    m_hazardCfg.alongsideAheadMeters = std::clamp(m, 0.0f, 20.0f);
}

void SpotterManager::setAlongsideClearMeters(float m) {
    // Same hysteresis contract as behind/clear, at alongside scale.
    m_hazardCfg.alongsideClearMeters =
        std::clamp(m, m_hazardCfg.alongsideOnMeters + 2.0f, 40.0f);
}

void SpotterManager::setLateralMeters(float m) {
    // Down to 3m is "same rut only"; 60 is most of a start straight. Wider
    // than that is the along-track-only behaviour this gate exists to end.
    m_hazardCfg.lateralMeters = std::clamp(m, 3.0f, 60.0f);
}

void SpotterManager::setBehindRepeatMs(int ms) {
    m_hazardCfg.behindRepeatMs = std::clamp(ms, 2000, 120000);
}

void SpotterManager::setClearMinEpisodeMs(int ms) {
    // 0 is meaningful: voice every clear, however short the episode.
    m_hazardCfg.clearMinEpisodeMs = std::clamp(ms, 0, 60000);
}

void SpotterManager::setBlueFlagCooldownMs(int ms) {
    m_hazardCfg.blueFlagCooldownMs =
        std::clamp(ms, kCooldownMinMs, kCooldownMaxMs);
}

void SpotterManager::setLappingCooldownMs(int ms) {
    m_hazardCfg.lappingCooldownMs =
        std::clamp(ms, kCooldownMinMs, kCooldownMaxMs);
}

void SpotterManager::setHazardCooldownMs(int ms) {
    m_hazardCfg.hazardCooldownMs =
        std::clamp(ms, kCooldownMinMs, kCooldownMaxMs);
}

void SpotterManager::setPackName(const std::string& name) {
    m_packName = name;
    reloadCuePack();
}

namespace {
// Read one pack folder's ini. Returns false when the folder or file is not
// there, which is not an error for an OVERLAY (a stored name whose folder was
// removed) and is close to fatal for the base (see reloadCuePack).
bool readPackFile(const std::string& name, SpotterCuePack::Pack& out,
                  std::string& dirOut) {
    // Reject names that could escape the pack root — the name reaches us from
    // the INI, which is hand-editable.
    if (name.empty() || name.find('/') != std::string::npos ||
        name.find('\\') != std::string::npos ||
        name.find("..") != std::string::npos) {
        DEBUG_WARN_F("Spotter: invalid pack name '%s'", name.c_str());
        return false;
    }
    const std::string dir = std::string(kPackRoot) + name;
    const std::string iniPath = dir + "\\" + name + ".ini";
    try {
        std::ifstream file(iniPath, std::ios::binary);
        if (!file.is_open()) return false;
        std::ostringstream buffer;
        buffer << file.rdbuf();
        out = SpotterCuePack::parse(buffer.str());
        dirOut = dir;
        return true;
    } catch (...) {
        DEBUG_WARN_F("Spotter: failed to read pack '%s'", name.c_str());
        return false;
    }
}
}  // namespace

// The active vocabulary: the SHIPPED pack's words, with the selected pack's
// laid over them.
//
// There is no third copy in C++. The plugin used to carry a built-in phrase
// for every cue — compose() plus a defaultText argument at each emitCue — and
// a pack line merely overrode it. Two consequences, both reported from the
// seat: commenting a row out of a pack changed nothing, because the hidden
// copy still spoke (it silenced other riders' penalties for exactly zero
// races); and the pack picker needed a "None" entry whose only meaning was
// "use the copy you cannot edit". The registry's `quiet` flag and a whole
// census case existed solely to keep the two in step. Deleting the copy
// deletes all of that: mxbmrp3_data/spotters/default/default.ini is now the
// only place the spotter's words exist, and an absent row is silence.
//
// WHAT LAYERS AND WHAT DOES NOT. Phrases layer, so a recorded pack covering
// twenty cues still speaks the other forty in the shipped wording rather than
// falling mute. Audio does NOT: wavs, mixes and the join gap come from the
// selected pack alone, because a clip is only findable relative to the folder
// it shipped in — the same reason a _2 variant plays only its own audio.
void SpotterManager::reloadCuePack() {
    m_pack = SpotterCuePack::Pack{};
    m_packDir.clear();
    // Reset the worker's copy up front so every failure path below leaves the
    // default join rather than the previous pack's.
    m_mixGapPublished.store(kMixGapMs, std::memory_order_relaxed);

    // An empty name is a settings file from before the split, when "" meant
    // "no pack, use the built-ins". The built-ins are gone, and the shipped
    // pack is what that user was effectively hearing, so read it as such.
    if (m_packName.empty()) m_packName = kBasePackName;

    std::string baseDir;
    SpotterCuePack::Pack base;
    if (!readPackFile(kBasePackName, base, baseDir)) {
        // Nothing else can supply words. Startup re-copies the shipped assets,
        // so this is a deleted or unreadable file rather than a fresh install,
        // and saying so beats a spotter that is silently mute.
        DEBUG_WARN_F("Spotter: shipped pack '%s' missing - the spotter has no "
                     "wording and will stay silent", kBasePackName);
    }

    // OVERLAID MEANS "AND IT LOADED", not merely "a different name is stored".
    // It was the name comparison alone, so a missing folder still took the
    // audio branch below with an EMPTY pack and an empty directory -- the log
    // said "using the shipped wording" while the shipped pack's wavs, mixes and
    // join gap were silently dropped. Inert only because `default` is text
    // only; the first shipped pack with clips would land a user whose folder
    // went missing on TTS, i.e. silence under Wine, which is the exact case the
    // layering exists to prevent.
    bool overlaid = m_packName != kBasePackName;
    SpotterCuePack::Pack sel;
    std::string selDir;
    if (overlaid && !readPackFile(m_packName, sel, selDir)) {
        // Keep the stored name (see the header): the folder may come back, and
        // rewriting the setting would lose the user's choice permanently.
        DEBUG_WARN_F("Spotter: pack '%s' not found - using the shipped wording",
                     m_packName.c_str());
        overlaid = false;
    }

    // Words: shipped first, selected over the top, with a selected cue owning
    // its alternates — see SpotterCuePack::mergePhrases, which is where that
    // rule and its reason live.
    m_pack.phrases = overlaid ? SpotterCuePack::mergePhrases(base, sel)
                              : base.phrases;
    // Audio: the selected pack's own, whichever that is.
    const SpotterCuePack::Pack& audio = overlaid ? sel : base;
    m_pack.wavs = audio.wavs;
    m_pack.mixes = audio.mixes;
    m_packDir = overlaid ? selDir : baseDir;
    if (audio.hasGapMs) {
        m_mixGapPublished.store(audio.gapMs, std::memory_order_relaxed);
    }

    DEBUG_INFO_F("Spotter: pack '%s' (%zu phrases, %zu wavs, join %dms)",
                 m_packName.c_str(), m_pack.phrases.size(), m_pack.wavs.size(),
                 m_mixGapPublished.load(std::memory_order_relaxed));

    // A row the plugin cannot act on is a TYPO nine times out of ten (or a key
    // that moved between versions). parse() is a FORMAT parser and stays
    // tolerant — it keeps the row, which is harmless because nothing ever
    // looks it up — but staying quiet about it is what leaves an author
    // waiting for a line that will never play.
    // NAMED BY THE PACK THE ROW CAME FROM, not by the selected one: these maps
    // are MERGED (shipped words with the selection over them), so attributing
    // every unknown key to m_packName reported a typo in the shipped default.ini
    // against the user's own pack -- and sent them looking in a file they did
    // not write.
    auto reportUnknown = [&](const std::string& key, const char* from) {
        if (SpotterCuePack::isCueKey(SpotterCuePack::stripVariantSuffix(key))) {
            return;
        }
        DEBUG_WARN_F("Spotter: pack '%s' has no cue named '%s' - that line "
                     "will never be spoken", from, key.c_str());
    };
    const char* selName = m_packName.c_str();
    auto whose = [&](const std::string& key) {
        return (overlaid && sel.phrases.count(key)) ? selName : kBasePackName;
    };
    // Audio comes from ONE pack, so its rows need no per-key lookup -- but which
    // pack that is depends on whether the overlay loaded, and `overlaid` is now
    // false when the folder is missing. Naming the selection unconditionally
    // reintroduced the misattribution three lines above, in exactly the case the
    // same commit created: base rows blamed on a pack that is not there.
    const char* audioName = overlaid ? selName : kBasePackName;
    for (const auto& kv : m_pack.phrases) reportUnknown(kv.first, whose(kv.first));
    for (const auto& kv : m_pack.wavs) reportUnknown(kv.first, audioName);
    for (const auto& kv : m_pack.mixes) reportUnknown(kv.first, audioName);

    // A mix recipe naming a placeholder the mixer does not know is dropped by
    // parse(), and dropping it is invisible from the outside: the cue still
    // speaks, through TTS, which on Wine is silence. Say so.
    for (const std::string& row : base.rejectedMixes) {
        DEBUG_WARN_F("Spotter: pack '%s' mix %s names something the mixer "
                     "cannot resolve - that recipe is dropped and the cue "
                     "falls back to speech", kBasePackName, row.c_str());
    }
    for (const std::string& row : sel.rejectedMixes) {
        DEBUG_WARN_F("Spotter: pack '%s' mix %s names something the mixer "
                     "cannot resolve - that recipe is dropped and the cue "
                     "falls back to speech", m_packName.c_str(), row.c_str());
    }
}

std::vector<std::string> SpotterManager::listAvailablePacks() const {
    std::vector<std::string> packs;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((std::string(kPackRoot) + "*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return packs;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (fd.cFileName[0] == '.') continue;
        packs.emplace_back(fd.cFileName);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    std::sort(packs.begin(), packs.end());
    return packs;
}

void SpotterManager::setCategoryEnabled(SpotterPhrase::Category cat, bool on) {
    const uint32_t bit = 1u << static_cast<unsigned>(cat);
    if (on) m_categoryMask |= bit;
    else    m_categoryMask &= ~bit;
}

void SpotterManager::setVolume(int v) {
    m_volume = v;
    publishAudioSettings();
}

void SpotterManager::setSpeed(float sp) {
    m_speed = sp;
    publishAudioSettings();
}

int SpotterManager::sapiRateForSpeed(float speed) {
    // SAPI's rate is an integer -10..10 on a roughly exponential scale where
    // 10 is about 3x. Inverting 3^(rate/10) keeps the TTS path in step with
    // the multiplier the wav paths stretch by, so the setting means one
    // thing whichever backend a cue lands on.
    if (speed <= 0.0f) return 0;
    const double r = 10.0 * std::log(static_cast<double>(speed)) / std::log(3.0);
    const int rounded = static_cast<int>(r < 0 ? r - 0.5 : r + 0.5);
    return rounded < -10 ? -10 : (rounded > 10 ? 10 : rounded);
}

void SpotterManager::publishAudioSettings() {
    if (m_volume < 0) m_volume = 0;
    if (m_volume > 100) m_volume = 100;
    if (m_speed < SpotterStretch::kMinSpeed) m_speed = SpotterStretch::kMinSpeed;
    if (m_speed > SpotterStretch::kMaxSpeed) m_speed = SpotterStretch::kMaxSpeed;
    m_volumePublished.store(m_volume, std::memory_order_relaxed);
    m_speedPublished.store(m_speed, std::memory_order_relaxed);
}

void SpotterManager::setTtsVoice(const std::string& name) {
    m_ttsVoice = name;
    // Resolved HERE, not in publishAudioSettings() and not at the utterance.
    // Not at the utterance because the worker must touch neither the
    // registry nor a game-thread member; not in publishAudioSettings()
    // because that is the volume/speed stepper's per-tick callback — putting
    // a registry enumeration behind it walked the SAPI catalogue ten times a
    // second while somebody held the volume arrow. The voice can only change
    // here, so this is the only place the lookup belongs.
    //
    // What crosses to the worker is the DISPLAY NAME, which the worker matches
    // against a live SAPI enumeration. It used to be a resolved registry path,
    // because an earlier attempt to hand over a name failed — but that attempt
    // was `<voice required="Name=...">` markup, which matches the token's
    // Attributes\Name rather than its display name and fails silently
    // (spotter_tts_voice.h). Matching the display string ourselves has neither
    // problem, and it reaches the voices a path cannot: an engine that
    // produces its tokens through an enumerator has no registry path at all.
    //
    // Checked here anyway, so an uninstalled voice says so ONCE at the click
    // rather than at every utterance. The setting is kept either way, so
    // reinstalling the voice restores the choice.
    std::string voiceName = m_ttsVoice;
    if (!m_ttsVoice.empty()) {
        bool installed = false;
        for (const auto& v : enumerateTtsVoices()) {
            if (v.first == m_ttsVoice) { installed = true; break; }
        }
        if (!installed) {
            DEBUG_WARN_F("Spotter: TTS voice '%s' is not installed - using the "
                         "system default (the setting is kept)",
                         m_ttsVoice.c_str());
            voiceName.clear();
        }
    }
    DEBUG_INFO_F("Spotter: TTS voice set to '%s'",
                 voiceName.empty() ? "system default" : voiceName.c_str());
    MutexLock lock(m_mutex);
    m_ttsVoicePublished = voiceName;
}

std::vector<std::string> SpotterManager::listTtsVoices() const {
    std::vector<std::string> voices;
    for (const auto& v : enumerateTtsVoices()) voices.push_back(v.first);
    return voices;
}

std::deque<SpotterLogEntry> SpotterManager::getCueLog() const {
    MutexLock lock(m_mutex);
    return m_cueLog;
}

std::string SpotterManager::getLatestCueText() const {
    MutexLock lock(m_mutex);
    return m_cueLog.empty() ? std::string() : m_cueLog.back().text;
}

SpotterManager::~SpotterManager() {
    // Backstop for the unload-WITHOUT-Shutdown() path only; the normal path
    // (shutdown() below) has already joined, so joinable() is false here.
    // Never joins — that deadlocks on the loader lock (thread_detach_grace.h).
    if (m_workerThread.joinable()) {
        // Before the stop signal, so the worker cannot observe the stop
        // without also observing that its COM teardown must be skipped
        // (m_running's release store publishes this store with it).
        m_abandonComCleanup.store(true, std::memory_order_release);
        {
            // Under the mutex for the same lost-wakeup reason as shutdown():
            // here a missed notify degrades to spinThenDetach's full 2s spin
            // plus a leaked thread rather than a hang, but the fix is the same
            // two lines.
            MutexLock lock(m_mutex);
            m_running.store(false, std::memory_order_release);
        }
        m_cv.notify_all();
        ThreadTeardown::spinThenDetach(m_workerThread, m_finished);
    }
    // Stop any SND_MEMORY playback BEFORE member destruction frees
    // m_activeMixBuffer — winmm reads that buffer on its own thread, and on
    // this no-Shutdown() path nothing else has purged it. Skipping this is
    // a use-after-free at game exit, the same crash class teardown_test.cpp
    // documents twice.
    PlaySoundA(nullptr, nullptr, 0);
}

void SpotterManager::say(const std::string& utf8Text, bool perishable) {
    if (utf8Text.empty()) return;
    SpotterCue cue;
    cue.kind = SpotterCue::Kind::Speech;
    cue.payload = utf8Text;
    cue.perishable = perishable;
    cue.enqueuedMs = GetTickCount64();
    enqueue(std::move(cue));
}

void SpotterManager::playWav(const std::string& path, bool perishable) {
    if (path.empty()) return;
    SpotterCue cue;
    cue.kind = SpotterCue::Kind::WavFile;
    cue.payload = path;
    cue.perishable = perishable;
    cue.enqueuedMs = GetTickCount64();
    enqueue(std::move(cue));
}

void SpotterManager::enqueue(SpotterCue cue) {
    {
        MutexLock lock(m_mutex);
        // NEVER after shutdown. `!m_running` is both "not started yet" and
        // "already stopped", so a cue arriving after the orchestrated join would
        // start a NEW worker -- the one shape the project's teardown invariants
        // forbid outright (a thread that outlives the DLL deadlocks FreeLibrary
        // on the loader lock), with only the destructor's spinThenDetach behind
        // it. Nothing reaches here today: PluginData::clear() writes
        // m_spectatedRaceNum directly and logs no events. This is the two lines
        // that keep it that way.
        if (m_shutdown.load(std::memory_order_acquire)) return;
        if (!m_running.load(std::memory_order_acquire)) {
            // Reap a worker that exited by exception (finished but unjoined —
            // assigning over a joinable std::thread calls std::terminate()).
            if (m_workerThread.joinable()) {
                try { m_workerThread.join(); } catch (...) {}
            }
            m_finished.store(false, std::memory_order_release);
            m_running.store(true, std::memory_order_release);
            m_workerThread = std::thread(&SpotterManager::workerThread, this);
        }
        if (m_queue.push(std::move(cue))) {
            DEBUG_WARN("Spotter: cue queue full, dropped oldest pending cue");
        }
    }
    m_cv.notify_one();
}

void SpotterManager::shutdown() {
    {
        // The stop flags flip under the queue mutex so the notify cannot land
        // between the worker's predicate check and its wait() — a lost wakeup
        // there leaves join() below waiting forever, i.e. the game hangs on
        // exit. Same shape as PluginThread::stop() and the analytics worker.
        MutexLock lock(m_mutex);
        m_shutdown.store(true, std::memory_order_release);   // enqueue() refuses from here on
        m_running.store(false, std::memory_order_release);
    }
    m_cv.notify_all();
    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }
    {
        MutexLock lock(m_mutex);
        m_queue.clear();
    }
    // Stop any SND_ASYNC wav or SND_MEMORY mix still sounding — winmm plays
    // those on its own thread, so joining our worker does not silence them.
    // The purge must precede the buffer release below.
    PlaySoundA(nullptr, nullptr, 0);
    m_activeMixBuffer.clear();
}

void SpotterManager::workerThread() {
    // Top-level guard: an uncaught throw in a std::thread body calls
    // std::terminate() and takes the game down.
    try {
        // APARTMENT-threaded, deliberately, and it is load-bearing: a TTS
        // engine registered ThreadingModel=Apartment (eSpeak's SAPI wrapper is
        // one) is homed in its creator's apartment. From the MTA this used to
        // be, that home was NOT this thread -- COM put the engine in the
        // process's main STA, the GAME's thread, and every dispatch into it
        // executed there, outside every SEH guard in this file. That is the
        // third espeak-ng crash: same faulting instruction as the first two,
        // reached through combase onto the game thread. As an STA the engine
        // lives on THIS thread instead, and its work arrives only inside the
        // guarded pump/wait calls in speak(). See the guard block above.
        const HRESULT coHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        const bool comReady = SUCCEEDED(coHr);
        if (!comReady) {
            DEBUG_WARN_F("Spotter: CoInitializeEx failed (0x%08lX) - TTS disabled",
                         static_cast<unsigned long>(coHr));
        }

        // The voice is created lazily on the first speech cue and lives only
        // on this thread — no marshaling, no cross-thread COM rules to obey.
        ISpVoice* pVoice = nullptr;
        bool voiceFailedLogged = false;
        // The voice token currently applied to pVoice, so SetVoice runs on a
        // CHANGE rather than once per utterance. "" means the system default,
        // which is also pVoice's state when it is first created.
        std::string appliedVoiceName;

        // A voice object whose engine never confirmed idle after a purge (see
        // the drain loop in speak()). Once set, the object is LEAKED and never
        // called again -- and CoUninitialize is skipped too, because on an STA
        // it pumps the remaining queue during teardown, which would execute
        // that engine's pending dispatch unguarded. Sticky for the thread's
        // lifetime; one leaked COM init per wedge is the accepted cost.
        bool comWedged = false;

        // RAII rather than trailing calls: the voice and the COM init must be
        // released on the exception exit too, since enqueue() can restart the
        // worker after an exception kills it. EXCEPT on the destructor's
        // detach path: m_abandonComCleanup means the loader lock is held and
        // both calls below can need it, so run neither and let m_finished
        // land while spinThenDetach is still waiting (see the flag's decl).
        struct SapiCleanup {
            ISpVoice*& voice;
            const bool& com;
            const bool& wedged;
            const std::atomic<bool>& abandon;
            ~SapiCleanup() {
                if (abandon.load(std::memory_order_acquire)) return;
                if (voice) { voice->Release(); voice = nullptr; }
                if (com && !wedged) CoUninitialize();
            }
        } sapiCleanup{pVoice, comReady, comWedged, m_abandonComCleanup};

        // Hand a built RIFF to winmm. SND_MEMORY plays from OUR memory, so
        // the previous sound must be stopped BEFORE the buffer it is reading
        // is replaced — one place for that rule, now that two paths use it.
        auto playFromMemory = [&](std::vector<uint8_t> wav) {
            PlaySoundA(nullptr, nullptr, 0);
            m_activeMixBuffer = std::move(wav);
            PlaySoundA(reinterpret_cast<LPCSTR>(m_activeMixBuffer.data()),
                       nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
        };

        // Whole-file read with the same 10MB sanity bound the chunk loader
        // uses: a cue wav is a second or two of PCM, anything larger is a
        // corrupt pack rather than a callout.
        auto readFileBytes = [](const std::string& path) {
            std::vector<uint8_t> bytes;
            std::ifstream f(path, std::ios::binary);
            if (!f.is_open()) return bytes;
            f.seekg(0, std::ios::end);
            const std::streamoff len = f.tellg();
            if (len > 0 && len <= 10 * 1024 * 1024) {
                bytes.resize(static_cast<size_t>(len));
                f.seekg(0);
                f.read(reinterpret_cast<char*>(bytes.data()), len);
            }
            return bytes;
        };

        // Shared by the Speech branch and MixSpec's TTS fallback.
        auto speak = [&](const std::string& utf8) {
            if (comReady && !pVoice && !voiceFailedLogged) {
                const HRESULT hr = CoCreateInstance(
                    kCLSID_SpVoice, nullptr, CLSCTX_ALL, kIID_ISpVoice,
                    reinterpret_cast<void**>(&pVoice));
                if (FAILED(hr) || !pVoice) {
                    pVoice = nullptr;
                    voiceFailedLogged = true;
                    DEBUG_WARN_F("Spotter: SAPI voice unavailable (0x%08lX) - "
                                 "speech cues will be dropped",
                                 static_cast<unsigned long>(hr));
                } else {
                    DEBUG_INFO("Spotter: SAPI voice ready");
                }
            }
            if (!pVoice) return;

            // Voice selection: SetVoice with the token whose registry key the
            // game thread resolved. Applied only when it CHANGES — creating a
            // token object per utterance would be COM work on the audio path
            // for a setting that moves when somebody clicks an arrow.
            std::string voiceName;
            {
                MutexLock lock(m_mutex);
                voiceName = m_ttsVoicePublished;
            }
            if (voiceName != appliedVoiceName) {
                // Recorded on SUCCESS, at the end of this block. Assigned here,
                // a transient failure (an engine mid-install, a token that
                // enumerates but will not open) warned once and then pinned the
                // wrong voice for the rest of the process, because every later
                // pass saw "already applied" and skipped the retry.
                //
                // AND THE RETRY IS BOUNDED, which is what the old latch was
                // really buying: findVoiceToken walks both SAPI categories, so
                // retrying per utterance would be a real cost against a voice
                // that can never load. It cannot be -- setTtsVoice() checks the
                // name against a live enumeration at the click and publishes an
                // EMPTY string when the voice is not installed, so a name that
                // reaches here exists. Only a transient can fail, and a
                // transient is the case worth retrying.
                bool applied = false;
                if (voiceName.empty()) {
                    // Back to the system default. SAPI has no "unset", so the
                    // voice object is rebuilt — cheap, and it happens only on
                    // an actual change.
                    ISpVoice* fresh = nullptr;
                    if (SUCCEEDED(CoCreateInstance(kCLSID_SpVoice, nullptr,
                                                   CLSCTX_ALL, kIID_ISpVoice,
                                                   reinterpret_cast<void**>(&fresh)))) {
                        // Release cascades into the old engine's teardown --
                        // guarded, like every other dispatch into engine code.
                        sapiGuardedRelease(pVoice);
                        pVoice = fresh;
                        applied = true;
                    }
                } else if (faultedVoices().count(voiceName)) {
                    // Retired this session (its engine faulted). RECORD the
                    // selection rather than retrying: retirement is
                    // deliberate, so leaving `applied` false would re-enter
                    // this block — and warn — on every utterance for the rest
                    // of the session.
                    applied = true;
                    DEBUG_WARN_F("Spotter: TTS voice '%s' is retired this "
                                 "session (engine fault) - speaking with the "
                                 "current voice",
                                 voiceName.c_str());
                } else {
                    // Matched by NAME against a live enumeration rather than
                    // resolved from a stored registry path. A voice supplied
                    // by a third-party engine's token enumerator has no path
                    // for SetId to open — it exists only for as long as
                    // somebody is asking SAPI — so a stored ID would fail for
                    // exactly the voices this menu exists to reach.
                    HRESULT thr = E_FAIL;
                    bool faulted = false;
                    ISpObjectToken* token = findVoiceToken(voiceName);
                    if (token) {
                        thr = sapiGuardedSetVoice(pVoice, token, &faulted);
                        token->Release();
                    }
                    if (faulted) {
                        // The engine faulted INSIDE SetVoice, so pVoice's own
                        // state is unknown too — drop it and let the next
                        // utterance lazily rebuild a fresh default-voice
                        // engine. The retired branch above then answers this
                        // name for the rest of the session.
                        faultedVoices().insert(voiceName);
                        DEBUG_WARN_F("Spotter: TTS voice '%s' CRASHED its engine on "
                                     "selection - not using it again this session",
                                     voiceName.c_str());
                        sapiGuardedRelease(pVoice);
                        pVoice = nullptr;
                        appliedVoiceName.clear();
                        return;
                    }
                    applied = SUCCEEDED(thr);
                    if (FAILED(thr)) {
                        // Keep talking in whatever voice we have. A wrong
                        // voice you can hear beats a right one you cannot —
                        // which is exactly what the markup path used to do.
                        // The NEXT cue tries again: `applied` stays false, so
                        // the selection is not recorded and this block re-runs.
                        DEBUG_WARN_F("Spotter: could not select TTS voice "
                                     "'%s' (0x%08lX) - speaking with the "
                                     "current voice",
                                     voiceName.c_str(),
                                     static_cast<unsigned long>(thr));
                    }
                }
                if (applied) appliedVoiceName = voiceName;
            }

            // Escaped, not wrapped: SPF_IS_XML is passed either way, so pack
            // phrases are markup regardless of which voice is speaking.
            const std::wstring wide =
                utf8ToWide(SpotterTtsVoice::speakable(utf8));
            if (wide.empty()) return;

            // Apply current settings per utterance (not at creation): an INI
            // reload or settings change lands on the next cue.
            pVoice->SetVolume(static_cast<USHORT>(
                m_volumePublished.load(std::memory_order_relaxed)));
            pVoice->SetRate(sapiRateForSpeed(
                m_speedPublished.load(std::memory_order_relaxed)));

            // Async speak + bounded polling instead of a synchronous call:
            // this is what lets shutdown() interrupt mid-sentence instead of
            // the game waiting out the phrase.
            ULONG stream = 0;
            bool engineFaulted = false;
            const HRESULT hr = sapiGuardedSpeak(pVoice, wide.c_str(),
                                                SPF_ASYNC | SPF_IS_XML, &stream,
                                                &engineFaulted);
            if (engineFaulted) {
                // The engine faulted mid-sentence rather than on selection.
                // Same policy: retire the voice for the session — and actually
                // stop using it. Retiring the NAME alone was not enough: the
                // faulted engine stayed applied to pVoice and, with
                // appliedVoiceName unchanged, every later cue skipped the
                // reselect block and dispatched straight back into it. Drop
                // pVoice so the next utterance rebuilds a fresh default-voice
                // engine and the retired branch above pins the name.
                faultedVoices().insert(voiceName);
                DEBUG_WARN_F("Spotter: TTS voice '%s' CRASHED its engine while "
                             "speaking - not using it again this session",
                             voiceName.c_str());
                sapiGuardedRelease(pVoice);
                pVoice = nullptr;
                appliedVoiceName.clear();
                return;
            }
            if (FAILED(hr)) {
                DEBUG_WARN_F("Spotter: Speak failed (0x%08lX)",
                             static_cast<unsigned long>(hr));
                return;
            }
            for (;;) {
                // Shutting down, or a settings click asking for the sample it
                // just chose rather than the one before it. Same purge either
                // way — the only thing that stops SAPI mid-sentence.
                if (!m_running.load(std::memory_order_acquire) ||
                    m_interruptSpeech.exchange(false,
                                               std::memory_order_acq_rel)) {
                    // Through the SEH guard like every other dispatch into the
                    // engine: the purge lands on the same engine that is
                    // speaking, which is exactly the one that may be faulting.
                    bool purgeFaulted = false;
                    sapiGuardedSpeak(pVoice, nullptr,
                                     SPF_PURGEBEFORESPEAK | SPF_ASYNC, nullptr,
                                     &purgeFaulted);
                    // AND WAIT FOR IT TO ACTUALLY STOP -- CONFIRMED, not
                    // assumed. The purge is ASYNC, so it only QUEUES the stop:
                    // without the drain the loop broke with the engine still
                    // unwinding the interrupted utterance, and the next cue's
                    // SetVoice landed on an engine mid-teardown. That is the
                    // race behind the second espeak-ng crash report (same
                    // faulting instruction as the first, a string scan at
                    // espeak-ng.dll+0x74ed8, dereferencing ~0). Cycling voices
                    // in the settings menu interrupts on EVERY click.
                    //
                    // The second fix drained ONE bounded slice and ignored the
                    // result, so past the bound the worker proceeded anyway --
                    // the same race, just less often. Now the drain repeats in
                    // bounded slices until WaitUntilDone says S_OK (the only
                    // "engine is idle" the API offers), and a voice that never
                    // says it is declared WEDGED below. Still bounded overall:
                    // the reason this loop is async in the first place is so
                    // shutdown does not wait out a sentence, so the shutdown
                    // cap is short. The verdict per slice is drainVerdict(),
                    // pure and pinned by test_spotter_tts_voice.cpp.
                    bool idle = false;
                    if (!purgeFaulted) {
                        const int cap =
                            m_running.load(std::memory_order_acquire)
                                ? kPurgeDrainSlices
                                : kPurgeDrainSlicesShutdown;
                        int slices = 0;
                        for (;;) {
                            bool stepFaulted = false;
                            sapiGuardedPump(&stepFaulted);
                            HRESULT dr = E_FAIL;
                            if (!stepFaulted) {
                                dr = sapiGuardedWait(pVoice, kPurgeDrainMs,
                                                     &stepFaulted);
                            }
                            if (stepFaulted) { purgeFaulted = true; break; }
                            const SpotterTtsVoice::DrainVerdict v =
                                SpotterTtsVoice::drainVerdict(
                                    static_cast<long>(dr), ++slices, cap);
                            if (v == SpotterTtsVoice::DrainVerdict::Idle) {
                                idle = true;
                                break;
                            }
                            if (v == SpotterTtsVoice::DrainVerdict::Wedged) break;
                        }
                    }
                    if (purgeFaulted) {
                        faultedVoices().insert(voiceName);
                        DEBUG_WARN_F("Spotter: TTS voice '%s' CRASHED its "
                                     "engine on purge - not using it again "
                                     "this session",
                                     voiceName.c_str());
                        sapiGuardedRelease(pVoice);
                        pVoice = nullptr;
                        appliedVoiceName.clear();
                    } else if (!idle) {
                        // WEDGED: the engine is (or may still be) speaking, and
                        // every entry point we have -- SetVoice, Release, even
                        // CoUninitialize's teardown pump -- walks its live
                        // state. Calling any of them is the use-after-free
                        // behind these crashes, so the object is abandoned
                        // instead: leaked deliberately, never touched again.
                        // The voice NAME is not retired -- a slow engine is not
                        // a faulty one, and a fresh object may behave.
                        DEBUG_WARN_F("Spotter: TTS engine still speaking after "
                                     "a purge - abandoning the voice object "
                                     "(voice '%s')",
                                     voiceName.c_str());
                        comWedged = true;
                        pVoice = nullptr;   // deliberate leak
                        appliedVoiceName.clear();
                    }
                    break;
                }
                // GUARDED like every other dispatch into the engine. This was
                // the one that was not: WaitUntilDone drives the same third-party
                // code Speak does, and a fault here took the process while the
                // calls either side of it were survivable.
                //
                // The pump comes FIRST: an apartment engine homed on this
                // thread cannot make progress -- and WaitUntilDone can never
                // reach S_OK -- until its queued dispatch is delivered, and
                // the pump is where that engine code actually executes (see
                // sapiGuardedPump). A fault there is the engine's, same as one
                // inside the wait.
                bool waitFaulted = false;
                sapiGuardedPump(&waitFaulted);
                HRESULT wr = E_FAIL;
                if (!waitFaulted) {
                    wr = sapiGuardedWait(pVoice, kSpeakPollMs, &waitFaulted);
                }
                if (waitFaulted) {
                    faultedVoices().insert(voiceName);
                    DEBUG_WARN_F("Spotter: TTS voice '%s' CRASHED its engine while "
                                 "waiting - not using it again this session",
                                 voiceName.c_str());
                    sapiGuardedRelease(pVoice);
                    pVoice = nullptr;
                    appliedVoiceName.clear();
                    return;
                }
                if (wr == S_OK) break;
            }
        };

        for (;;) {
            SpotterCue cue;
            {
                CvLock lk(m_mutex);
                while (m_running.load(std::memory_order_acquire) && m_queue.empty()) {
                    m_cv.wait(lk.native());
                }
                if (!m_running.load(std::memory_order_acquire)) break;
                if (!m_queue.pop(cue, GetTickCount64())) continue;
                // Popping IS the moment nothing is speaking, so an interrupt
                // that nobody consumed dies here rather than cutting off the
                // cue it was asking for. It only ever means "stop what is
                // sounding NOW".
                //
                // Both ways it went wrong without this. From idle, the flag
                // survived into the preview's OWN poll loop, which purged the
                // very sample the click asked to hear — intermittently,
                // because clicking fast enough meant each preview consumed the
                // flag by interrupting the previous one, and it worked. And a
                // preview on a RECORDED pack never reaches speak() at all, so
                // the flag sat set until some unrelated TTS cue minutes later
                // ran into it.
                //
                // INSIDE the lock, beside the pop it belongs to — see
                // previewVoice for the window that opens when the two are
                // apart.
                m_interruptSpeech.store(false, std::memory_order_release);
            }

            if (cue.kind == SpotterCue::Kind::Speech) {
                speak(cue.payload);
            } else if (cue.kind == SpotterCue::Kind::MixSpec) {
                // The recipe carries its own parts (see SpotterCue). All file
                // I/O deliberately lives here on the worker.
                std::vector<uint8_t> wav;
                if (!cue.mixChunks.empty()) {
                    auto loadChunk = [&](const std::string& name)
                        -> SpotterMix::Pcm {
                        const std::vector<uint8_t> bytes =
                            readFileBytes(cue.mixDir + "\\" + name);
                        return SpotterMix::parseWav(bytes.data(), bytes.size());
                    };

                    std::vector<SpotterMix::Pcm> chunks;
                    chunks.reserve(cue.mixChunks.size());
                    bool ok = true;
                    for (size_t i = 0; i < cue.mixChunks.size() && ok; ++i) {
                        SpotterMix::Pcm pcm = loadChunk(cue.mixChunks[i]);
                        if (pcm.valid()) {
                            chunks.push_back(std::move(pcm));
                            continue;
                        }
                        // Lightweight-pack fallback: a missing three-digit
                        // number chunk stitches from the hundreds split
                        // ("one | forty two"). Parts are atoms — no
                        // recursion — and one missing part fails the whole
                        // mix down to TTS, never dropping a word.
                        const std::vector<std::string> parts =
                            SpotterMix::decomposeNumFile(cue.mixChunks[i]);
                        if (parts.empty()) {
                            DEBUG_WARN_F("Spotter: mix chunk unusable: %s",
                                         cue.mixChunks[i].c_str());
                            ok = false;
                            break;
                        }
                        for (const std::string& part : parts) {
                            SpotterMix::Pcm p = loadChunk(part);
                            if (!p.valid()) {
                                DEBUG_WARN_F(
                                    "Spotter: mix chunk unusable: %s (split "
                                    "of %s)", part.c_str(),
                                    cue.mixChunks[i].c_str());
                                ok = false;
                                break;
                            }
                            chunks.push_back(std::move(p));
                        }
                    }
                    if (ok) {
                        // Stretch each chunk, not the assembled clip: the
                        // inter-chunk gaps are pacing we chose, and running
                        // WSOLA across a silence would try to correlate
                        // through it. Speeding up shortens the words and
                        // leaves the joins intact.
                        const float sp =
                            m_speedPublished.load(std::memory_order_relaxed);
                        if (!SpotterStretch::isUnity(sp)) {
                            for (SpotterMix::Pcm& c : chunks) {
                                c.samples = SpotterStretch::apply(
                                    c.samples, sp, c.sampleRate);
                            }
                        }
                        // The join scales with the speech: a pack's gap is in
                        // ITS time, so at 1.5x the words shorten and a fixed
                        // gap would stand out as a stutter between them.
                        const int gap = static_cast<int>(
                            m_mixGapPublished.load(std::memory_order_relaxed) /
                            (sp > 0.0f ? sp : 1.0f));
                        wav = SpotterMix::assemble(chunks, gap);
                    }
                }

                if (!wav.empty()) {
                    SpotterMix::applyGain(
                        wav, m_volumePublished.load(std::memory_order_relaxed));
                    playFromMemory(std::move(wav));
                } else if (!cue.payload.empty()) {
                    speak(cue.payload);  // down the ladder, keep the words
                }
            } else {  // WavFile
                // At full volume the file plays straight from disk — winmm
                // handles more formats than our own reader, so the simple
                // path stays the default one. Below 100 the samples have to
                // be scaled, which means reading and re-emitting the RIFF
                // ourselves (PlaySound has no per-sound volume); a file our
                // reader rejects falls back to playing it unscaled rather
                // than going silent.
                const int vol = m_volumePublished.load(std::memory_order_relaxed);
                const float sp = m_speedPublished.load(std::memory_order_relaxed);
                bool played = false;
                if (vol < 100 || !SpotterStretch::isUnity(sp)) {
                    std::vector<uint8_t> bytes = readFileBytes(cue.payload);
                    SpotterMix::Pcm pcm =
                        SpotterMix::parseWav(bytes.data(), bytes.size());
                    if (pcm.valid()) {
                        pcm.samples = SpotterStretch::apply(pcm.samples, sp,
                                                            pcm.sampleRate);
                        std::vector<SpotterMix::Pcm> one;
                        one.push_back(std::move(pcm));
                        std::vector<uint8_t> wav =
                            SpotterMix::assemble(one, /*gapMs=*/0);
                        if (!wav.empty()) {
                            SpotterMix::applyGain(wav, vol);
                            playFromMemory(std::move(wav));
                            played = true;
                        }
                    }
                }
                // Fire-and-forget: winmm plays on its own thread and a later
                // PlaySound (or shutdown's purge) cancels it. SND_NODEFAULT so
                // a missing file fails silently instead of the system beep.
                if (!played &&
                    !PlaySoundA(cue.payload.c_str(), nullptr,
                                SND_FILENAME | SND_ASYNC | SND_NODEFAULT)) {
                    DEBUG_WARN_F("Spotter: failed to play wav '%s' (missing file?)",
                                 cue.payload.c_str());
                }
            }
        }

        // pVoice release + CoUninitialize: sapiCleanup, on every exit.
    } catch (...) {
        DEBUG_WARN("Spotter: worker thread terminated by exception");
        m_running.store(false, std::memory_order_release);
    }
    m_finished.store(true, std::memory_order_release);
}
