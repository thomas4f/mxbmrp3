// ============================================================================
// core/tracked_riders_manager.cpp
// Manages tracked riders - riders the user wants to highlight in HUDs
// ============================================================================
#include "tracked_riders_manager.h"
#include "plugin_data.h"
#include "asset_manager.h"
#include "../diagnostics/logger.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cstdlib>
#include <cctype>

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
        PluginData::getInstance().notifyTrackedRidersChanged();
    }
}

void TrackedRidersManager::clearAll() {
    if (!m_trackedRiders.empty()) {
        m_trackedRiders.clear();
        m_bDirty = true;
        PluginData::getInstance().notifyTrackedRidersChanged();
        DEBUG_INFO("TrackedRidersManager: Cleared all tracked riders");
    }
}

// Encode special characters in name for safe serialization
// Uses percent-encoding for delimiters: % -> %25, | -> %7C, ; -> %3B
static std::string encodeName(const std::string& name) {
    std::ostringstream encoded;
    for (char c : name) {
        if (c == '%') {
            encoded << "%25";
        } else if (c == '|') {
            encoded << "%7C";
        } else if (c == ';') {
            encoded << "%3B";
        } else {
            encoded << c;
        }
    }
    return encoded.str();
}

// Decode percent-encoded name
static std::string decodeName(const std::string& encoded) {
    std::string decoded;
    decoded.reserve(encoded.size());
    for (size_t i = 0; i < encoded.size(); ++i) {
        if (encoded[i] == '%' && i + 2 < encoded.size()) {
            char hex[3] = { encoded[i + 1], encoded[i + 2], '\0' };
            char* end;
            long val = strtol(hex, &end, 16);
            if (end == hex + 2) {
                decoded += static_cast<char>(val);
                i += 2;
                continue;
            }
        }
        decoded += encoded[i];
    }
    return decoded;
}

std::string TrackedRidersManager::serializeToString() const {
    std::ostringstream oss;
    bool first = true;

    for (const auto& pair : m_trackedRiders) {
        const TrackedRiderConfig& config = pair.second;

        if (!first) {
            oss << ";";
        }
        first = false;

        // Format: name|#RRGGBB|shape
        // Encode name to handle special characters (|, ;, %)
        // Store color as hex for readability
        oss << encodeName(config.name) << "|#";
        oss << std::hex << std::setfill('0') << std::setw(6) << (config.color & 0xFFFFFF);
        oss << std::dec << "|" << config.shapeIndex;
    }

    return oss.str();
}

void TrackedRidersManager::deserializeFromString(const std::string& data) {
    m_trackedRiders.clear();

    if (data.empty()) {
        return;
    }

    std::istringstream iss(data);
    std::string entry;

    while (std::getline(iss, entry, ';')) {
        if (entry.empty()) continue;

        // Parse: name|color|shape
        std::istringstream entryStream(entry);
        std::string encodedName, colorStr, shapeStr;

        if (std::getline(entryStream, encodedName, '|') &&
            std::getline(entryStream, colorStr, '|') &&
            std::getline(entryStream, shapeStr, '|')) {

            try {
                // Decode name (handles percent-encoded special chars)
                std::string name = decodeName(encodedName);

                // Parse color (#RRGGBB hex format)
                if (colorStr.empty() || colorStr[0] != '#') {
                    continue;  // Skip malformed entries
                }
                unsigned long color = std::stoul(colorStr.substr(1), nullptr, 16) | 0xFF000000;

                int shapeIndex = std::stoi(shapeStr);

                // Clamp shape to valid range
                int maxShape = getMaxShapeIndex();
                if (shapeIndex < 1 || shapeIndex > maxShape) {
                    shapeIndex = getDefaultShapeIndex();
                }

                // Add rider (normalized internally)
                std::string normalizedName = normalizeName(name);
                if (!normalizedName.empty()) {
                    TrackedRiderConfig config;
                    config.name = name;  // Preserve original case
                    config.color = color;
                    config.shapeIndex = shapeIndex;
                    m_trackedRiders[normalizedName] = config;
                }
            } catch (...) {
                // Skip malformed entries
                DEBUG_INFO_F("TrackedRidersManager: Skipping malformed entry '%s'", entry.c_str());
            }
        }
    }

    DEBUG_INFO_F("TrackedRidersManager: Loaded %zu tracked riders", m_trackedRiders.size());
}
