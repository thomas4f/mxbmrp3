// ============================================================================
// core/spotter_manager_audio.cpp
// SpotterManager's audio backends and worker thread: SAPI TTS (voice
// enumeration/selection, the SEH guards around third-party engines), winmm
// wav/mix playback, the cue queue, and thread lifecycle (enqueue/shutdown/
// destructor). Split from spotter_manager.cpp; every method body is unchanged.
// ============================================================================
// file-budget: 1150 the SAPI guards, voice plumbing and worker thread are one safety story
#include "spotter_manager.h"

#include "spotter_mix.h"
#include "spotter_tts_voice.h"
#include "plugin_data.h"
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
                    // Published from inside the lock, immediately before the
                    // wait: the only moment the worker holds nothing in flight.
                    m_workerParked.store(true, std::memory_order_release);
                    m_cv.wait(lk.native());
                }
                m_workerParked.store(false, std::memory_order_release);
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
