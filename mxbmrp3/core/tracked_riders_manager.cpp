// ============================================================================
// core/tracked_riders_manager.cpp
// Manages tracked riders - riders the user wants to highlight in HUDs
// ============================================================================
#include "tracked_riders_manager.h"
#include "plugin_data.h"
#include "asset_manager.h"
#include "../diagnostics/logger.h"
#include "../vendor/nlohmann/json.hpp"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <cstdlib>
#include <cctype>
#include <windows.h>

// Subdirectory and file name (matches PersonalBestManager pattern)
static constexpr const char* TRACKED_SUBDIRECTORY = "mxbmrp3";
static constexpr const char* TRACKED_FILENAME = "mxbmrp3_tracked_riders.json";

TrackedRidersManager& TrackedRidersManager::getInstance() {
    static TrackedRidersManager instance;
    return instance;
}

// Default icon filename for tracked riders
static constexpr const char* DEFAULT_RIDER_ICON = "circle";

// Helper to get shape index from filename (returns 1 if not found)
static int getDefaultShapeIndex() {
    const auto& assetMgr = AssetManager::getInstance();
    int spriteIndex = assetMgr.getIconSpriteIndex(DEFAULT_RIDER_ICON);
    if (spriteIndex <= 0) return 1;
    return spriteIndex - assetMgr.getFirstIconSpriteIndex() + 1;
}

// Helper to get valid shape bounds
static int getMaxShapeIndex() {
    return static_cast<int>(AssetManager::getInstance().getIconCount());
}

bool TrackedRidersManager::shouldRotate(int shapeIndex) {
    // Get the icon filename and check for directional patterns
    const auto& assetMgr = AssetManager::getInstance();
    int spriteIndex = assetMgr.getFirstIconSpriteIndex() + shapeIndex - 1;
    std::string filename = assetMgr.getIconFilename(spriteIndex);

    if (filename.empty()) return false;

    // Directional icons contain these patterns in their filename
    return filename.find("angle-up") != std::string::npos ||
           filename.find("angles-up") != std::string::npos ||
           filename.find("arrow-up") != std::string::npos ||
           filename.find("caret-up") != std::string::npos ||
           filename.find("chevron") != std::string::npos ||
           filename.find("circle-play") != std::string::npos ||
           filename.find("circle-up") != std::string::npos ||
           filename.find("ghost") != std::string::npos ||
           filename.find("location") != std::string::npos ||
           filename.find("meteor") != std::string::npos ||
           filename.find("paper-plane") != std::string::npos ||
           filename.find("plane-up") != std::string::npos ||
           filename.find("play") != std::string::npos ||
           filename.find("rocket") != std::string::npos;
}

std::string TrackedRidersManager::normalizeName(const std::string& name) {
    std::string normalized = name;

    // Trim leading whitespace
    size_t start = normalized.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    normalized = normalized.substr(start);

    // Trim trailing whitespace
    size_t end = normalized.find_last_not_of(" \t\n\r");
    if (end != std::string::npos) {
        normalized = normalized.substr(0, end + 1);
    }

    // Case-sensitive matching - only trim whitespace, preserve case
    // This ensures "Thomas" and "thomas" are treated as different riders
    return normalized;
}

unsigned long TrackedRidersManager::getNextColor() const {
    size_t index = m_trackedRiders.size() % ColorPalette::ALL_COLORS.size();
    return ColorPalette::ALL_COLORS[index];
}

bool TrackedRidersManager::addTrackedRider(const std::string& name, unsigned long color, int shapeIndex) {
    std::string normalizedName = normalizeName(name);
    if (normalizedName.empty()) return false;

    // Check if already exists
    if (m_trackedRiders.find(normalizedName) != m_trackedRiders.end()) {
        return false;
    }

    // Enforce maximum limit
    if (static_cast<int>(m_trackedRiders.size()) >= MAX_TRACKED_RIDERS) {
        DEBUG_INFO_F("TrackedRidersManager: Cannot add rider '%s', max limit (%d) reached",
                     name.c_str(), MAX_TRACKED_RIDERS);
        return false;
    }

    // Auto-assign color if not specified (0 = auto)
    if (color == 0) {
        color = getNextColor();
    }

    // Clamp shape index to valid range
    int maxShape = getMaxShapeIndex();
    if (shapeIndex < 1 || shapeIndex > maxShape) {
        shapeIndex = getDefaultShapeIndex();
    }

    // Add new tracked rider (store original name for display)
    TrackedRiderConfig config;
    config.name = name;  // Store original name with original case
    config.color = color;
    config.shapeIndex = shapeIndex;

    m_trackedRiders[normalizedName] = config;
    m_bDirty = true;
    m_needsSave = true;
    PluginData::getInstance().notifyTrackedRidersChanged();

    DEBUG_INFO_F("TrackedRidersManager: Added rider '%s' with color %lu and shape %d",
                 name.c_str(), color, shapeIndex);

    return true;
}

