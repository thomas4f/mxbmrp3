// ============================================================================
// replay_tool/main.cpp
// Standalone replay tool for mxbmrp3 plugin
// ============================================================================

#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <vector>

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
    RunLap = 9,              // Added (was missing)
    RunSplit = 10,           // Added (was missing)
    RunTelemetry = 11,       // Fixed (was 9)
    DrawInit = 12,           // Fixed (was 10)
    Draw = 13,               // Fixed (was 11)
    TrackCenterline = 14,    // Fixed (was 12)
    RaceEvent = 15,          // Fixed (was 13)
    RaceDeinit = 16,         // Fixed (was 14)
    RaceSession = 17,        // Fixed (was 15)
    RaceSessionState = 18,   // Fixed (was 16)
    RaceAddEntry = 19,       // Fixed (was 17)
    RaceRemoveEntry = 20,    // Fixed (was 18)
    RaceLap = 21,            // Fixed (was 19)
    RaceSplit = 22,          // Added (was missing)
    RaceHoleshot = 23,       // Added (was missing)
    RaceClassification = 24, // Fixed (was 20)
    RaceTrackPosition = 25,  // Fixed (was 21)
    RaceCommunication = 26,  // Fixed (was 22)
    RaceVehicleData = 27,    // Added (was missing)
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

