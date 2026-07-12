// ============================================================================
// tests/integration/harness/tape.h
// The callback-tape format: a recording of the real PiBoSo callback stream the
// game sends the plugin. Mirrors mxbmrp3/core/event_recorder.h (magic
// "MXBHREC", a FileHeader, then a stream of [EventHeader + raw payload]) so a
// tape captured in-game by the recorder (the in-plugin EventRecorder, enabled via
// [Recorder] enabled=1) can be replayed headlessly into the plugin via
// PluginHost::replayTape().
//
// MAINTENANCE: keep the EventType values, FileHeader and EventHeader layouts,
// and the two compound-payload packings (RaceClassification, RaceTrackPosition)
// byte-identical to mxbmrp3/core/event_recorder.{h,cpp}. Default alignment
// (NOT packed), matching the recorder.
//
// TapeWriter lets a test synthesize a tape (used to round-trip-verify the
// replayer without a game); the real fidelity comes from a recorded tape.
// ============================================================================
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "plugin_api.h"

namespace tape {

enum class EventType : uint32_t {
    None = 0, Startup = 1, Shutdown = 2, EventInit = 3, EventDeinit = 4,
    RunInit = 5, RunDeinit = 6, RunStart = 7, RunStop = 8, RunLap = 9, RunSplit = 10,
    RunTelemetry = 11, DrawInit = 12, Draw = 13, TrackCenterline = 14,
    RaceEvent = 15, RaceDeinit = 16, RaceSession = 17, RaceSessionState = 18,
    RaceAddEntry = 19, RaceRemoveEntry = 20, RaceLap = 21, RaceSplit = 22,
    RaceHoleshot = 23, RaceClassification = 24, RaceTrackPosition = 25,
    RaceCommunication = 26, RaceVehicleData = 27,
};

struct FileHeader {
    char magic[8];         // "MXBHREC\0"
    uint32_t version;      // 1
    uint32_t numEvents;    // total events (informational; the replayer reads to EOF)
    uint64_t startTimeUs;
    uint64_t endTimeUs;
    uint32_t flags;
    char reserved[32];
};

struct EventHeader {
    uint32_t eventType;
    uint32_t dataSize;     // payload bytes that follow
    uint64_t timestampUs;  // since recording start (ignored on replay)
};

// The RaceClassification payload prefix (header struct + entry count), followed
// by `numEntries` SPluginsRaceClassificationEntry_t. Matches the recorder.
struct ClassificationPrefix {
    SPluginsRaceClassification_t header;
    int numEntries;
};
// The RaceTrackPosition payload prefix, followed by `numVehicles` positions.
struct TrackPositionPrefix { int numVehicles; };

// Minimal writer used to synthesize a tape in a test.
class TapeWriter {
public:
    bool open(const std::string& path) {
        m_f = fopen(path.c_str(), "wb");
        if (!m_f) return false;
        FileHeader h{};
        std::memcpy(h.magic, "MXBHREC\0", 8);
        h.version = 1;
        fwrite(&h, sizeof(h), 1, m_f);
        return true;
    }
    void writeSimple(EventType t, const void* data, uint32_t size) {
        EventHeader eh{ (uint32_t)t, size, 0 };
        fwrite(&eh, sizeof(eh), 1, m_f);
        if (data && size) fwrite(data, size, 1, m_f);
        ++m_count;
    }
    void writeClassification(const SPluginsRaceClassification_t& hdr,
                             const std::vector<SPluginsRaceClassificationEntry_t>& entries) {
        ClassificationPrefix pre{ hdr, (int)entries.size() };
        std::vector<uint8_t> buf(sizeof(pre) + entries.size() * sizeof(entries[0]));
        std::memcpy(buf.data(), &pre, sizeof(pre));
        if (!entries.empty())
            std::memcpy(buf.data() + sizeof(pre), entries.data(), entries.size() * sizeof(entries[0]));
        writeSimple(EventType::RaceClassification, buf.data(), (uint32_t)buf.size());
    }
    void writeTrackPosition(const std::vector<SPluginsRaceTrackPosition_t>& positions) {
        TrackPositionPrefix pre{ (int)positions.size() };
        std::vector<uint8_t> buf(sizeof(pre) + positions.size() * sizeof(positions[0]));
        std::memcpy(buf.data(), &pre, sizeof(pre));
        if (!positions.empty())
            std::memcpy(buf.data() + sizeof(pre), positions.data(), positions.size() * sizeof(positions[0]));
        writeSimple(EventType::RaceTrackPosition, buf.data(), (uint32_t)buf.size());
    }
    void close() { if (m_f) { fclose(m_f); m_f = nullptr; } }
    ~TapeWriter() { close(); }

private:
    FILE* m_f = nullptr;
    uint32_t m_count = 0;
};

}  // namespace tape
