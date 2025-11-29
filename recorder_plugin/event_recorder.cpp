// ============================================================================
// recorder_plugin/event_recorder.cpp
// Event recording for standalone recorder plugin
// ============================================================================
#include "event_recorder.h"
#include "performance_timer.h"

// Only include DebugConsole when building as part of mxbmrp3
#ifndef STANDALONE_RECORDER
#include "../diagnostics/logger.h"
#else
// Standalone recorder uses printf instead
#define DEBUG_WARN(msg) printf("[WARN] %s\n", msg)
#define DEBUG_INFO(msg) printf("[INFO] %s\n", msg)
#define DEBUG_WARN_F(fmt, ...) printf("[WARN] " fmt "\n", __VA_ARGS__)
#define DEBUG_INFO_F(fmt, ...) printf("[INFO] " fmt "\n", __VA_ARGS__)
#endif

#include <windows.h>
#include <cstring>
#include <vector>

EventRecorder::EventRecorder()
    : m_file(nullptr), m_recording(false), m_startTimeUs(0), m_eventCount(0), m_performanceFrequency(0) {

    // Initialize performance counter frequency
    m_performanceFrequency = PerformanceTimer::initializeFrequency();
    if (m_performanceFrequency == 1000000LL) {
        DEBUG_WARN("EventRecorder: QueryPerformanceFrequency failed, using 1MHz fallback");
    }
}

EventRecorder::~EventRecorder() {
    if (m_recording) {
        stopRecording();
    }
}

EventRecorder& EventRecorder::getInstance() {
    static EventRecorder instance;
    return instance;
}

uint64_t EventRecorder::getCurrentTimeUs() const {
    return PerformanceTimer::getCurrentTimeMicroseconds(m_performanceFrequency);
}

bool EventRecorder::startRecording(const char* filePath) {
    if (m_recording) {
        DEBUG_WARN("EventRecorder: Already recording, stop first");
        return false;
    }

    // Open file for writing (binary mode)
    errno_t err = fopen_s(&m_file, filePath, "wb");
    if (err != 0 || m_file == nullptr) {
        DEBUG_WARN_F("EventRecorder: Failed to open file: %s", filePath);
        return false;
    }

    m_recording = true;
    m_startTimeUs = getCurrentTimeUs();
    m_eventCount = 0;

    // Write initial header
    writeHeader();

    DEBUG_INFO_F("EventRecorder: Started recording to %s", filePath);
    return true;
}

void EventRecorder::stopRecording() {
    if (!m_recording) {
        return;
    }

    m_recording = false;

    // Update header with final event count and end time
    updateHeader();

    if (m_file) {
        fclose(m_file);
        m_file = nullptr;
    }

    DEBUG_INFO_F("EventRecorder: Stopped recording (%u events)", m_eventCount);
}

void EventRecorder::writeHeader() {
    if (!m_file) return;

    Recording::FileHeader header;
    header.startTimeUs = m_startTimeUs;
    header.numEvents = 0;  // Will be updated on close
    header.endTimeUs = 0;  // Will be updated on close

    // Write header at beginning of file
    fwrite(&header, sizeof(header), 1, m_file);
    fflush(m_file);
}

void EventRecorder::updateHeader() {
    if (!m_file) return;

    // Seek to beginning
    fseek(m_file, 0, SEEK_SET);

    // Read current header
    Recording::FileHeader header;
    fread(&header, sizeof(header), 1, m_file);

    // Update fields
    header.numEvents = m_eventCount;
    header.endTimeUs = getCurrentTimeUs();

    // Write back
    fseek(m_file, 0, SEEK_SET);
    fwrite(&header, sizeof(header), 1, m_file);
    fflush(m_file);
}

void EventRecorder::writeEvent(Recording::EventType type, const void* data, size_t size) {
    if (!m_recording || !m_file) return;

    // Calculate timestamp relative to recording start
    uint64_t timestamp = getCurrentTimeUs() - m_startTimeUs;

    // Write event header
    Recording::EventHeader eventHeader(type, static_cast<uint32_t>(size), timestamp);
    fwrite(&eventHeader, sizeof(eventHeader), 1, m_file);

    // Write event data
    if (data && size > 0) {
        fwrite(data, size, 1, m_file);
    }

    m_eventCount++;

    // Flush periodically (every 100 events)
    if (m_eventCount % 100 == 0) {
        fflush(m_file);
    }
}

void EventRecorder::recordEventInit(const SPluginsBikeEvent_t* data) {
    if (!data) return;
    writeEvent(Recording::EventType::EventInit, data, sizeof(SPluginsBikeEvent_t));
}

void EventRecorder::recordRunInit(const SPluginsBikeSession_t* data) {
    if (!data) return;
    writeEvent(Recording::EventType::RunInit, data, sizeof(SPluginsBikeSession_t));
}