bool TrackedRidersManager::removeTrackedRider(const std::string& name) {
    std::string normalizedName = normalizeName(name);
    if (normalizedName.empty()) return false;

    auto it = m_trackedRiders.find(normalizedName);
    if (it == m_trackedRiders.end()) {
        return false;
    }

    DEBUG_INFO_F("TrackedRidersManager: Removed rider '%s'", name.c_str());
    m_trackedRiders.erase(it);
    m_bDirty = true;
    m_needsSave = true;
    PluginData::getInstance().notifyTrackedRidersChanged();

    return true;
}

bool TrackedRidersManager::isTracked(const std::string& name) const {
    std::string normalizedName = normalizeName(name);
    return m_trackedRiders.find(normalizedName) != m_trackedRiders.end();
}

const TrackedRiderConfig* TrackedRidersManager::getTrackedRider(const std::string& name) const {
    std::string normalizedName = normalizeName(name);
    auto it = m_trackedRiders.find(normalizedName);
    if (it != m_trackedRiders.end()) {
        return &it->second;
    }
    return nullptr;
}

void TrackedRidersManager::setTrackedRiderColor(const std::string& name, unsigned long color) {
    std::string normalizedName = normalizeName(name);
    auto it = m_trackedRiders.find(normalizedName);
    if (it != m_trackedRiders.end()) {
        it->second.color = color;
        m_bDirty = true;
        m_needsSave = true;
        PluginData::getInstance().notifyTrackedRidersChanged();
    }
}

void TrackedRidersManager::setTrackedRiderShape(const std::string& name, int shapeIndex) {
    std::string normalizedName = normalizeName(name);
    auto it = m_trackedRiders.find(normalizedName);
    if (it != m_trackedRiders.end()) {
        // Wrap around to valid range
        int maxShape = getMaxShapeIndex();
        if (shapeIndex < 1) shapeIndex = maxShape;
        else if (shapeIndex > maxShape) shapeIndex = 1;
        it->second.shapeIndex = shapeIndex;
        m_bDirty = true;
        m_needsSave = true;
        PluginData::getInstance().notifyTrackedRidersChanged();
    }
}

void TrackedRidersManager::cycleTrackedRiderColor(const std::string& name, bool forward) {
    std::string normalizedName = normalizeName(name);
    auto it = m_trackedRiders.find(normalizedName);
    if (it != m_trackedRiders.end()) {
        // Find current color in palette
        int currentIndex = ColorPalette::getColorIndex(it->second.color);
        int paletteSize = static_cast<int>(ColorPalette::ALL_COLORS.size());

        if (currentIndex < 0) {
            // Color not in palette, start at first color
            currentIndex = 0;
        } else {
            // Cycle to next/previous
            if (forward) {
                currentIndex = (currentIndex + 1) % paletteSize;
            } else {
                currentIndex = (currentIndex - 1 + paletteSize) % paletteSize;
            }
        }

        it->second.color = ColorPalette::ALL_COLORS[currentIndex];
        m_bDirty = true;
        m_needsSave = true;
        PluginData::getInstance().notifyTrackedRidersChanged();
    }
}

void TrackedRidersManager::cycleTrackedRiderShape(const std::string& name, bool forward) {
    std::string normalizedName = normalizeName(name);
    auto it = m_trackedRiders.find(normalizedName);
    if (it != m_trackedRiders.end()) {
        int maxShape = getMaxShapeIndex();
        int shape = it->second.shapeIndex;
        if (forward) {
            shape++;
            if (shape > maxShape) shape = 1;
        } else {
            shape--;
            if (shape < 1) shape = maxShape;
        }
        it->second.shapeIndex = shape;
        m_bDirty = true;
        m_needsSave = true;
        PluginData::getInstance().notifyTrackedRidersChanged();
    }
}

void TrackedRidersManager::clearAll() {
    if (!m_trackedRiders.empty()) {
        m_trackedRiders.clear();
        m_bDirty = true;
        m_needsSave = true;
        PluginData::getInstance().notifyTrackedRidersChanged();
        DEBUG_INFO("TrackedRidersManager: Cleared all tracked riders");
    }
}

std::string TrackedRidersManager::getFilePath() const {
    std::string path;

    if (m_savePath.empty()) {
        path = std::string(".\\") + TRACKED_SUBDIRECTORY;
    } else {
        path = m_savePath;
        if (!path.empty() && path.back() != '/' && path.back() != '\\') {
            path += '\\';
        }
        path += TRACKED_SUBDIRECTORY;
    }

    // Ensure directory exists
    if (!CreateDirectoryA(path.c_str(), NULL)) {
        DWORD error = GetLastError();
        if (error != ERROR_ALREADY_EXISTS) {
            DEBUG_INFO_F("[TrackedRidersManager] Failed to create directory: %s (error %lu)", path.c_str(), error);
        }
    }

    return path + "\\" + TRACKED_FILENAME;
}

