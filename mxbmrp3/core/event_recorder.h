// ============================================================================
// core/event_recorder.h
// In-plugin callback-tape recorder. Writes the raw PiBoSo callback stream the
// game sends us to a .tape file, for headless replay in the integration harness
// (see tests/integration/harness/tape.h and PluginHost::replayTape()).
//
// This replaces the old standalone mxbmrp3_record.dlo plugin: the main plugin
// already receives every callback, so it taps them directly (in mxb_api.cpp) —
// no second process, no console window, no dual-plugin load. Off by default;
// a developer opts in via the hidden [Recorder] enabled=1 INI key (no HUD, no
// hotkey). MX Bikes only (GAME_HAS_RECORDER) — the format is the MX Bikes
// SPlugins* struct layout.
//
// MAINTENANCE: keep the EventType values, FileHeader / EventHeader layouts, and
// the compound-payload prefixes that tape.h mirrors — RaceClassification
// (ClassificationPrefix) and RaceTrackPosition (TrackPositionPrefix) — byte-identical
// to tests/integration/harness/tape.h. Default alignment (NOT packed). The other
// packed payloads (RunTelemetry, RaceCommunication) are recorder-internal: the
// replayer doesn't dispatch them, so tape.h defines no prefix for them, but their
// on-disk layout must still stay self-consistent across record/parse. The committed
// golden-master tapes are coupled to the SPlugins* layout at record time and need
// re-recording after an API-struct change.
// ============================================================================
#pragma once

#include "../game/game_config.h"   // GAME_HAS_RECORDER (+ SPlugins* on MX Bikes)

#if GAME_HAS_RECORDER

#include <cstdint>
#include <cstdio>
#include <cstring>

// Binary file format for recordings
namespace Recording {

    // File header (72 bytes with default alignment: the 68 declared bytes round up
    // to the 8-byte alignment of the uint64 fields). Layout — not the size — is the
    // contract; keep it identical to tape.h's FileHeader.
    struct FileHeader {
        char magic[8];           // "MXBHREC\0"
        uint32_t version;        // Format version (1)
        uint32_t numEvents;      // Total number of events (updated on close)
        uint64_t startTimeUs;    // Recording start time (microseconds)
        uint64_t endTimeUs;      // Recording end time (updated on close)
        uint32_t flags;          // Feature flags (reserved)
        char reserved[32];       // Reserved for future use

        FileHeader() : version(1), numEvents(0), startTimeUs(0), endTimeUs(0), flags(0) {
            memcpy(magic, "MXBHREC\0", 8);
            memset(reserved, 0, sizeof(reserved));
        }
    };

    // Event types that can be recorded
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

    // Event entry header (16 bytes)
    struct EventHeader {
        uint32_t eventType;      // Event type enum
        uint32_t dataSize;       // Size of event data
        uint64_t timestampUs;    // Microseconds since recording start

        EventHeader() : eventType(0), dataSize(0), timestampUs(0) {}
        EventHeader(EventType type, uint32_t size, uint64_t timestamp)
            : eventType(static_cast<uint32_t>(type)), dataSize(size), timestampUs(timestamp) {}
    };
}

class EventRecorder {
public:
    static EventRecorder& getInstance();

    // Config (set from the [Recorder] INI section at load). Defaults off.
    void setRecordingEnabled(bool enabled) { m_configEnabled = enabled; }
    bool isRecordingEnabled() const { return m_configEnabled; }

    // Open a fresh session tape under <savePath>/mxbmrp3/tapes/session_*.tape and
    // record the Startup event. No-op if recording is not enabled or already
    // running. Called once at plugin startup (after settings load).
    void beginSessionRecording(const char* savePath);

    // Recording control (low-level; beginSessionRecording is the usual entry point)
    bool startRecording(const char* filePath);
    void stopRecording();
    bool isRecording() const { return m_recording; }

    // Record specific events (called from the mxb_api.cpp export taps).
    void recordStartup(const char* savePath, int version);
    void recordShutdown();
    void recordEventInit(const SPluginsBikeEvent_t* data);
    void recordEventDeinit();
    void recordRunInit(const SPluginsBikeSession_t* data);
    void recordRunDeinit();
    void recordRunStart();
    void recordRunStop();
    void recordRunLap(const SPluginsBikeLap_t* data);
    void recordRunSplit(const SPluginsBikeSplit_t* data);
    void recordRunTelemetry(const SPluginsBikeData_t* data, float fTime, float fPos);
    void recordDrawInit(int numSprites, char** spriteNames, int numFonts, char** fontNames, int result);
    void recordDraw();
    void recordTrackCenterline(int numSegments, void* segments, void* raceData);
    void recordRaceEvent(const SPluginsRaceEvent_t* data);
    void recordRaceDeinit();
    void recordRaceSession(const SPluginsRaceSession_t* data);
    void recordRaceSessionState(const SPluginsRaceSessionState_t* data);
    void recordRaceAddEntry(const SPluginsRaceAddEntry_t* data);
    void recordRaceRemoveEntry(const SPluginsRaceRemoveEntry_t* data);
    void recordRaceLap(const SPluginsRaceLap_t* data);
    void recordRaceSplit(const SPluginsRaceSplit_t* data);
    void recordRaceHoleshot(const SPluginsRaceHoleshot_t* data);
    void recordRaceClassification(const SPluginsRaceClassification_t* data,
                                  const SPluginsRaceClassificationEntry_t* entries,
                                  int numEntries);
    void recordRaceTrackPosition(const SPluginsRaceTrackPosition_t* positions, int numVehicles);
    void recordRaceCommunication(const SPluginsRaceCommunication_t* data, int dataSize);
    void recordRaceVehicleData(const SPluginsRaceVehicleData_t* data);

private:
    EventRecorder();
    ~EventRecorder();
    EventRecorder(const EventRecorder&) = delete;
    EventRecorder& operator=(const EventRecorder&) = delete;

    void writeHeader();
    void updateHeader();
    void writeEvent(Recording::EventType type, const void* data, size_t size);
    uint64_t getCurrentTimeUs() const;

    FILE* m_file;
    bool m_recording;
    bool m_configEnabled;      // [Recorder] enabled; opt-in, default off
    uint64_t m_startTimeUs;
    uint32_t m_eventCount;
    long long m_performanceFrequency;
};

#endif  // GAME_HAS_RECORDER
