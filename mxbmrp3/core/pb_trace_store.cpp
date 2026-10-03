// ============================================================================
// core/pb_trace_store.cpp
// See pb_trace_store.h. The file is JSON so it can be read by eye next to the
// stats file; each table is one string of 1000 comma-separated ints rather
// than a JSON array, which keeps the file a few lines per key.
// ============================================================================
#include "pb_trace_store.h"

#include "../diagnostics/logger.h"
#include "../vendor/nlohmann/json.hpp"
#ifdef _WIN32
#include "atomic_file_writer.h"
#endif

#include <cstdio>
#include <cstdlib>
#include <fstream>

namespace {
constexpr const char* TRACES_SUBDIRECTORY = "mxbmrp3";   // the stats file's folder
constexpr const char* TRACES_FILENAME = "mxbmrp3_pb_traces.json";
constexpr int FILE_VERSION = 1;
}

PbTraceStore& PbTraceStore::getInstance() {
    static PbTraceStore instance;
    return instance;
}

std::string PbTraceStore::makeKey(const std::string& trackId, const std::string& bikeName) {
    return trackId + "|" + bikeName;   // the stats file's key, so the two read alike
}

std::string PbTraceStore::filePathFor(const std::string& savePath) {
    // Backslashes like the stats file's path; the native unit build (Linux)
    // needs the host's separator or the folder name becomes part of the file's.
#ifdef _WIN32
    constexpr char SEP = '\\';
#else
    constexpr char SEP = '/';
#endif
    std::string path = savePath.empty() ? std::string(".") : savePath;
    if (path.back() != '/' && path.back() != '\\') path += SEP;
    path += TRACES_SUBDIRECTORY;
    path += SEP;
    path += TRACES_FILENAME;
    return path;
}

std::string PbTraceStore::encode(const PbGapTracker::Table& table) {
    std::string out;
    out.reserve(PbGapTracker::NUM_POINTS * 6);
    char buf[16];
    for (int i = 0; i < PbGapTracker::NUM_POINTS; ++i) {
        const PbGapTracker::Point& p = table[i];
        snprintf(buf, sizeof(buf), "%d", p.valid ? p.elapsedMs : -1);
        if (i) out += ',';
        out += buf;
    }
    return out;
}

bool PbTraceStore::decode(const std::string& text, PbGapTracker::Table& table) {
    PbGapTracker::Table parsed{};
    const char* s = text.c_str();
    int count = 0;
    while (*s && count < PbGapTracker::NUM_POINTS) {
        char* end = nullptr;
        const long v = std::strtol(s, &end, 10);
        if (end == s) return false;   // not a number where one was expected
        parsed[count++] = PbGapTracker::Point{ static_cast<int>(v), v >= 0 };
        s = end;
        if (*s == ',') ++s;
        else if (*s) return false;
    }
    if (count != PbGapTracker::NUM_POINTS || *s) return false;   // short, long, or trailing junk
    table = parsed;
    return true;
}

// Every sample of a lap is an elapsed time INSIDE that lap: a value past the
// lap time is corruption, and read as a gap it would be a huge one -- the
// failure direction this whole area guards against -- so the entry is refused.
bool PbTraceStore::samplesFit(const PbGapTracker::Table& table, int lapTimeMs) {
    for (const PbGapTracker::Point& p : table) {
        if (p.valid && p.elapsedMs > lapTimeMs) return false;
    }
    return true;
}

