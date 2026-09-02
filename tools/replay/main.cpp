// ============================================================================
// tools/replay/main.cpp
// Standalone replay tool for mxbmrp3 plugin
// ============================================================================

#include <windows.h>
#include <conio.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <vector>
#include <string>

// Forward declarations of plugin API functions
extern "C" {
    typedef int (*PFN_Startup)(char*);
    typedef void (*PFN_Shutdown)();
    typedef void (*PFN_RunInit)(void*, int);
    typedef int (*PFN_DrawInit)(int*, char**, int*, char**);
    typedef void (*PFN_Draw)(int, int*, void**, int*, void**);
    typedef void (*PFN_EventInit)(void*, int);
    typedef void (*PFN_RaceEvent)(void*, int);
    typedef void (*PFN_RaceSession)(void*, int);
    typedef void (*PFN_RaceSessionState)(void*, int);
    typedef void (*PFN_RaceAddEntry)(void*, int);
    typedef void (*PFN_RaceRemoveEntry)(void*, int);
    typedef void (*PFN_RaceLap)(void*, int);
    typedef void (*PFN_RaceClassification)(void*, int, void*, int);
    typedef void (*PFN_RaceTrackPosition)(int, void*, int);
    typedef void (*PFN_RaceCommunication)(void*, int);
    typedef void (*PFN_RunTelemetry)(void*, int, float, float);
}

struct PluginAPI {
    HMODULE hModule;
    PFN_Startup Startup;
    PFN_Shutdown Shutdown;
    PFN_RunInit RunInit;
    PFN_DrawInit DrawInit;
    PFN_Draw Draw;
    PFN_EventInit EventInit;
    PFN_RaceEvent RaceEvent;
    PFN_RaceSession RaceSession;
    PFN_RaceSessionState RaceSessionState;
    PFN_RaceAddEntry RaceAddEntry;
    PFN_RaceRemoveEntry RaceRemoveEntry;
    PFN_RaceLap RaceLap;
    PFN_RaceClassification RaceClassification;
    PFN_RaceTrackPosition RaceTrackPosition;
    PFN_RaceCommunication RaceCommunication;
    PFN_RunTelemetry RunTelemetry;

    bool load(const char* dllPath) {
        hModule = LoadLibraryA(dllPath);
        if (!hModule) {
            printf("ERROR: Failed to load plugin DLL: %s\n", dllPath);
            return false;
        }

        // Load function pointers
        Startup = (PFN_Startup)GetProcAddress(hModule, "Startup");
        Shutdown = (PFN_Shutdown)GetProcAddress(hModule, "Shutdown");
        RunInit = (PFN_RunInit)GetProcAddress(hModule, "RunInit");
        DrawInit = (PFN_DrawInit)GetProcAddress(hModule, "DrawInit");
        Draw = (PFN_Draw)GetProcAddress(hModule, "Draw");
        EventInit = (PFN_EventInit)GetProcAddress(hModule, "EventInit");
        RaceEvent = (PFN_RaceEvent)GetProcAddress(hModule, "RaceEvent");
        RaceSession = (PFN_RaceSession)GetProcAddress(hModule, "RaceSession");
        RaceSessionState = (PFN_RaceSessionState)GetProcAddress(hModule, "RaceSessionState");
        RaceAddEntry = (PFN_RaceAddEntry)GetProcAddress(hModule, "RaceAddEntry");
        RaceRemoveEntry = (PFN_RaceRemoveEntry)GetProcAddress(hModule, "RaceRemoveEntry");
        RaceLap = (PFN_RaceLap)GetProcAddress(hModule, "RaceLap");
        RaceClassification = (PFN_RaceClassification)GetProcAddress(hModule, "RaceClassification");
        RaceTrackPosition = (PFN_RaceTrackPosition)GetProcAddress(hModule, "RaceTrackPosition");
        RaceCommunication = (PFN_RaceCommunication)GetProcAddress(hModule, "RaceCommunication");
        RunTelemetry = (PFN_RunTelemetry)GetProcAddress(hModule, "RunTelemetry");

        // Check required functions
        if (!Startup || !Shutdown || !Draw) {
            printf("ERROR: Missing required plugin functions\n");
            return false;
        }

        printf("Plugin loaded successfully: %s\n", dllPath);
        return true;
    }

    void unload() {
        if (hModule) {
            FreeLibrary(hModule);
            hModule = nullptr;
        }
    }
};

// Performance counter helpers
static LARGE_INTEGER g_performanceFrequency;

void initPerformanceCounter() {
    QueryPerformanceFrequency(&g_performanceFrequency);
}

// Ctrl+C handling. Without a handler, the console default terminates the process
// via ExitProcess, which kills the plugin's background worker threads at arbitrary
// points and then runs its static destructors — a worker killed mid-std::map
// mutation leaves a corrupted tree that the teardown then traverses (an access
// violation inside mxbmrp3.dlo, which also gets reported to analytics as if it were
// a real in-game crash). Instead we cooperatively cancel: the handler just sets a
// flag, the replay loop breaks, and the normal Shutdown()/unload path runs on the
// main thread for a clean teardown.
static std::atomic<bool> g_stopRequested{false};
BOOL WINAPI consoleCtrlHandler(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT) {
        g_stopRequested = true;
        printf("\n[mxbmrp3_replay] Ctrl+C — stopping replay and shutting down cleanly...\n");
        return TRUE;  // handled: do NOT let the default ExitProcess race teardown
    }
    return FALSE;
}

// Output suppression helpers for --quiet mode
static FILE* g_savedStdout = nullptr;
static FILE* g_savedStderr = nullptr;

void suppressPluginOutput() {
    // Redirect stdout and stderr to NUL to suppress plugin output
    g_savedStdout = stdout;
    g_savedStderr = stderr;

    FILE* nullFile;
    freopen_s(&nullFile, "NUL", "w", stdout);
    freopen_s(&nullFile, "NUL", "w", stderr);
}

void restoreOutput() {
    // Restore stdout and stderr
    if (g_savedStdout) {
        freopen_s(&g_savedStdout, "CONOUT$", "w", stdout);
    }
    if (g_savedStderr) {
        freopen_s(&g_savedStderr, "CONOUT$", "w", stderr);
    }
}

