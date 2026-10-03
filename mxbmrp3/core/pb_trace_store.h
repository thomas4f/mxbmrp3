// ============================================================================
// core/pb_trace_store.h
// The rider's all-time PB laps as PbGapTracker tables, persisted per track and
// bike -- so the Gap Bar's gap and ghost work from the first flying lap of a
// session instead of only after a new PB is set in it.
//
// WHAT IS STORED. One PbGapTracker::Table (elapsed ms at each of 1000 track
// positions, -1 where the lap had no sample) plus the lap time, keyed like the
// stats file's personal bests: "<trackId>|<bikeName>". The store keeps the
// LAST lap it was handed for a key, and is only handed a lap the stats file has
// just stored as that key's PB, so it tracks the bike-level PB the stats file
// holds -- even a slower one, after the stats were cleared or restored from a
// backup: the PB and its trace match, or no trace is shown. Which bike's table
// the tracker is planted with under the class-scoped PB setting is StatsManager's call (getPersonalBest names the
// bike), not this file's -- the store knows nothing about categories.
//
// A SEPARATE FILE, not a block in mxbmrp3_stats.json: a table is a few KB per
// key, bulk derived data next to a small readable file, and losing it costs
// the ghost, not a record. WRITTEN WHEN THE STATS FILE IS: a PB lap only
// marks the store dirty, and save() runs from inside StatsManager::save() --
// at every stats save point, so the two are never written apart -- through
// the shared AtomicFileWriter, so nothing is written while the player is riding and a
// crash mid-write cannot corrupt the file. Clearing the stats clears this too.
//
// A TRACE CAN LAG THE PB: the stats file also saved mid-ride after a PB, so a
// crash before the next save kept the PB time and lost its trace (a user's
// stats held 67205 ms, the trace 67813 ms), and a stats file restored from a
// backup leaves the trace faster than the PB. PluginData plants a trace as the
// all-time reference only when its lap time IS the PB's, so a mismatched lap is
// never shown as it; the next PB brings its own trace either way.

// NOT VALIDATED AGAINST THE TRACK. A track mod update that moves the start line
// leaves the table describing a lap that can no longer be driven; that is the
// same exposure the stored PB times already have, and it is deliberately not
// addressed here.
//
// Game thread only. File I/O is one read at startup and one write per save()
// with something to write, wrapped in try/catch like the stats file's.
// ============================================================================
#pragma once

#include "pb_gap_tracker.h"

#include <map>
#include <string>

class PbTraceStore {
public:
    static PbTraceStore& getInstance();

    // Read the file under savePath (the plugin's Documents folder). Missing or
    // unreadable is an empty store, never an error the game sees.
    void load(const std::string& savePath);

    // The stored table for a track and bike, or nullptr. lapTimeMs is written
    // when a table is returned.
    const PbGapTracker::Table* find(const std::string& trackId, const std::string& bikeName,
                                    int* lapTimeMs) const;

    // Keep `table` as the key's lap, replacing what is stored: the caller hands
    // over only a lap the stats file has just stored as the key's PB, so this
    // follows the stats even when that PB is slower than the stored lap.
    // Returns false for a missing key or lap time; the file is written by save().
    // A sample past the lap time is dropped from the kept copy: the tracker's
    // last samples are wall-clock after the S2 resync and can overrun the
    // official time by a few ms at the line, and load() refuses a table that
    // does not fit -- an entry kept here that load() then threw away would be
    // erased by the next save and replaced by whatever slower lap came next.
    bool store(const std::string& trackId, const std::string& bikeName,
               const PbGapTracker::Table& table, int lapTimeMs);

    // Write the file if anything changed since it was last written. Called
    // where the stats file is flushed.
    void save();

    // Drop every table and write the (empty) file: the stats were cleared.
    void clear();

    // The file's path for a save path, the way the stats file builds its own.
    static std::string filePathFor(const std::string& savePath);

    size_t size() const { return m_entries.size(); }

    // The serialised form, exposed so the unit test can pin the round-trip
    // without a file: -1 marks a slot the lap had no sample at.
    static std::string encode(const PbGapTracker::Table& table);
    static bool decode(const std::string& text, PbGapTracker::Table& table);
    // Every valid sample lies within the lap time; a table that does not is refused.
    static bool samplesFit(const PbGapTracker::Table& table, int lapTimeMs);

private:
    PbTraceStore() = default;
    ~PbTraceStore() = default;
    PbTraceStore(const PbTraceStore&) = delete;
    PbTraceStore& operator=(const PbTraceStore&) = delete;

    bool writeFile() const;
    static std::string makeKey(const std::string& trackId, const std::string& bikeName);

    struct Entry {
        int lapTimeMs = 0;
        PbGapTracker::Table table{};
    };
    std::map<std::string, Entry> m_entries;
    std::string m_filePath;
    bool m_dirty = false;
};