void PbTraceStore::load(const std::string& savePath) {
    m_filePath = filePathFor(savePath);
    m_entries.clear();

    std::ifstream file(m_filePath);
    if (!file.is_open()) {
        DEBUG_INFO_F("[PbTraceStore] No trace file at %s", m_filePath.c_str());
        return;
    }
    try {
        nlohmann::json j;
        file >> j;
        const auto it = j.find("traces");
        if (it == j.end() || !it->is_object()) return;
        for (const auto& [key, entry] : it->items()) {
            // One bad entry fails alone: the types are checked rather than
            // read through value(), whose type_error would reach the outer
            // catch and drop every other trace with it.
            const auto lap = entry.is_object() ? entry.find("lapTimeMs") : entry.end();
            const auto tab = entry.is_object() ? entry.find("table") : entry.end();
            Entry e;
            const bool shaped = entry.is_object() && lap != entry.end() && lap->is_number_integer()
                                && tab != entry.end() && tab->is_string();
            if (!shaped || lap->get<int>() <= 0 || !decode(tab->get<std::string>(), e.table)
                || !samplesFit(e.table, lap->get<int>())) {
                DEBUG_WARN_F("[PbTraceStore] Skipping malformed trace for %s", key.c_str());
                continue;
            }
            e.lapTimeMs = lap->get<int>();
            m_entries[key] = std::move(e);
        }
        DEBUG_INFO_F("[PbTraceStore] Loaded %zu PB traces from %s", m_entries.size(), m_filePath.c_str());
    } catch (const std::exception& e) {
        DEBUG_WARN_F("[PbTraceStore] Failed to read %s: %s", m_filePath.c_str(), e.what());
        m_entries.clear();
    }
}

const PbGapTracker::Table* PbTraceStore::find(const std::string& trackId, const std::string& bikeName,
                                              int* lapTimeMs) const {
    const auto it = m_entries.find(makeKey(trackId, bikeName));
    if (it == m_entries.end()) return nullptr;
    if (lapTimeMs) *lapTimeMs = it->second.lapTimeMs;
    return &it->second.table;
}

bool PbTraceStore::store(const std::string& trackId, const std::string& bikeName,
                         const PbGapTracker::Table& table, int lapTimeMs) {
    if (lapTimeMs <= 0 || trackId.empty() || bikeName.empty()) return false;
    const std::string key = makeKey(trackId, bikeName);
    Entry& e = m_entries[key];
    e.lapTimeMs = lapTimeMs;
    e.table = table;
    // What load() would refuse is not kept (see the header): a sample past the
    // lap time becomes a hole, so the entry fits its own time on the way back in.
    int dropped = 0;
    for (PbGapTracker::Point& p : e.table) {
        if (p.valid && p.elapsedMs > lapTimeMs) {
            p = PbGapTracker::Point{};
            ++dropped;
        }
    }
    if (dropped > 0) {
        DEBUG_INFO_F("[PbTraceStore] %s: %d sample(s) past the %d ms lap dropped", key.c_str(), dropped, lapTimeMs);
    }
    m_dirty = true;
    return true;
}

void PbTraceStore::clear() {
    if (m_entries.empty()) return;
    m_entries.clear();
    m_dirty = true;
    save();
}

void PbTraceStore::save() {
    if (m_filePath.empty()) return;
#ifdef _WIN32
    // Also when the last write failed on the writer thread (m_dirty cleared at hand-over).
    if (!m_dirty && !AtomicFileWriter::needsRetry(m_filePath)) return;
#else
    if (!m_dirty) return;
#endif
    if (writeFile()) m_dirty = false;
}

bool PbTraceStore::writeFile() const {
    try {
        nlohmann::json j;
        j["version"] = FILE_VERSION;
        nlohmann::json traces = nlohmann::json::object();
        for (const auto& [key, e] : m_entries) {
            nlohmann::json t;
            t["lapTimeMs"] = e.lapTimeMs;
            t["table"] = encode(e.table);
            traces[key] = t;
        }
        j["traces"] = traces;
        std::string text = j.dump(1) + "\n";
#ifdef _WIN32
        // The stats file's writer: temp file + replace, so a crash mid-write
        // leaves the old file whole, and off the game thread.
        if (!AtomicFileWriter::submit(m_filePath, std::move(text))) {
            DEBUG_WARN_F("[PbTraceStore] Failed to write %s", m_filePath.c_str());
            return false;
        }
#else
        // The native unit build has no Win32; a plain write is what it can test.
        std::ofstream file(m_filePath);
        if (!file.is_open()) {
            DEBUG_WARN_F("[PbTraceStore] Failed to open %s for writing", m_filePath.c_str());
            return false;
        }
        file << text;
#endif
        DEBUG_INFO_F("[PbTraceStore] Wrote %zu PB traces to %s", m_entries.size(), m_filePath.c_str());
        return true;
    } catch (const std::exception& e) {
        DEBUG_WARN_F("[PbTraceStore] Failed to write %s: %s", m_filePath.c_str(), e.what());
        return false;
    }
}