uint64_t getCurrentTimeUs() {
    LARGE_INTEGER currentTime;
    QueryPerformanceCounter(&currentTime);
    return (currentTime.QuadPart / g_performanceFrequency.QuadPart) * 1000000LL +
           ((currentTime.QuadPart % g_performanceFrequency.QuadPart) * 1000000LL) / g_performanceFrequency.QuadPart;
}

// Recording file format structures
struct RecordingHeader {
    char magic[8];
    uint32_t version;
    uint32_t numEvents;
    uint64_t startTimeUs;
    uint64_t endTimeUs;
    uint32_t flags;
    char reserved[32];
};

struct EventHeader {
    uint32_t eventType;
    uint32_t dataSize;
    uint64_t timestampUs;
};

enum class EventType : uint32_t {
    None = 0,
    Startup = 1,
    Shutdown = 2,
    EventInit = 3,
    EventDeinit = 4,
    RunInit = 5,
    RunDeinit = 6,
    RunStart = 7,
    RunStop = 8,
    RunLap = 9,
    RunSplit = 10,
    RunTelemetry = 11,
    DrawInit = 12,
    Draw = 13,
    TrackCenterline = 14,
    RaceEvent = 15,
    RaceDeinit = 16,
    RaceSession = 17,
    RaceSessionState = 18,
    RaceAddEntry = 19,
    RaceRemoveEntry = 20,
    RaceLap = 21,
    RaceSplit = 22,
    RaceHoleshot = 23,
    RaceClassification = 24,
    RaceTrackPosition = 25,
    RaceCommunication = 26,
    RaceVehicleData = 27,
};

const char* getEventTypeName(EventType type) {
    switch (type) {
    case EventType::Startup: return "Startup";
    case EventType::Shutdown: return "Shutdown";
    case EventType::EventInit: return "EventInit";
    case EventType::EventDeinit: return "EventDeinit";
    case EventType::RunInit: return "RunInit";
    case EventType::RunDeinit: return "RunDeinit";
    case EventType::RunStart: return "RunStart";
    case EventType::RunStop: return "RunStop";
    case EventType::RunLap: return "RunLap";
    case EventType::RunSplit: return "RunSplit";
    case EventType::RunTelemetry: return "RunTelemetry";
    case EventType::DrawInit: return "DrawInit";
    case EventType::Draw: return "Draw";
    case EventType::TrackCenterline: return "TrackCenterline";
    case EventType::RaceEvent: return "RaceEvent";
    case EventType::RaceDeinit: return "RaceDeinit";
    case EventType::RaceSession: return "RaceSession";
    case EventType::RaceSessionState: return "RaceSessionState";
    case EventType::RaceAddEntry: return "RaceAddEntry";
    case EventType::RaceRemoveEntry: return "RaceRemoveEntry";
    case EventType::RaceLap: return "RaceLap";
    case EventType::RaceSplit: return "RaceSplit";
    case EventType::RaceHoleshot: return "RaceHoleshot";
    case EventType::RaceClassification: return "RaceClassification";
    case EventType::RaceTrackPosition: return "RaceTrackPosition";
    case EventType::RaceCommunication: return "RaceCommunication";
    case EventType::RaceVehicleData: return "RaceVehicleData";
    default: return "Unknown";
    }
}

struct EventStats {
    uint32_t count;
    uint64_t minTimeUs;
    uint64_t maxTimeUs;
    uint64_t totalTimeUs;

    EventStats() : count(0), minTimeUs(UINT64_MAX), maxTimeUs(0), totalTimeUs(0) {}

    void record(uint64_t timeUs) {
        count++;
        totalTimeUs += timeUs;
        if (timeUs < minTimeUs) minTimeUs = timeUs;
        if (timeUs > maxTimeUs) maxTimeUs = timeUs;
    }

    double getAverageMs() const {
        return count > 0 ? (totalTimeUs / (double)count) / 1000.0 : 0.0;
    }

    double getMinMs() const {
        return minTimeUs != UINT64_MAX ? minTimeUs / 1000.0 : 0.0;
    }

    double getMaxMs() const {
        return maxTimeUs / 1000.0;
    }

    double getTotalMs() const {
        return totalTimeUs / 1000.0;
    }
};

// --- interactive replay ------------------------------------------------------
// One tape event held in memory. The whole tape is loaded up front (tapes are
// megabytes): sequential fread can only play forward, while an in-memory
// vector gives the interactive loop seeking in both directions.
struct TapeEvent {
    uint32_t type;
    uint64_t timestampUs;   // relative to tape start
    std::vector<uint8_t> data;
};

// Keys the interactive loop understands (returned by pollKey).
enum class ReplayKey {
    None,
    PauseToggle,   // Space
    SpeedUp,       // '+' / '='
    SpeedDown,     // '-'
    SkipForward,   // Right arrow / '.'
    SkipBack,      // Left arrow / ','
    Restart,       // 'r'
    Quit,          // 'q'
};

// Non-blocking console key read, folding the two-byte extended-key encodings
// (arrows arrive as 0x00 or 0xE0 followed by a scan code). '.'/',' are the
// terminal-safe aliases for environments where arrows don't reach _getch.
static ReplayKey pollKey() {
    if (!_kbhit()) return ReplayKey::None;
    int c = _getch();
    if (c == 0 || c == 0xE0) {
        if (!_kbhit()) return ReplayKey::None;
        switch (_getch()) {
            case 77: return ReplayKey::SkipForward;   // right arrow
            case 75: return ReplayKey::SkipBack;      // left arrow
            default: return ReplayKey::None;
        }
    }
    switch (c) {
        case ' ': return ReplayKey::PauseToggle;
        case '+': case '=': return ReplayKey::SpeedUp;
        case '-': return ReplayKey::SpeedDown;
        case '.': case '>': return ReplayKey::SkipForward;
        case ',': case '<': return ReplayKey::SkipBack;
        case 'r': case 'R': return ReplayKey::Restart;
        case 'q': case 'Q': return ReplayKey::Quit;
        default: return ReplayKey::None;
    }
}

