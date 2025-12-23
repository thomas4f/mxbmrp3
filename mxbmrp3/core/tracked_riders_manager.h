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
    int shapeIndex;          // Icon index (1-50): maps to rider icon sprites

    TrackedRiderConfig() : color(ColorPalette::RED), shapeIndex(12) {}  // Default to Circle (SHAPE_CIRCLE = 12)
    TrackedRiderConfig(const std::string& riderName, unsigned long c, int shape)
        : name(riderName), color(c), shapeIndex(shape) {}
};

class TrackedRidersManager {
public:
    static TrackedRidersManager& getInstance();

    // Icon constants - 50 icons alphabetically ordered by filename (1-indexed)
    // Files: award, ban, bolt-lightning, bomb, bullseye, certificate, circle-arrow-up,
    //        circle-chevron-up, circle-dot, circle-exclamation, circle-play, circle,
    //        circle-up, circle-user, circle-xmark, crown, diamond, eye, fire, flag,
    //        ghost, heart, hexagon, location-arrow, location-pin, mask, medal, meteor,
    //        mug-hot, octagon, paper-plane, peace, pentagon, plane-up, play, poo,
    //        radiation, record-vinyl, robot, rocket, shield, skull-crossbones, skull,
    //        snowflake, star-of-life, star, triangle-exclamation, trophy, web-awesome, xmark
    static constexpr int SHAPE_AWARD = 1;
    static constexpr int SHAPE_BAN = 2;
    static constexpr int SHAPE_BOLT = 3;
    static constexpr int SHAPE_BOMB = 4;
    static constexpr int SHAPE_BULLSEYE = 5;
    static constexpr int SHAPE_CERTIFICATE = 6;
    static constexpr int SHAPE_ARROWUP = 7;
    static constexpr int SHAPE_CHEVRON = 8;
    static constexpr int SHAPE_DOT = 9;
    static constexpr int SHAPE_ALERT = 10;
    static constexpr int SHAPE_CIRCLEPLAY = 11;
    static constexpr int SHAPE_CIRCLE = 12;
    static constexpr int SHAPE_CIRCLEUP = 13;
    static constexpr int SHAPE_USER = 14;
    static constexpr int SHAPE_X = 15;
    static constexpr int SHAPE_CROWN = 16;
    static constexpr int SHAPE_DIAMOND = 17;
    static constexpr int SHAPE_EYE = 18;
    static constexpr int SHAPE_FIRE = 19;
    static constexpr int SHAPE_FLAG = 20;
    static constexpr int SHAPE_GHOST = 21;
    static constexpr int SHAPE_HEART = 22;
    static constexpr int SHAPE_HEXAGON = 23;
    static constexpr int SHAPE_LOCATION = 24;
    static constexpr int SHAPE_PIN = 25;
    static constexpr int SHAPE_MASK = 26;
    static constexpr int SHAPE_MEDAL = 27;
    static constexpr int SHAPE_METEOR = 28;
    static constexpr int SHAPE_MUG = 29;
    static constexpr int SHAPE_OCTAGON = 30;
    static constexpr int SHAPE_PLANE = 31;
    static constexpr int SHAPE_PEACE = 32;
    static constexpr int SHAPE_PENTAGON = 33;
    static constexpr int SHAPE_PLANEUP = 34;
    static constexpr int SHAPE_PLAY = 35;
    static constexpr int SHAPE_POO = 36;
    static constexpr int SHAPE_RADIATION = 37;
    static constexpr int SHAPE_VINYL = 38;
    static constexpr int SHAPE_ROBOT = 39;
    static constexpr int SHAPE_ROCKET = 40;
    static constexpr int SHAPE_SHIELD = 41;
    static constexpr int SHAPE_CROSSBONES = 42;
    static constexpr int SHAPE_SKULL = 43;
    static constexpr int SHAPE_SNOWFLAKE = 44;
    static constexpr int SHAPE_STARLIFE = 45;
    static constexpr int SHAPE_STAR = 46;
    static constexpr int SHAPE_WARNING = 47;
    static constexpr int SHAPE_TROPHY = 48;
    static constexpr int SHAPE_WEB = 49;
    static constexpr int SHAPE_XMARK = 50;
    // Icon range
    static constexpr int SHAPE_COUNT = 50;
    static constexpr int SHAPE_MIN = 1;
    static constexpr int SHAPE_MAX = 50;

    // Maximum tracked riders (5 pages × 36 per page)
    static constexpr int MAX_TRACKED_RIDERS = 180;

    // Get display name for shape index
    static const char* getShapeName(int shapeIndex);

    // Check if icon should rotate with rider heading (directional icons)
    static bool shouldRotate(int shapeIndex);

    // Get next color for new rider (cycles through ColorPalette::ALL_COLORS)
    unsigned long getNextColor() const;

    // Add a rider to tracking list (color=0 means auto-assign next color)
    // Returns true if added, false if already exists or at capacity
    bool addTrackedRider(const std::string& name, unsigned long color = 0, int shapeIndex = SHAPE_CIRCLE);

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

    // Persistence - load/save from/to INI format strings
    // Format: "name1|color1|shape1;name2|color2|shape2;..."
    std::string serializeToString() const;
    void deserializeFromString(const std::string& data);

    // Mark that tracked riders have changed (for HUD updates)
    void markDirty() { m_bDirty = true; }
    bool isDirty() const { return m_bDirty; }
    void clearDirty() { m_bDirty = false; }

private:
    TrackedRidersManager() : m_bDirty(false) {}
    ~TrackedRidersManager() = default;
    TrackedRidersManager(const TrackedRidersManager&) = delete;
    TrackedRidersManager& operator=(const TrackedRidersManager&) = delete;

    // Normalize name for consistent matching (lowercase, trimmed)
    static std::string normalizeName(const std::string& name);

    // Tracked riders storage (key = normalized name)
    std::unordered_map<std::string, TrackedRiderConfig> m_trackedRiders;

    // Dirty flag for HUD updates
    bool m_bDirty;
};