int main(int argc, char* argv[]) {
    printf("=================================================================\n");
    printf("MXBMRP3 Replay Tool - Standalone Performance Testing\n");
    printf("=================================================================\n");

    if (argc < 3) {
        printf("Usage: %s <plugin.dll> <recording.mxbrec> [options]\n\n", argv[0]);
        printf("Options:\n");
        printf("  --speed <N>  Replay speed multiplier (default: 1.0)\n");
        printf("               0 = maximum speed (no waiting)\n");
        printf("               1 = normal speed (real-time)\n");
        printf("               10 = 10x faster\n");
        printf("  --quiet      Suppress plugin debug logs (show only replay_tool output)\n\n");
        printf("Examples:\n");
        printf("  %s mxbmrp3.dll recording.mxbrec\n", argv[0]);
        printf("  %s mxbmrp3.dll recording.mxbrec --speed 10\n", argv[0]);
        printf("  %s mxbmrp3.dll recording.mxbrec --speed 0 --quiet\n\n", argv[0]);
        return 1;
    }

    const char* pluginPath = argv[1];
    const char* recordingPath = argv[2];

    // Parse optional parameters
    float speedMultiplier = 1.0f;  // Default: normal speed
    bool quietMode = false;  // Default: show plugin logs

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

    // Load plugin
    PluginAPI plugin = {};
    if (!plugin.load(pluginPath)) {
        return 1;
    }

    // Initialize plugin
    printf("\nInitializing plugin...\n");

    // Suppress plugin output if quiet mode is enabled
    if (quietMode) {
        suppressPluginOutput();
    }

    char savePath[] = "./";
    int telemetryRate = plugin.Startup(savePath);

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
    }

    // Restore output for replay_tool's own messages
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

    uint64_t replayStartUs = getCurrentTimeUs();
    uint32_t eventsProcessed = 0;
    uint64_t totalPluginTimeUs = 0;

    // Per-event-type statistics (28 event types: 0-27)
    EventStats eventTypeStats[28];

    for (uint32_t i = 0; i < header.numEvents; ++i) {
        // Read event header
        EventHeader eventHeader;
        if (fread(&eventHeader, sizeof(eventHeader), 1, file) != 1) {
            printf("ERROR: Failed to read event %u header\n", i);
            break;
        }

        // Read event data
        std::vector<uint8_t> eventDataVec;
        uint8_t* eventData = nullptr;
        if (eventHeader.dataSize > 0) {
            eventDataVec.resize(eventHeader.dataSize);
            if (fread(eventDataVec.data(), eventHeader.dataSize, 1, file) != 1) {
                printf("ERROR: Failed to read event %u data\n", i);
                break;
            }
            eventData = eventDataVec.data();
        }

        // Wait until it's time to dispatch this event (with speed adjustment)
        if (speedMultiplier > 0.0f) {
            // Calculate target time with speed multiplier
            uint64_t targetTimeUs = (uint64_t)(eventHeader.timestampUs / speedMultiplier);

            while (true) {
                uint64_t currentTimeUs = getCurrentTimeUs();
                uint64_t elapsedUs = currentTimeUs - replayStartUs;
                if (elapsedUs >= targetTimeUs) {
                    break;
                }
                // Sleep briefly to avoid busy-waiting
                Sleep(0);
            }
        }
        // If speedMultiplier == 0 (max speed), skip waiting entirely

        // Dispatch event
        uint64_t eventStartUs = getCurrentTimeUs();

        EventType type = (EventType)eventHeader.eventType;
        switch (type) {
        case EventType::Startup:
            // Startup was already called - skip
            break;

        case EventType::Shutdown:
            // Will call at end - skip
            break;

        case EventType::EventInit:
            if (plugin.EventInit && eventData) {
                plugin.EventInit(eventData, eventHeader.dataSize);
            }
            break;

        case EventType::EventDeinit:
            // EventDeinit has no callback in the API we defined
            break;

        case EventType::RunInit:
            if (plugin.RunInit && eventData) {
                plugin.RunInit(eventData, eventHeader.dataSize);
            }
            break;

        case EventType::RunDeinit:
            // RunDeinit has no callback in the API we defined
            break;

        case EventType::RunStart:
            // RunStart has no callback in the API we defined
            break;

        case EventType::RunStop:
            // RunStop has no callback in the API we defined
            break;

        case EventType::RunLap:
            // RunLap has no callback in the API we defined
            break;

        case EventType::RunSplit:
            // RunSplit has no callback in the API we defined
            break;

        case EventType::RunTelemetry:
            if (plugin.RunTelemetry && eventData && eventHeader.dataSize >= sizeof(void*) + 2 * sizeof(float)) {
                void* bikeData = eventData;
                int bikeDataSize = eventHeader.dataSize - 2 * sizeof(float);
                float* floatData = (float*)(eventData + bikeDataSize);
                plugin.RunTelemetry(bikeData, bikeDataSize, floatData[0], floatData[1]);
            }
            break;

        case EventType::RaceEvent:
            if (plugin.RaceEvent && eventData) {
                plugin.RaceEvent(eventData, eventHeader.dataSize);
            }
            break;

        case EventType::RaceSession:
            if (plugin.RaceSession && eventData) {
                plugin.RaceSession(eventData, eventHeader.dataSize);
            }
            break;

        case EventType::RaceSessionState:
            if (plugin.RaceSessionState && eventData) {
                plugin.RaceSessionState(eventData, eventHeader.dataSize);
            }
            break;

        case EventType::RaceAddEntry:
            if (plugin.RaceAddEntry && eventData) {
                plugin.RaceAddEntry(eventData, eventHeader.dataSize);
            }
            break;

        case EventType::RaceRemoveEntry:
            if (plugin.RaceRemoveEntry && eventData) {
                plugin.RaceRemoveEntry(eventData, eventHeader.dataSize);
            }
            break;

        case EventType::RaceLap:
            if (plugin.RaceLap && eventData) {
                plugin.RaceLap(eventData, eventHeader.dataSize);
            }
            break;

        case EventType::RaceSplit:
            // RaceSplit has no callback in the API we defined
            break;

        case EventType::RaceHoleshot:
            // RaceHoleshot has no callback in the API we defined
            break;

        case EventType::RaceClassification:
            if (plugin.RaceClassification && eventData) {
                // Extract header + numEntries + entries
                // Format: SPluginsRaceClassification_t (16 bytes) + int numEntries (4 bytes) + entries array
                // SPluginsRaceClassification_t has 4 fields: m_iSession, m_iSessionState, m_iSessionTime, m_iNumEntries
                const int headerSize = 16;  // sizeof(SPluginsRaceClassification_t) = 4 * sizeof(int)
                int* numEntries = (int*)(eventData + headerSize);
                void* entries = eventData + headerSize + sizeof(int);
                int entrySize = 0; // Will be calculated
                if (*numEntries > 0) {
                    entrySize = (eventHeader.dataSize - headerSize - sizeof(int)) / (*numEntries);
                }
                plugin.RaceClassification(eventData, headerSize, entries, entrySize);
            }
            break;

        case EventType::RaceTrackPosition:
            if (plugin.RaceTrackPosition && eventData) {
                int* numVehicles = (int*)eventData;
                void* positions = eventData + sizeof(int);
                int entrySize = 0; // Will be calculated
                if (*numVehicles > 0) {
                    entrySize = (eventHeader.dataSize - sizeof(int)) / (*numVehicles);
                }
                plugin.RaceTrackPosition(*numVehicles, positions, entrySize);
            }
            break;

        case EventType::RaceCommunication:
            if (plugin.RaceCommunication && eventData) {
                int* actualDataSize = (int*)(eventData + (eventHeader.dataSize - sizeof(int)));
                plugin.RaceCommunication(eventData, *actualDataSize);
            }
            break;

        case EventType::RaceVehicleData:
            // RaceVehicleData has no callback in the API we defined
            break;

        case EventType::DrawInit:
            // DrawInit was already called - skip
            break;

        case EventType::Draw:
            {
                // Simulate draw call
                int numQuads = 0;
                void* quads = nullptr;
                int numStrings = 0;
                void* strings = nullptr;
                plugin.Draw(0, &numQuads, &quads, &numStrings, &strings);
            }
            break;

        case EventType::TrackCenterline:
            // TrackCenterline - no callback in the API we defined
            break;

        case EventType::RaceDeinit:
            // RaceDeinit has no callback in the API we defined
            break;

        default:
            break;
        }

        uint64_t eventEndUs = getCurrentTimeUs();
        uint64_t eventDurationUs = eventEndUs - eventStartUs;
        totalPluginTimeUs += eventDurationUs;

        // Record per-event-type statistics
        if (eventHeader.eventType < 28) {
            eventTypeStats[eventHeader.eventType].record(eventDurationUs);
        }

        // eventDataVec automatically freed when going out of scope

        eventsProcessed++;

        // Silent during replay - don't interleave progress messages with plugin output
    }

    fclose(file);

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