// --- --web helpers -----------------------------------------------------------
// The plugin serves the web overlay from "plugins\mxbmrp3_data\web" and reads its
// settings from "<savePath>\mxbmrp3\", BOTH relative to the process working
// directory. In-game the game root is that directory; for this tool it defaults
// to wherever the exe was launched. --web fixes that: it derives the game root
// from the plugin's own path (a plugin lives in <game>\plugins\) and cd's there,
// so the installed overlay files resolve, then flips webServer=1 on.

// Derive "<gameRoot>" from "<gameRoot>\plugins\mxbmrp3.dlo". Empty if the plugin
// isn't inside a folder literally named "plugins".
static std::string deriveGameRoot(const char* pluginPath) {
    char full[MAX_PATH];
    if (GetFullPathNameA(pluginPath, MAX_PATH, full, nullptr) == 0) return std::string();
    std::string p(full);
    size_t s1 = p.find_last_of("\\/");
    if (s1 == std::string::npos) return std::string();
    std::string dir = p.substr(0, s1);                    // <gameRoot>\plugins
    size_t s2 = dir.find_last_of("\\/");
    if (s2 == std::string::npos) return std::string();
    if (_stricmp(dir.c_str() + s2 + 1, "plugins") != 0) return std::string();
    return dir.substr(0, s2);                              // <gameRoot>
}

// Ensure "[General] webServer=1" in <baseDir>\mxbmrp3\mxbmrp3_settings.ini,
// preserving every other line. Creates the folder/file if missing. Idempotent.
// baseDir is the plugin's save directory (--savepath, or the game root by default).
static bool ensureWebServerEnabled(const std::string& baseDir) {
    std::string dir = baseDir + "\\mxbmrp3";
    CreateDirectoryA(dir.c_str(), nullptr);               // no-op if it exists
    std::string iniPath = dir + "\\mxbmrp3_settings.ini";

    std::string content;
    if (FILE* f = fopen(iniPath.c_str(), "rb")) {
        char buf[4096]; size_t n;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0) content.append(buf, n);
        fclose(f);
    }

    // Rewrite line by line: force any webServer key to 1; track [General] presence.
    std::string out;
    bool wroteKey = false, haveGeneral = false;
    size_t i = 0;
    while (true) {
        size_t e = content.find('\n', i);
        bool last = (e == std::string::npos);
        std::string line = content.substr(i, (last ? content.size() : e) - i);
        size_t a = line.find_first_not_of(" \t\r");
        std::string t = (a == std::string::npos) ? std::string() : line.substr(a);
        if (t.rfind("[General]", 0) == 0) haveGeneral = true;
        size_t eq = t.find('=');
        std::string key = (eq == std::string::npos) ? t : t.substr(0, eq);
        size_t z = key.find_last_not_of(" \t\r");
        key = (z == std::string::npos) ? std::string() : key.substr(0, z + 1);
        if (key == "webServer") { out += "webServer=1"; wroteKey = true; }
        else out += line;
        if (last) break;
        out += '\n';
        i = e + 1;
    }
    if (!wroteKey) {
        if (haveGeneral) {
            size_t pos = out.find("[General]");
            size_t nl = out.find('\n', pos);
            if (nl == std::string::npos) out += "\nwebServer=1";
            else out.insert(nl, "\nwebServer=1");
        } else {
            out = "[General]\nwebServer=1\n" + out;
        }
    }

    FILE* w = fopen(iniPath.c_str(), "wb");
    if (!w) return false;
    if (!out.empty()) fwrite(out.data(), 1, out.size(), w);
    fclose(w);
    return true;
}

// Ensure "[General] displayTarget=COMPANION" in <baseDir>\mxbmrp3\mxbmrp3_settings.ini,
// preserving every other line (mirrors ensureWebServerEnabled). Companion = the HUD
// shows in the standalone window; there's no game view in the replay tool anyway.
// Creates the folder/file/section if missing. Idempotent. baseDir is the save dir.
static bool ensureCompanionEnabled(const std::string& baseDir) {
    std::string dir = baseDir + "\\mxbmrp3";
    CreateDirectoryA(dir.c_str(), nullptr);
    std::string iniPath = dir + "\\mxbmrp3_settings.ini";

    std::string content;
    if (FILE* f = fopen(iniPath.c_str(), "rb")) {
        char buf[4096]; size_t n;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0) content.append(buf, n);
        fclose(f);
    }

    std::string out;
    bool wroteKey = false, haveGeneral = false;
    size_t i = 0;
    while (true) {
        size_t e = content.find('\n', i);
        bool last = (e == std::string::npos);
        std::string line = content.substr(i, (last ? content.size() : e) - i);
        size_t a = line.find_first_not_of(" \t\r");
        std::string t = (a == std::string::npos) ? std::string() : line.substr(a);
        if (t.rfind("[General]", 0) == 0) haveGeneral = true;
        size_t eq = t.find('=');
        std::string key = (eq == std::string::npos) ? t : t.substr(0, eq);
        size_t z = key.find_last_not_of(" \t\r");
        key = (z == std::string::npos) ? std::string() : key.substr(0, z + 1);
        if (key == "displayTarget") { out += "displayTarget=COMPANION"; wroteKey = true; }
        else out += line;
        if (last) break;
        out += '\n';
        i = e + 1;
    }
    if (!wroteKey) {
        if (haveGeneral) {
            size_t pos = out.find("[General]");
            size_t nl = out.find('\n', pos);
            if (nl == std::string::npos) out += "\ndisplayTarget=COMPANION";
            else out.insert(nl, "\ndisplayTarget=COMPANION");
        } else {
            out = "[General]\ndisplayTarget=COMPANION\n" + out;
        }
    }

    FILE* w = fopen(iniPath.c_str(), "wb");
    if (!w) return false;
    if (!out.empty()) fwrite(out.data(), 1, out.size(), w);
    fclose(w);
    return true;
}

