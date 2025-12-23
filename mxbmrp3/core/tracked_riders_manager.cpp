// ============================================================================
// core/tracked_riders_manager.cpp
// Manages tracked riders - riders the user wants to highlight in HUDs
// ============================================================================
#include "tracked_riders_manager.h"
#include "plugin_data.h"
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

const char* TrackedRidersManager::getShapeName(int shapeIndex) {
    switch (shapeIndex) {
        case SHAPE_ALERT:       return "Alert";
        case SHAPE_ARROWUP:     return "ArrowUp";
        case SHAPE_AWARD:       return "Award";
        case SHAPE_BAN:         return "Ban";
        case SHAPE_BOLT:        return "Bolt";
        case SHAPE_BOMB:        return "Bomb";
        case SHAPE_BULLSEYE:    return "Bullseye";
        case SHAPE_CERTIFICATE: return "Certificate";
        case SHAPE_CHEVRON:     return "Chevron";
        case SHAPE_CIRCLE:      return "Circle";
        case SHAPE_CIRCLEPLAY:  return "CirclePlay";
        case SHAPE_CIRCLEUP:    return "CircleUp";
        case SHAPE_CROSSBONES:  return "Crossbones";
        case SHAPE_CROWN:       return "Crown";
        case SHAPE_DIAMOND:     return "Diamond";
        case SHAPE_DOT:         return "Dot";
        case SHAPE_EYE:         return "Eye";
        case SHAPE_FIRE:        return "Fire";
        case SHAPE_FLAG:        return "Flag";
        case SHAPE_GHOST:       return "Ghost";
        case SHAPE_HEART:       return "Heart";
        case SHAPE_HEXAGON:     return "Hexagon";
        case SHAPE_LOCATION:    return "Location";
        case SHAPE_MASK:        return "Mask";
        case SHAPE_MEDAL:       return "Medal";
        case SHAPE_METEOR:      return "Meteor";
        case SHAPE_MUG:         return "Mug";
        case SHAPE_OCTAGON:     return "Octagon";
        case SHAPE_PEACE:       return "Peace";
        case SHAPE_PENTAGON:    return "Pentagon";
        case SHAPE_PIN:         return "Pin";
        case SHAPE_PLANE:       return "Plane";
        case SHAPE_PLANEUP:     return "PlaneUp";
        case SHAPE_PLAY:        return "Play";
        case SHAPE_POO:         return "Poo";
        case SHAPE_RADIATION:   return "Radiation";
        case SHAPE_ROBOT:       return "Robot";
        case SHAPE_ROCKET:      return "Rocket";
        case SHAPE_SHIELD:      return "Shield";
        case SHAPE_SKULL:       return "Skull";
        case SHAPE_SNOWFLAKE:   return "Snowflake";
        case SHAPE_STAR:        return "Star";
        case SHAPE_STARLIFE:    return "StarLife";
        case SHAPE_TROPHY:      return "Trophy";
        case SHAPE_USER:        return "User";
        case SHAPE_VINYL:       return "Vinyl";
        case SHAPE_WARNING:     return "Warning";
        case SHAPE_WEB:         return "Web";
        case SHAPE_X:           return "X";
        case SHAPE_XMARK:       return "Xmark";
        default:                return "Circle";
    }
}

bool TrackedRidersManager::shouldRotate(int shapeIndex) {
    // Directional icons that should rotate with rider heading
    switch (shapeIndex) {
        case SHAPE_ARROWUP:
        case SHAPE_CHEVRON:
        case SHAPE_CIRCLEPLAY:
        case SHAPE_CIRCLEUP:
        case SHAPE_GHOST:
        case SHAPE_LOCATION:
        case SHAPE_METEOR:
        case SHAPE_PLANE:
        case SHAPE_PLANEUP:
        case SHAPE_PLAY:
        case SHAPE_ROCKET:
            return true;
        default:
            return false;  // Symmetric icons don't rotate
    }
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
    if (shapeIndex < SHAPE_MIN || shapeIndex > SHAPE_MAX) {
        shapeIndex = SHAPE_CIRCLE;
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
        // Clamp to valid range
        if (shapeIndex < SHAPE_MIN) shapeIndex = SHAPE_MAX;
        else if (shapeIndex > SHAPE_MAX) shapeIndex = SHAPE_MIN;
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
        int shape = it->second.shapeIndex;
        if (forward) {
            shape++;
            if (shape > SHAPE_MAX) shape = SHAPE_MIN;
        } else {
            shape--;
            if (shape < SHAPE_MIN) shape = SHAPE_MAX;
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
                if (shapeIndex < SHAPE_MIN || shapeIndex > SHAPE_MAX) {
                    shapeIndex = SHAPE_CIRCLE;
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
