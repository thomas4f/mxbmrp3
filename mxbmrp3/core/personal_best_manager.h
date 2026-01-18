// ============================================================================
// core/personal_best_manager.h
// Manages persistent storage of personal best lap times per track/bike combo
// ============================================================================
#pragma once

#include <string>
#include <unordered_map>
#include <ctime>
#include <mutex>

// Personal best lap entry with metadata
struct PersonalBestEntry {
    std::string trackId;      // Short track identifier (e.g., "club")
    std::string bikeName;     // Full bike name (e.g., "KTM 450 SX-F")
    int lapTime;              // Total lap time in milliseconds
    int sector1;              // Sector 1 time in milliseconds
    int sector2;              // Sector 2 time in milliseconds
    int sector3;              // Sector 3 time in milliseconds
    int sector4;              // Sector 4 time in milliseconds (GP Bikes only, -1 if N/A)

    // Metadata (not part of key)
    std::string setupName;    // Setup filename used
    int conditions;           // Weather conditions
    std::time_t timestamp;    // When PB was set

    PersonalBestEntry()
        : lapTime(-1), sector1(-1), sector2(-1), sector3(-1), sector4(-1)
        , conditions(-1), timestamp(0) {}

    bool isValid() const { return lapTime > 0; }
};

class PersonalBestManager {
public:
    static PersonalBestManager& getInstance();

    // Load/save from JSON file
    // savePath is the plugin save directory (same as settings)
    void load(const char* savePath);
    void save();

    // Lookup personal best for track+bike combination
    // Returns nullptr if no PB exists for this combo
    const PersonalBestEntry* getPersonalBest(const std::string& trackId,
                                              const std::string& bikeName) const;

    // Update personal best - only saves if better than existing
    // Returns true if this was a new PB (and file was saved)
    bool updatePersonalBest(const PersonalBestEntry& entry);

    // Get all entries (for settings UI display)
    const std::unordered_map<std::string, PersonalBestEntry>& getAllEntries() const {
        return m_entries;
    }

    // Get number of stored personal bests
    size_t getEntryCount() const { return m_entries.size(); }

    // Clear a specific entry (returns true if entry existed)
    bool clearEntry(const std::string& trackId, const std::string& bikeName);

    // Clear all entries
    void clearAll();

private:
    PersonalBestManager() = default;
    ~PersonalBestManager() = default;
    PersonalBestManager(const PersonalBestManager&) = delete;
    PersonalBestManager& operator=(const PersonalBestManager&) = delete;

    // Generate lookup key from track and bike
    static std::string makeKey(const std::string& trackId, const std::string& bikeName);

    // Get full path to JSON file
    std::string getFilePath() const;

    // Entries keyed by "trackId|bikeName"
    std::unordered_map<std::string, PersonalBestEntry> m_entries;

    // Save path (set during load)
    std::string m_savePath;

    // Mutex for thread-safe access (in case of async saves)
    mutable std::mutex m_mutex;

    // File format version
    static constexpr int FILE_VERSION = 1;
};