// Set up the standalone companion window for a --window run: cd to the game root so
// the plugin's fonts/sprites (plugins\mxbmrp3_data) resolve, and enable the window.
static bool setUpCompanionWindow(const char* pluginPath, const std::string& iniBaseDir) {
    std::string gameRoot = deriveGameRoot(pluginPath);
    std::string dataDir = gameRoot.empty() ? std::string()
                                           : gameRoot + "\\plugins\\mxbmrp3_data";
    DWORD attr = dataDir.empty() ? INVALID_FILE_ATTRIBUTES : GetFileAttributesA(dataDir.c_str());
    if (gameRoot.empty() || attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        printf("WARNING: --window could not locate the plugin data.\n");
        printf("         The plugin must be in <game>\\plugins\\ so that\n");
        printf("         <game>\\plugins\\mxbmrp3_data exists. Window NOT opened.\n\n");
        return false;
    }
    SetCurrentDirectoryA(gameRoot.c_str());   // fonts/sprites resolve relative to the game root
    // ...but the settings live in the save dir. REPORT the write: the window is
    // opened by the plugin reading this key, so a failed write means no window,
    // and claiming "ENABLED" anyway sends the user looking at the wrong thing
    // (the usual cause is an unwritable or malformed --savepath).
    if (!ensureCompanionEnabled(iniBaseDir)) {
        printf("WARNING: --window could not write displayTarget=COMPANION into\n");
        printf("         %s\\mxbmrp3\\mxbmrp3_settings.ini\n", iniBaseDir.c_str());
        printf("         Check --savepath. Window NOT opened.\n\n");
        return false;
    }
    printf("Companion window: ENABLED\n");
    printf("  game root : %s\n\n", gameRoot.c_str());
    return true;
}

// Strip quote characters a Windows command line leaves in a path argument.
// THE TRAP THIS EXISTS FOR: cmd treats a backslash before a closing quote as an
// escape, so the natural-looking
//     --savepath "%USERPROFILE%\Documents\replay-tool\"
// hands us  C:\Users\me\Documents\replay-tool"  — a path with a literal quote
// in it. Every subsequent CreateDirectory/fopen then fails, which surfaces as
// "the companion window doesn't open" rather than as a path problem. Quotes are
// not legal in Windows paths at all, so dropping them is always the right
// reading of the user's intent.
static std::string stripQuotes(std::string p) {
    p.erase(std::remove(p.begin(), p.end(), '"'), p.end());
    return p;
}

// Normalize a --savepath argument to the PiBoSo-style SAVE FOLDER (the directory
// that CONTAINS mxbmrp3\), so the plugin reads <dir>\mxbmrp3\mxbmrp3_settings.ini and
// writes its log/stats/settings there. Be forgiving about what the user points at:
// the save folder itself (…\MX Bikes), its mxbmrp3\ subfolder, or the
// mxbmrp3_settings.ini file — all resolve to the save folder. Returns the input
// unchanged if it matches none of those shapes.
static std::string normalizeSaveDir(std::string p) {
    while (!p.empty() && (p.back() == '\\' || p.back() == '/')) p.pop_back();
    auto leaf = [&](const std::string& s) {
        size_t sl = s.find_last_of("\\/");
        return sl == std::string::npos ? s : s.substr(sl + 1);
    };
    auto parent = [&](const std::string& s) {
        size_t sl = s.find_last_of("\\/");
        return sl == std::string::npos ? s : s.substr(0, sl);
    };
    if (_stricmp(leaf(p).c_str(), "mxbmrp3_settings.ini") == 0) p = parent(p);  // ...\mxbmrp3
    if (_stricmp(leaf(p).c_str(), "mxbmrp3") == 0)              p = parent(p);  // ...\<save>
    return p;
}

// Set up the web overlay for a --web run: cd to the game root so the installed
// overlay resolves, and enable the server. Returns false (with a clear message)
// if the plugin isn't in a game's plugins folder.
static bool setUpWebOverlay(const char* pluginPath, const std::string& iniBaseDir) {
    std::string gameRoot = deriveGameRoot(pluginPath);
    std::string webDir = gameRoot.empty() ? std::string()
                                          : gameRoot + "\\plugins\\mxbmrp3_data\\web";
    DWORD attr = webDir.empty() ? INVALID_FILE_ATTRIBUTES : GetFileAttributesA(webDir.c_str());
    if (gameRoot.empty() || attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        printf("WARNING: --web could not locate the overlay files.\n");
        printf("         The plugin must be in <game>\\plugins\\ so that\n");
        printf("         <game>\\plugins\\mxbmrp3_data\\web exists. Overlay NOT served.\n\n");
        return false;
    }
    SetCurrentDirectoryA(gameRoot.c_str());               // web root resolves relative to the game root
    ensureWebServerEnabled(iniBaseDir);                   // ...but the settings live in the save dir
    printf("Web overlay: ENABLED\n");
    printf("  game root : %s\n", gameRoot.c_str());
    printf("  overlay   : http://localhost:8080  (port/bind: [Advanced] in %s\\mxbmrp3\\mxbmrp3_settings.ini)\n\n",
           gameRoot.c_str());
    return true;
}