void EventRecorder::recordRunTelemetry(const SPluginsBikeData_t* data, float fTime, float fPos) {
    if (!data) return;

    // Pack telemetry data (bike data + time + pos)
    struct TelemetryData {
        SPluginsBikeData_t bikeData;
        float time;
        float pos;
    } telemetryData;

    memcpy(&telemetryData.bikeData, data, sizeof(SPluginsBikeData_t));
    telemetryData.time = fTime;
    telemetryData.pos = fPos;

    writeEvent(Recording::EventType::RunTelemetry, &telemetryData, sizeof(telemetryData));
}

void EventRecorder::recordRaceEvent(const SPluginsRaceEvent_t* data) {
    if (!data) return;
    writeEvent(Recording::EventType::RaceEvent, data, sizeof(SPluginsRaceEvent_t));
}

void EventRecorder::recordRaceSession(const SPluginsRaceSession_t* data) {
    if (!data) return;
    writeEvent(Recording::EventType::RaceSession, data, sizeof(SPluginsRaceSession_t));
}

void EventRecorder::recordRaceSessionState(const SPluginsRaceSessionState_t* data) {
    if (!data) return;
    writeEvent(Recording::EventType::RaceSessionState, data, sizeof(SPluginsRaceSessionState_t));
}

void EventRecorder::recordRaceAddEntry(const SPluginsRaceAddEntry_t* data) {
    if (!data) return;
    writeEvent(Recording::EventType::RaceAddEntry, data, sizeof(SPluginsRaceAddEntry_t));
}

void EventRecorder::recordRaceRemoveEntry(const SPluginsRaceRemoveEntry_t* data) {
    if (!data) return;
    writeEvent(Recording::EventType::RaceRemoveEntry, data, sizeof(SPluginsRaceRemoveEntry_t));
}

void EventRecorder::recordRaceLap(const SPluginsRaceLap_t* data) {
    if (!data) return;
    writeEvent(Recording::EventType::RaceLap, data, sizeof(SPluginsRaceLap_t));
}

void EventRecorder::recordRaceSplit(const SPluginsRaceSplit_t* data) {
    if (!data) return;
    writeEvent(Recording::EventType::RaceSplit, data, sizeof(SPluginsRaceSplit_t));
}

void EventRecorder::recordRaceHoleshot(const SPluginsRaceHoleshot_t* data) {
    if (!data) return;
    writeEvent(Recording::EventType::RaceHoleshot, data, sizeof(SPluginsRaceHoleshot_t));
}

void EventRecorder::recordRaceClassification(const SPluginsRaceClassification_t* data,
                                            const SPluginsRaceClassificationEntry_t* entries,
                                            int numEntries) {
    if (!data || !entries || numEntries <= 0) return;

    // Pack classification data (header + entries)
    struct ClassificationData {
        SPluginsRaceClassification_t header;
        int numEntries;
        // Entries follow after this
    } classificationData;

    memcpy(&classificationData.header, data, sizeof(SPluginsRaceClassification_t));
    classificationData.numEntries = numEntries;

    // Calculate total size
    size_t totalSize = sizeof(classificationData) +
                       (numEntries * sizeof(SPluginsRaceClassificationEntry_t));

    // Use vector for exception-safe memory management
    std::vector<uint8_t> buffer(totalSize);

    // Copy classification header
    memcpy(buffer.data(), &classificationData, sizeof(classificationData));

    // Copy entries
    memcpy(buffer.data() + sizeof(classificationData), entries,
           numEntries * sizeof(SPluginsRaceClassificationEntry_t));

    // Write event
    writeEvent(Recording::EventType::RaceClassification, buffer.data(), totalSize);
}

void EventRecorder::recordRaceTrackPosition(const SPluginsRaceTrackPosition_t* positions, int numVehicles) {
    if (!positions || numVehicles <= 0) return;

    // Pack track position data (count + positions)
    struct TrackPositionData {
        int numVehicles;
        // Positions follow after this
    } trackPositionData;

    trackPositionData.numVehicles = numVehicles;

    // Calculate total size
    size_t totalSize = sizeof(trackPositionData) +
                       (numVehicles * sizeof(SPluginsRaceTrackPosition_t));

    // Use vector for exception-safe memory management
    std::vector<uint8_t> buffer(totalSize);

    // Copy header
    memcpy(buffer.data(), &trackPositionData, sizeof(trackPositionData));

    // Copy positions
    memcpy(buffer.data() + sizeof(trackPositionData), positions,
           numVehicles * sizeof(SPluginsRaceTrackPosition_t));

    // Write event
    writeEvent(Recording::EventType::RaceTrackPosition, buffer.data(), totalSize);
}

