// ============================================================================
// recorder_plugin/event_recorder.h
// Event recording for standalone recorder plugin
// ============================================================================
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
// Include full MX Bikes API (structures + function declarations)
// Having declarations here doesn't cause conflicts - recorder_plugin.cpp
// provides the actual implementations of these functions
// Note: Uses shared mxb_api.h from mxbmrp3 via AdditionalIncludeDirectories
#include "vendor/piboso/mxb_api.h"

// Binary file format for recordings
namespace Recording {

    // File header (64 bytes)
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

    // Recording control
    bool startRecording(const char* filePath);
    void stopRecording();
    bool isRecording() const { return m_recording; }

    // Record specific events
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
    uint64_t m_startTimeUs;
    uint32_t m_eventCount;
    long long m_performanceFrequency;
};