// Dispatch one tape event into the plugin — the single dispatch point shared
// by the paced interactive loop and the seek re-feed. `stats`/`totalPluginTimeUs`
// may be null (the re-feed doesn't time itself). Event payloads are copied per
// dispatch so a callback that scribbles on its buffer can't corrupt the tape
// for a later seek's re-feed.
static void dispatchEvent(PluginAPI& plugin, const TapeEvent& ev,
                          EventStats* eventTypeStats,
                          uint64_t* totalPluginTimeUs) {
    std::vector<uint8_t> scratch(ev.data);
    uint8_t* eventData = scratch.empty() ? nullptr : scratch.data();
    const uint32_t dataSize = (uint32_t)scratch.size();

    uint64_t eventStartUs = getCurrentTimeUs();
    EventType type = (EventType)ev.type;
    switch (type) {
        case EventType::Startup:    break;  // done by the session init
        case EventType::Shutdown:   break;  // done at teardown
        case EventType::DrawInit:   break;  // done by the session init

        case EventType::EventInit:
            if (plugin.EventInit && eventData) plugin.EventInit(eventData, dataSize);
            break;
        case EventType::RunInit:
            if (plugin.RunInit && eventData) plugin.RunInit(eventData, dataSize);
            break;
        case EventType::RunTelemetry:
            if (plugin.RunTelemetry && eventData && dataSize >= sizeof(void*) + 2 * sizeof(float)) {
                void* bikeData = eventData;
                int bikeDataSize = dataSize - 2 * sizeof(float);
                float* floatData = (float*)(eventData + bikeDataSize);
                plugin.RunTelemetry(bikeData, bikeDataSize, floatData[0], floatData[1]);
            }
            break;
        case EventType::RaceEvent:
            if (plugin.RaceEvent && eventData) plugin.RaceEvent(eventData, dataSize);
            break;
        case EventType::RaceSession:
            if (plugin.RaceSession && eventData) plugin.RaceSession(eventData, dataSize);
            break;
        case EventType::RaceSessionState:
            if (plugin.RaceSessionState && eventData) plugin.RaceSessionState(eventData, dataSize);
            break;
        case EventType::RaceAddEntry:
            if (plugin.RaceAddEntry && eventData) plugin.RaceAddEntry(eventData, dataSize);
            break;
        case EventType::RaceRemoveEntry:
            if (plugin.RaceRemoveEntry && eventData) plugin.RaceRemoveEntry(eventData, dataSize);
            break;
        case EventType::RaceLap:
            if (plugin.RaceLap && eventData) plugin.RaceLap(eventData, dataSize);
            break;
        case EventType::RaceClassification:
            if (plugin.RaceClassification && eventData) {
                // Format: SPluginsRaceClassification_t (16 bytes) + int numEntries + entries
                const int headerSize = 16;
                int* numEntries = (int*)(eventData + headerSize);
                void* entries = eventData + headerSize + sizeof(int);
                int entrySize = 0;
                if (*numEntries > 0) {
                    entrySize = (dataSize - headerSize - sizeof(int)) / (*numEntries);
                }
                plugin.RaceClassification(eventData, headerSize, entries, entrySize);
            }
            break;
        case EventType::RaceTrackPosition:
            if (plugin.RaceTrackPosition && eventData) {
                int* numVehicles = (int*)eventData;
                void* positions = eventData + sizeof(int);
                int entrySize = 0;
                if (*numVehicles > 0) {
                    entrySize = (dataSize - sizeof(int)) / (*numVehicles);
                }
                plugin.RaceTrackPosition(*numVehicles, positions, entrySize);
            }
            break;
        case EventType::RaceCommunication:
            if (plugin.RaceCommunication && eventData) {
                int* actualDataSize = (int*)(eventData + (dataSize - sizeof(int)));
                plugin.RaceCommunication(eventData, *actualDataSize);
            }
            break;
        case EventType::Draw: {
            int numQuads = 0;
            void* quads = nullptr;
            int numStrings = 0;
            void* strings = nullptr;
            plugin.Draw(0, &numQuads, &quads, &numStrings, &strings);
            break;
        }

        default:
            break;  // event types with no callback in this tool's API surface
    }

    if (totalPluginTimeUs) {
        uint64_t eventDurationUs = getCurrentTimeUs() - eventStartUs;
        *totalPluginTimeUs += eventDurationUs;
        if (eventTypeStats && ev.type < 28) {
            eventTypeStats[ev.type].record(eventDurationUs);
        }
    }
}

