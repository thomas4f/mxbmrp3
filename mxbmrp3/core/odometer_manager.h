// ============================================================================
// core/odometer_manager.h
// Manages per-bike odometer data (total distance traveled) stored in JSON
// ============================================================================
#pragma once

#include <string>
#include <unordered_map>
#include <mutex>
#include <chrono>

class OdometerManager {
public:
    static OdometerManager& getInstance();

    // Lifecycle
    void load(const char* savePath);
    void save();

    // Bike context
    void setCurrentBike(const std::string& bikeName);
    std::string getCurrentBike() const;  // Returns by value to avoid data race

    // Distance tracking - called from telemetry handler
    void updateDistance(float speedMs);

    // Access methods (using double to avoid precision loss at high distances)
    double getOdometerForCurrentBike() const;     // Total distance for current bike (meters)
    double getSessionTripDistance() const;        // Distance traveled this session (meters)
    double getTotalOdometer() const;              // Total distance across all bikes (meters)

    // Reset methods
    void resetSessionTrip();                      // Resets session trip counter

    // Mark data as dirty (triggers save on next save point)
    void markDirty();
    bool isDirty() const;

private:
    OdometerManager() = default;
    ~OdometerManager() = default;
    OdometerManager(const OdometerManager&) = delete;
    OdometerManager& operator=(const OdometerManager&) = delete;

    std::string getFilePath() const;

    static constexpr int FILE_VERSION = 1;

    mutable std::mutex m_mutex;
    std::string m_savePath;
    std::string m_currentBikeName;
    bool m_dirty = false;

    // Per-bike odometer data (total distance in meters)
    // Using double to maintain precision at high values (100k+ km)
    std::unordered_map<std::string, double> m_bikeOdometers;

    // Session tracking (not persisted)
    double m_sessionTripDistance = 0.0;

    // Time tracking for distance calculation
    std::chrono::steady_clock::time_point m_lastUpdateTime;
    bool m_hasLastUpdateTime = false;
};