void EventRecorder::recordRaceCommunication(const SPluginsRaceCommunication_t* data, int dataSize) {
    if (!data) return;

    // Record both the structure and the actual data size
    struct CommunicationData {
        SPluginsRaceCommunication_t communication;
        int actualDataSize;
    } communicationData;

    memcpy(&communicationData.communication, data, sizeof(SPluginsRaceCommunication_t));
    communicationData.actualDataSize = dataSize;

    writeEvent(Recording::EventType::RaceCommunication, &communicationData, sizeof(communicationData));
}

void EventRecorder::recordRaceVehicleData(const SPluginsRaceVehicleData_t* data) {
    if (!data) return;
    writeEvent(Recording::EventType::RaceVehicleData, data, sizeof(SPluginsRaceVehicleData_t));
}

void EventRecorder::recordStartup(const char* savePath, int version) {
    // Pack startup data (save path string + version)
    struct StartupData {
        char savePath[256];
        int version;
    } startupData;

    memset(startupData.savePath, 0, sizeof(startupData.savePath));
    if (savePath) {
        strncpy_s(startupData.savePath, sizeof(startupData.savePath), savePath, sizeof(startupData.savePath) - 1);
    }
    startupData.version = version;

    writeEvent(Recording::EventType::Startup, &startupData, sizeof(startupData));
}

void EventRecorder::recordShutdown() {
    // Shutdown event has no data, just timestamp
    writeEvent(Recording::EventType::Shutdown, nullptr, 0);
}

void EventRecorder::recordEventDeinit() {
    // EventDeinit has no data, just timestamp
    writeEvent(Recording::EventType::EventDeinit, nullptr, 0);
}

void EventRecorder::recordRunDeinit() {
    // RunDeinit has no data, just timestamp
    writeEvent(Recording::EventType::RunDeinit, nullptr, 0);
}

void EventRecorder::recordRunStart() {
    // RunStart has no data, just timestamp
    writeEvent(Recording::EventType::RunStart, nullptr, 0);
}

void EventRecorder::recordRunStop() {
    // RunStop has no data, just timestamp
    writeEvent(Recording::EventType::RunStop, nullptr, 0);
}

void EventRecorder::recordRunLap(const SPluginsBikeLap_t* data) {
    if (data) {
        writeEvent(Recording::EventType::RunLap, data, sizeof(SPluginsBikeLap_t));
    }
}

void EventRecorder::recordRunSplit(const SPluginsBikeSplit_t* data) {
    if (data) {
        writeEvent(Recording::EventType::RunSplit, data, sizeof(SPluginsBikeSplit_t));
    }
}

void EventRecorder::recordDrawInit(int numSprites, char** spriteNames, int numFonts, char** fontNames, int result) {
    // Pack DrawInit data - record what the plugin returned
    struct DrawInitData {
        int numSprites;
        int numFonts;
        int result;
        // Sprite/font names would require more complex packing, omit for now
        // The replay tool will provide dummy names
    } drawInitData;

    drawInitData.numSprites = numSprites;
    drawInitData.numFonts = numFonts;
    drawInitData.result = result;

    writeEvent(Recording::EventType::DrawInit, &drawInitData, sizeof(drawInitData));
}

void EventRecorder::recordDraw() {
    // Draw event has no data, just timestamp
    writeEvent(Recording::EventType::Draw, nullptr, 0);
}

void EventRecorder::recordTrackCenterline(int numSegments, void* segments, void* raceData) {
    // Record full track centerline data for 1:1 reproduction
    // Note: raceData size is unknown (API doesn't specify), so we skip it for now
    // mxbmrp3 uses TrackCenterline data for MapHud rendering

    if (numSegments <= 0 || segments == nullptr) {
        // Write just the count if no valid segment data
        struct TrackCenterlineData {
            int numSegments;
        } data;
        data.numSegments = numSegments;
        writeEvent(Recording::EventType::TrackCenterline, &data, sizeof(data));
        return;
    }

    // Calculate total size: numSegments + segment array
    const size_t segmentArraySize = numSegments * sizeof(SPluginsTrackSegment_t);
    const size_t totalSize = sizeof(int) + segmentArraySize;

    // Use vector for exception-safe memory management
    std::vector<uint8_t> buffer(totalSize);

    // Write numSegments first
    *reinterpret_cast<int*>(buffer.data()) = numSegments;

    // Copy segment array
    memcpy(buffer.data() + sizeof(int), segments, segmentArraySize);

    // Write event
    writeEvent(Recording::EventType::TrackCenterline, buffer.data(), totalSize);
}

void EventRecorder::recordRaceDeinit() {
    // RaceDeinit has no data, just timestamp
    writeEvent(Recording::EventType::RaceDeinit, nullptr, 0);
}