int main(int argc, char* argv[]) {
    printf("=================================================================\n");
    printf("MXBMRP3 Replay Tool - Standalone Performance Testing\n");
    printf("=================================================================\n");

    if (argc < 3) {
        printf("Usage: %s <plugin.dll> <recording.tape> [options]\n\n", argv[0]);
        printf("Options:\n");
        printf("  --speed <N>  Replay speed multiplier (default: 1.0)\n");
        printf("               0 = maximum speed (no waiting, non-interactive)\n");
        printf("               1 = normal speed (real-time)\n");
        printf("\n");
        printf("Interactive keys (any --speed > 0):\n");
        printf("  Space        pause / resume\n");
        printf("  + / -        double / halve speed (0.125x..32x)\n");
        printf("  Right or .   skip ahead 10s (events still feed, just unpaced)\n");
        printf("  Left  or ,   skip back 10s (plugin reload + fast re-feed)\n");
        printf("  R            restart from the beginning\n");
        printf("  Q            quit (clean shutdown)\n");
        printf("               10 = 10x faster\n");
        printf("  --quiet      Suppress plugin debug logs (show only mxbmrp3_replay output)\n");
        printf("  --web        Serve the web overlay: cd to the game root (derived from the\n");
        printf("               plugin path) and enable webServer=1, so http://localhost:8080\n");
        printf("               shows the overlay live. Plugin must be in <game>\\plugins\\.\n");
        printf("  --window     Open the standalone companion window: cd to the game root and\n");
        printf("               set displayTarget=COMPANION, so the plugin renders the HUD in its\n");
        printf("               own window as the replay plays. Plugin must be in <game>\\plugins\\.\n");
        printf("  --savepath <dir>  ONE directory for ALL of the plugin's state: the config\n");
        printf("               it reads PLUS the log, stats and settings it writes all live in\n");
        printf("               <dir>\\mxbmrp3\\. Point it at your PiBoSo save folder (\"...\\MX\n");
        printf("               Bikes\", its mxbmrp3\\ folder, or the .ini all work) to replay with\n");
        printf("               your real config -- note the plugin then writes there too. Point\n");
        printf("               it at a scratch folder to stay isolated. Defaults to game root.\n\n");
        printf("Examples:\n");
        printf("  %s mxbmrp3.dll recording.tape\n", argv[0]);
        printf("  %s mxbmrp3.dll recording.tape --speed 10\n", argv[0]);
        printf("  %s \"<game>\\plugins\\mxbmrp3.dlo\" recording.tape --speed 5 --web\n", argv[0]);
        printf("  %s \"<game>\\plugins\\mxbmrp3.dlo\" recording.tape --window --savepath \"C:\\Users\\You\\Documents\\PiBoSo\\MX Bikes\"\n\n", argv[0]);
        return 1;
    }

    // All three path arguments go through stripQuotes — see its header for the
    // trailing-backslash-before-quote trap that makes this necessary.
    const std::string pluginPathStr = stripQuotes(argv[1]);
    const std::string recordingPathStr = stripQuotes(argv[2]);
    const char* pluginPath = pluginPathStr.c_str();
    const char* recordingPath = recordingPathStr.c_str();

    // Parse optional parameters
    float speedMultiplier = 1.0f;  // Default: normal speed
    bool quietMode = false;  // Default: show plugin logs
    bool webMode = false;    // Default: no web overlay
    bool windowMode = false; // Default: no companion window
    std::string savePathArg;               // --savepath: one dir for all plugin state

    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--speed") == 0 && i + 1 < argc) {
            speedMultiplier = (float)atof(argv[i + 1]);
            if (speedMultiplier < 0.0f) {
                printf("ERROR: Speed multiplier must be >= 0\n");
                return 1;
            }
            i++;  // Skip the next argument (the speed value)
        } else if (strcmp(argv[i], "--quiet") == 0) {
            quietMode = true;
        } else if (strcmp(argv[i], "--window") == 0) {
            windowMode = true;
        } else if (strcmp(argv[i], "--web") == 0) {
            webMode = true;
        } else if (strcmp(argv[i], "--savepath") == 0 && i + 1 < argc) {
            savePathArg = stripQuotes(argv[++i]);
        }
    }

    // Print speed info
    if (speedMultiplier == 0.0f) {
        printf("Replay mode: MAXIMUM SPEED (no waiting)\n");
    } else if (speedMultiplier == 1.0f) {
        printf("Replay mode: REAL-TIME (1x speed)\n");
    } else {
        printf("Replay mode: %.1fx SPEED\n", speedMultiplier);
    }
    printf("=================================================================\n\n");

    initPerformanceCounter();

    // Cancel cleanly on Ctrl+C instead of hard-terminating mid-replay (see handler).
    SetConsoleCtrlHandler(consoleCtrlHandler, TRUE);

    // Load plugin
    PluginAPI plugin = {};
    if (!plugin.load(pluginPath)) {
        return 1;
    }

    // Resolve the plugin's save directory: ONE folder that holds all of its state —
    // the config it READS plus the log, stats and settings it WRITES, all under
    // <dir>\mxbmrp3\. --savepath names it explicitly (point it at your PiBoSo save
    // folder to replay with your real config — note the plugin then writes there too,
    // updating your real stats/settings; point it at a scratch folder to stay
    // isolated). Without --savepath we fall back to the legacy "./" (which the asset
    // cd below points at the game root). The game root is a separate concern:
    // fonts/sprites/overlay files load relative to the working directory, so we still
    // cd there, exactly like the game does (cwd = install, savePath = Documents).
    std::string gameRoot = deriveGameRoot(pluginPath);
    std::string saveDir;  // absolute save dir when --savepath is given; empty => "./"
    if (!savePathArg.empty()) {
        char full[MAX_PATH];
        std::string resolved = (GetFullPathNameA(savePathArg.c_str(), MAX_PATH, full, nullptr) > 0)
                                   ? std::string(full) : savePathArg;
        saveDir = normalizeSaveDir(resolved);   // accept the save folder, its mxbmrp3\, or the .ini
        CreateDirectoryA(saveDir.c_str(), nullptr);
        CreateDirectoryA((saveDir + "\\mxbmrp3").c_str(), nullptr);
    }
    // Where --web/--window flip their ini keys: the save dir if given, else the game
    // root (the legacy "./" target).
    const std::string iniBaseDir = saveDir.empty() ? gameRoot : saveDir;

    // Set up the web overlay before Startup (and before any --quiet output
    // suppression, so the URL is always shown): cd to the game root so the
    // installed overlay files resolve, and flip webServer=1 on in the save dir's ini.
    // The plugin's Startup then reads the enabled flag and starts serving.
    if (webMode) {
        setUpWebOverlay(pluginPath, iniBaseDir);
    }

    // Same idea for the standalone companion window: cd to the game root so the
    // plugin's fonts/sprites resolve, and enable it in the save dir's ini. Startup
    // opens the window; the replay's Draw calls then render into it live.
    if (windowMode) {
        setUpCompanionWindow(pluginPath, iniBaseDir);
    }

    // Initialize plugin
    printf("\nInitializing plugin...\n");

    // Suppress plugin output if quiet mode is enabled
    if (quietMode) {
        suppressPluginOutput();
    }

    // Pass the resolved save dir to Startup (mutable buffer — the API takes char*).
    // Empty --savepath keeps the legacy "./" (the working dir, i.e. the game root
    // after the asset cd above).
    std::string startupPath = saveDir.empty() ? std::string("./") : saveDir;
    std::vector<char> savePathBuf(startupPath.c_str(), startupPath.c_str() + startupPath.size() + 1);
    int telemetryRate = plugin.Startup(savePathBuf.data());

    // Initialize drawing (dummy resources)
    int numSprites = 0;
    char* spriteNames = nullptr;
    int numFonts = 0;
    char* fontNames = nullptr;
    plugin.DrawInit(&numSprites, &spriteNames, &numFonts, &fontNames);

    // Only show initialization details in verbose mode (not quiet)
    if (!quietMode) {
        // Map telemetry rate enum to actual frequency
        const char* telemetryRateStr = "Unknown";
        switch (telemetryRate) {
            case 0: telemetryRateStr = "100 Hz"; break;
            case 1: telemetryRateStr = "50 Hz"; break;
            case 2: telemetryRateStr = "20 Hz"; break;
            case 3: telemetryRateStr = "10 Hz"; break;
        }

        printf("Telemetry rate: %s (enum: %d)\n", telemetryRateStr, telemetryRate);
        printf("Draw initialized: %d sprites, %d fonts\n", numSprites, numFonts);

        // Report the ONE directory the plugin uses for config + log + stats + settings,
        // so nothing has to be hunted for. With --savepath it's that explicit dir;
        // otherwise it's the working directory ("./", which the asset cd points at the
        // game root). NOTE: plugin debug lines go to the log FILE below; they only also
        // appear on this console for a Debug DLL (the shipping Release DLL logs to file
        // only).
        std::string shownSaveDir = startupPath;
        if (saveDir.empty()) {
            char cwd[MAX_PATH] = {0};
            if (GetCurrentDirectoryA(sizeof(cwd), cwd) > 0) shownSaveDir = cwd;
        }
        printf("Save path : %s\n", shownSaveDir.c_str());
        printf("Log file  : %s\\mxbmrp3\\mxbmrp3_log.txt\n", shownSaveDir.c_str());
    }

    // Restore output for mxbmrp3_replay's own messages
    if (quietMode) {
        restoreOutput();
    }

    // Load recording
    printf("\nLoading recording: %s\n", recordingPath);
    FILE* file = fopen(recordingPath, "rb");
    if (!file) {
        printf("ERROR: Failed to open recording file\n");
        plugin.Shutdown();
        plugin.unload();
        return 1;
    }

    // Read header
    RecordingHeader header;
    if (fread(&header, sizeof(header), 1, file) != 1) {
        printf("ERROR: Failed to read header\n");
        fclose(file);
        plugin.Shutdown();
        plugin.unload();
        return 1;
    }

    // Verify magic
    if (memcmp(header.magic, "MXBHREC\0", 8) != 0) {
        printf("ERROR: Invalid recording file (bad magic)\n");
        fclose(file);
        plugin.Shutdown();
        plugin.unload();
        return 1;
    }

    printf("Recording info:\n");
    printf("  Version: %u\n", header.version);
    printf("  Events: %u\n", header.numEvents);
    printf("  Duration: %.2f seconds\n", (header.endTimeUs - header.startTimeUs) / 1000000.0);

    // Replay events
    printf("\n=================================================================\n");
    printf("Starting replay... (plugin output below)\n");
    printf("=================================================================\n\n");

    // Suppress plugin output during event replay if quiet mode is enabled
    if (quietMode) {
        suppressPluginOutput();
    }

    // Load the whole tape into memory up front: sequential fread can only play
    // forward, while the interactive loop seeks in both directions. Tapes are
    // megabytes, so this is cheap.
    std::vector<TapeEvent> events;
    events.reserve(header.numEvents);
    for (uint32_t i = 0; i < header.numEvents; ++i) {
        EventHeader eventHeader;
        if (fread(&eventHeader, sizeof(eventHeader), 1, file) != 1) {
            printf("ERROR: Failed to read event %u header\n", i);
            break;
        }
        TapeEvent ev;
        ev.type = eventHeader.eventType;
        ev.timestampUs = eventHeader.timestampUs;
        if (eventHeader.dataSize > 0) {
            ev.data.resize(eventHeader.dataSize);
            if (fread(ev.data.data(), eventHeader.dataSize, 1, file) != 1) {
                printf("ERROR: Failed to read event %u data\n", i);
                break;
            }
        }
        events.push_back(std::move(ev));
    }
    fclose(file);

    // Timeline from the EVENTS, not the header: some tapes carry a bogus
    // header end-time, and the first event can sit well past zero (the
    // recorder arms before the session). The interactive clock starts at the
    // first event rather than waiting out that dead air.
    const uint64_t firstEventUs = events.empty() ? 0 : events.front().timestampUs;
    const uint64_t lastEventUs = events.empty() ? 0 : events.back().timestampUs;
    const bool interactive = speedMultiplier > 0.0f;
    if (interactive) {
        printf("Keys: [Space] pause  [+/-] speed  [Right or .] +10s  "
               "[Left or ,] -10s  [R] restart  [Q] quit\n\n");
    }

    uint64_t replayStartUs = getCurrentTimeUs();
    uint32_t eventsProcessed = 0;
    uint64_t totalPluginTimeUs = 0;

    // Per-event-type statistics (28 event types: 0-27)
    EventStats eventTypeStats[28];

    if (!interactive) {
        // --speed 0: burst mode — no pacing, no keys (batch/profiling use).
        for (size_t i = 0; i < events.size() && !g_stopRequested; ++i) {
            dispatchEvent(plugin, events[i], eventTypeStats, &totalPluginTimeUs);
            eventsProcessed++;
        }
    } else {
        // Paced interactive loop. `tapeUs` is the TAPE CLOCK: it advances by
        // wall time * speed while playing, freezes while paused, and jumps on
        // seeks; events dispatch as the clock passes their timestamps (a
        // forward jump feeds the skipped window back-to-back, so plugin state
        // stays consistent). Seeking BACK cannot rewind callback state, so it
        // reloads the plugin from scratch and re-feeds the tape up to the
        // target at burst speed with output suppressed — sub-second for
        // typical tapes. (--web note: the reload restarts the HTTP server;
        // overlay clients reconnect on their own.)
        double tapeUs = (double)firstEventUs;
        bool paused = false;
        double speed = speedMultiplier;
        size_t idx = 0;
        uint64_t lastWallUs = getCurrentTimeUs();

        auto printStatus = [&](const char* note) {
            printf("[replay %6.1fs / %.1fs  %.2fx %s] %s\n",
                   tapeUs / 1e6, lastEventUs / 1e6, speed,
                   paused ? "PAUSED" : "playing", note);
        };
        // Seek to an absolute tape time: full plugin reload + suppressed
        // burst re-feed of everything up to the target (no stats — the
        // re-feed would double-count). Floor at the first event: earlier is
        // dead air by definition.
        auto seekTo = [&](double targetUs) {
            if (targetUs < (double)firstEventUs) targetUs = (double)firstEventUs;
            printStatus("seeking (plugin reload + re-feed)...");
            if (!quietMode) suppressPluginOutput();
            plugin.Shutdown();
            plugin.unload();
            if (!plugin.load(pluginPath)) {
                if (!quietMode) restoreOutput();
                printf("ERROR: plugin reload failed during seek; stopping\n");
                g_stopRequested = true;
                return;
            }
            plugin.Startup(savePathBuf.data());
            int ns = 0, nf = 0;
            char* sn = nullptr;
            char* fn = nullptr;
            plugin.DrawInit(&ns, &sn, &nf, &fn);
            idx = 0;
            while (idx < events.size() &&
                   events[idx].timestampUs <= (uint64_t)targetUs) {
                dispatchEvent(plugin, events[idx], nullptr, nullptr);
                idx++;
            }
            if (!quietMode) restoreOutput();
            tapeUs = targetUs;
            lastWallUs = getCurrentTimeUs();
            printStatus("seek done");
        };

        printStatus("started");
        while (idx < events.size() && !g_stopRequested) {
            switch (pollKey()) {
                case ReplayKey::PauseToggle:
                    paused = !paused;
                    printStatus(paused ? "paused" : "resumed");
                    break;
                case ReplayKey::SpeedUp:
                    if (speed < 32.0) speed *= 2.0;
                    printStatus("speed up");
                    break;
                case ReplayKey::SpeedDown:
                    if (speed > 0.125) speed /= 2.0;
                    printStatus("speed down");
                    break;
                case ReplayKey::SkipForward:
                    tapeUs += 10e6;
                    printStatus("skip +10s");
                    break;
                case ReplayKey::SkipBack:
                    seekTo(tapeUs - 10e6);
                    break;
                case ReplayKey::Restart:
                    seekTo(0.0);
                    break;
                case ReplayKey::Quit:
                    g_stopRequested = true;
                    break;
                case ReplayKey::None:
                    break;
            }
            if (g_stopRequested) break;

            const uint64_t nowUs = getCurrentTimeUs();
            if (!paused) tapeUs += (double)(nowUs - lastWallUs) * speed;
            lastWallUs = nowUs;

            while (idx < events.size() &&
                   events[idx].timestampUs <= (uint64_t)tapeUs &&
                   !g_stopRequested) {
                dispatchEvent(plugin, events[idx], eventTypeStats,
                              &totalPluginTimeUs);
                eventsProcessed++;
                idx++;
            }

            if (paused && windowMode) {
                // Keep the companion window's input/rendering alive while the
                // tape is frozen (same reason as the post-replay loop below).
                int nq = 0;
                void* q = nullptr;
                int nstr = 0;
                void* s = nullptr;
                plugin.Draw(0, &nq, &q, &nstr, &s);
                Sleep(16);
            } else {
                Sleep(1);
            }
        }
        if (g_stopRequested) {
            printf("[mxbmrp3_replay] stopped at %.1fs (%zu/%zu events)\n",
                   tapeUs / 1e6, idx, events.size());
        }
    }


    uint64_t replayEndUs = getCurrentTimeUs();
    uint64_t totalReplayTimeUs = replayEndUs - replayStartUs;

    // Restore output for final statistics if quiet mode was enabled
    if (quietMode) {
        restoreOutput();
    }

    // Final statistics
    printf("\n\n=================================================================\n");
    printf("=================================================================\n");
    printf("                      REPLAY COMPLETE\n");
    printf("=================================================================\n");
    printf("=================================================================\n");
    printf("Events processed: %u / %u\n", eventsProcessed, header.numEvents);
    printf("Total replay time: %.2f seconds\n", totalReplayTimeUs / 1000000.0);
    printf("Total plugin time: %.2f seconds (%.1f%% of replay time)\n",
           totalPluginTimeUs / 1000000.0,
           (totalPluginTimeUs * 100.0) / totalReplayTimeUs);
    printf("Average plugin time per event: %.2f microseconds\n",
           eventsProcessed > 0 ? (totalPluginTimeUs / (double)eventsProcessed) : 0.0);

    // Per-event-type statistics table
    printf("\n=================================================================\n");
    printf("Performance by Event Type\n");
    printf("=================================================================\n");
    printf("%-22s %8s %10s %10s %10s %10s %7s\n",
           "Event Type", "Count", "Min (ms)", "Max (ms)", "Avg (ms)", "Sum (ms)", "% Time");
    printf("-----------------------------------------------------------------\n");

    // Sort event types by total time (descending) for better readability
    struct EventTypeWithStats {
        EventType type;
        EventStats* stats;
    };
    EventTypeWithStats sortedEvents[28];
    int validEvents = 0;

    for (int i = 0; i < 28; i++) {
        if (eventTypeStats[i].count > 0) {
            sortedEvents[validEvents].type = (EventType)i;
            sortedEvents[validEvents].stats = &eventTypeStats[i];
            validEvents++;
        }
    }

    // Bubble sort by total time (descending)
    for (int i = 0; i < validEvents - 1; i++) {
        for (int j = 0; j < validEvents - i - 1; j++) {
            if (sortedEvents[j].stats->totalTimeUs < sortedEvents[j + 1].stats->totalTimeUs) {
                EventTypeWithStats temp = sortedEvents[j];
                sortedEvents[j] = sortedEvents[j + 1];
                sortedEvents[j + 1] = temp;
            }
        }
    }

    // Print sorted statistics
    for (int i = 0; i < validEvents; i++) {
        EventType type = sortedEvents[i].type;
        EventStats* stats = sortedEvents[i].stats;
        double percentOfTotal = (stats->totalTimeUs * 100.0) / totalPluginTimeUs;

        printf("%-22s %8u %10.3f %10.3f %10.3f %10.3f %6.1f%%\n",
               getEventTypeName(type),
               stats->count,
               stats->getMinMs(),
               stats->getMaxMs(),
               stats->getAverageMs(),
               stats->getTotalMs(),
               percentOfTotal);
    }

    printf("=================================================================\n");
    printf("\n");

    // Keep the plugin live and ticking after the tape ends when there's something to
    // interact with. The companion window (--window) needs continuous Draw calls on
    // this (game) thread to process mouse/keyboard — without them the window renders
    // but can't show the cursor, open the settings menu, or drag/configure HUDs. --web
    // likewise stays served. Exit with Ctrl+C.
    if ((windowMode || webMode) && !g_stopRequested) {
        printf("Replay finished. %s is live — interact with it, then press Ctrl+C to exit.\n\n",
               windowMode ? "Companion window" : "Web overlay");
        if (quietMode) suppressPluginOutput();
        while (!g_stopRequested) {
            int numQuads = 0; void* quads = nullptr;
            int numStrings = 0; void* strings = nullptr;
            plugin.Draw(0, &numQuads, &quads, &numStrings, &strings);
            Sleep(16);  // ~60 Hz: responsive input, negligible CPU
        }
        if (quietMode) restoreOutput();
        printf("\nExiting.\n");
    }

    // Shutdown plugin (suppress output if quiet mode)
    if (quietMode) {
        suppressPluginOutput();
    }

    plugin.Shutdown();
    plugin.unload();

    if (quietMode) {
        restoreOutput();
    }

    printf("Replay tool finished successfully.\n");
    return 0;
}