void TrackedRidersManager::load(const char* savePath) {
    m_savePath = savePath ? savePath : "";
    m_trackedRiders.clear();
    m_needsSave = false;  // Reset save flag on load

    std::string filePath = getFilePath();

    std::ifstream file(filePath);
    if (!file.is_open()) {
        DEBUG_INFO_F("[TrackedRidersManager] No tracked riders file found at %s", filePath.c_str());
        return;
    }

    try {
        nlohmann::json j;
        file >> j;

        // Check version
        int version = j.value("version", 0);
        if (version != 1) {
            DEBUG_INFO_F("[TrackedRidersManager] Version mismatch: file=%d, expected=1. Starting fresh.", version);
            return;
        }

        // Parse riders array
        if (j.contains("riders") && j["riders"].is_array()) {
            const auto& assetMgr = AssetManager::getInstance();

            for (const auto& riderJson : j["riders"]) {
                std::string name = riderJson.value("name", "");
                std::string colorStr = riderJson.value("color", "");

                if (name.empty() || colorStr.empty()) continue;

                // Parse color (#RRGGBB hex format)
                unsigned long color = ColorPalette::RED;
                if (colorStr.length() == 7 && colorStr[0] == '#') {
                    try {
                        color = std::stoul(colorStr.substr(1), nullptr, 16) | 0xFF000000;
                    } catch (...) {
                        color = ColorPalette::RED;
                    }
                }

                // Parse icon by name
                int shapeIndex = getDefaultShapeIndex();
                std::string iconName = riderJson.value("icon", "");
                if (!iconName.empty()) {
                    int spriteIndex = assetMgr.getIconSpriteIndex(iconName);
                    if (spriteIndex > 0) {
                        shapeIndex = spriteIndex - assetMgr.getFirstIconSpriteIndex() + 1;
                    }
                }

                // Add rider
                std::string normalizedName = normalizeName(name);
                if (!normalizedName.empty()) {
                    TrackedRiderConfig config;
                    config.name = name;
                    config.color = color;
                    config.shapeIndex = shapeIndex;
                    m_trackedRiders[normalizedName] = config;
                }
            }
        }

        DEBUG_INFO_F("[TrackedRidersManager] Loaded %zu tracked riders from %s",
                     m_trackedRiders.size(), filePath.c_str());

    } catch (const nlohmann::json::exception& e) {
        DEBUG_INFO_F("[TrackedRidersManager] Failed to parse JSON: %s", e.what());
        m_trackedRiders.clear();
    } catch (const std::exception& e) {
        DEBUG_INFO_F("[TrackedRidersManager] Error loading tracked riders: %s", e.what());
        m_trackedRiders.clear();
    }
}

void TrackedRidersManager::save() {
    // Only save if data has changed since last save/load
    if (!m_needsSave) {
        return;
    }

    std::string filePath = getFilePath();
    std::string tempPath = filePath + ".tmp";

    try {
        nlohmann::json j;
        j["version"] = 1;

        nlohmann::json riders = nlohmann::json::array();
        for (const auto& [key, config] : m_trackedRiders) {
            nlohmann::json riderJson;
            riderJson["name"] = config.name;

            // Format color as #RRGGBB
            std::ostringstream colorStream;
            colorStream << "#" << std::hex << std::setfill('0') << std::setw(6) << (config.color & 0xFFFFFF);
            riderJson["color"] = colorStream.str();

            // Convert shape index to icon filename for robustness
            const auto& assetMgr = AssetManager::getInstance();
            int spriteIndex = assetMgr.getFirstIconSpriteIndex() + config.shapeIndex - 1;
            std::string iconName = assetMgr.getIconFilename(spriteIndex);
            riderJson["icon"] = iconName.empty() ? DEFAULT_RIDER_ICON : iconName;

            riders.push_back(riderJson);
        }
        j["riders"] = riders;

        // Write to temp file first
        std::ofstream tempFile(tempPath);
        if (!tempFile.is_open()) {
            DEBUG_INFO_F("[TrackedRidersManager] Failed to open temp file for writing: %s", tempPath.c_str());
            return;
        }

        tempFile << j.dump(2);  // Pretty print with 2-space indent
        tempFile.close();

        if (tempFile.fail()) {
            DEBUG_INFO_F("[TrackedRidersManager] Failed to write temp file: %s", tempPath.c_str());
            std::remove(tempPath.c_str());
            return;
        }

        // Atomic rename
        if (!MoveFileExA(tempPath.c_str(), filePath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            DEBUG_WARN_F("[TrackedRidersManager] Failed to save file (error %lu): %s", GetLastError(), filePath.c_str());
            std::remove(tempPath.c_str());
            return;
        }

        m_needsSave = false;  // Reset flag after successful save
        DEBUG_INFO_F("[TrackedRidersManager] Saved %zu tracked riders to %s",
                     m_trackedRiders.size(), filePath.c_str());

    } catch (const std::exception& e) {
        DEBUG_INFO_F("[TrackedRidersManager] Error saving tracked riders: %s", e.what());
        std::remove(tempPath.c_str());
    }
}
