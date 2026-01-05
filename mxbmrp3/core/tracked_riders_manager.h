// ============================================================================
// core/tracked_riders_manager.h
// Manages tracked riders - riders the user wants to highlight in HUDs
// ============================================================================
#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include "color_config.h"

// Configuration for a tracked rider
struct TrackedRiderConfig {
    std::string name;        // Rider name (used as key, stored normalized for matching)
    unsigned long color;     // Custom color for this rider
    int shapeIndex;          // Icon shape index (1-based offset into icon list)

    TrackedRiderConfig() : color(ColorPalette::RED), shapeIndex(1) {}  // Default to first icon
    TrackedRiderConfig(const std::string& riderName, unsigned long c, int shape)
        : name(riderName), color(c), shapeIndex(shape) {}
};

class TrackedRidersManager {
public:
    static TrackedRidersManager& getInstance();

    // Icon shape indices are 1-based offsets into the icon list discovered by AssetManager
    // The actual icons available depend on files in mxbmrp3_data/icons/
    // Use AssetManager::getIconSpriteIndex(filename) to get sprite index for a specific icon
    // Use AssetManager::getIconCount() to get the maximum valid shape index

    // Maximum tracked riders (5 pages × 36 per page)
    static constexpr int MAX_TRACKED_RIDERS = 180;

    // Check if icon should rotate with rider heading (based on filename patterns)
    static bool shouldRotate(int shapeIndex);

    // Get next color for new rider (cycles through ColorPalette::ALL_COLORS)
    unsigned long getNextColor() const;

    // Add a rider to tracking list (color=0 means auto-assign next color, shapeIndex=0 means use default)
    // Returns true if added, false if already exists or at capacity
    bool addTrackedRider(const std::string& name, unsigned long color = 0, int shapeIndex = 0);

    // Check if at maximum capacity
    bool isAtCapacity() const { return static_cast<int>(m_trackedRiders.size()) >= MAX_TRACKED_RIDERS; }
    int getTrackedCount() const { return static_cast<int>(m_trackedRiders.size()); }

    // Remove a rider from tracking list
    // Returns true if removed, false if not found
    bool removeTrackedRider(const std::string& name);

    // Check if a rider is tracked
    bool isTracked(const std::string& name) const;

    // Get tracked rider config (returns nullptr if not tracked)
    const TrackedRiderConfig* getTrackedRider(const std::string& name) const;

    // Update tracked rider settings
    void setTrackedRiderColor(const std::string& name, unsigned long color);
    void setTrackedRiderShape(const std::string& name, int shapeIndex);
    void cycleTrackedRiderColor(const std::string& name, bool forward = true);
    void cycleTrackedRiderShape(const std::string& name, bool forward = true);

    // Get all tracked riders (for settings UI and persistence)
    const std::unordered_map<std::string, TrackedRiderConfig>& getAllTrackedRiders() const {
        return m_trackedRiders;
    }

    // Clear all tracked riders
    void clearAll();

    // Persistence - load/save from JSON file
    void load(const char* savePath);
    void save();

    // Mark that tracked riders have changed (for HUD updates)
    void markDirty() { m_bDirty = true; }
    bool isDirty() const { return m_bDirty; }
    void clearDirty() { m_bDirty = false; }

    // Check if data needs to be saved (modified since last save/load)
    bool needsSave() const { return m_needsSave; }

private:
    TrackedRidersManager() : m_bDirty(false), m_needsSave(false) {}
    ~TrackedRidersManager() = default;
    TrackedRidersManager(const TrackedRidersManager&) = delete;
    TrackedRidersManager& operator=(const TrackedRidersManager&) = delete;

    // Normalize name for consistent matching (lowercase, trimmed)
    static std::string normalizeName(const std::string& name);

    // Get full path to JSON file
    std::string getFilePath() const;

    // Tracked riders storage (key = normalized name)
    std::unordered_map<std::string, TrackedRiderConfig> m_trackedRiders;

    // Save path (set during load)
    std::string m_savePath;

    // Dirty flag for HUD updates
    bool m_bDirty;

    // Flag to track if data has changed since last save/load
    bool m_needsSave;
};
