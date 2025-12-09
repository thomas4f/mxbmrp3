// ============================================================================
// core/color_config.cpp
// User-configurable color settings for HUD elements
// ============================================================================
#include "color_config.h"
#include "../diagnostics/logger.h"

namespace ColorPalette {
    const char* getColorName(unsigned long color) {
        switch (color) {
            case WHITE:      return "White";
            case LIGHT_GRAY: return "Light Gray";
            case GRAY:       return "Gray";
            case DARK_GRAY:  return "Dark Gray";
            case BLACK:      return "Black";
            case RED:        return "Red";
            case GREEN:      return "Green";
            case BLUE:       return "Blue";
            case YELLOW:     return "Yellow";
            case ORANGE:     return "Orange";
            case CYAN:       return "Cyan";
            case PURPLE:     return "Purple";
            case PINK:       return "Pink";
            default:         return "Custom";
        }
    }

    int getColorIndex(unsigned long color) {
        for (size_t i = 0; i < ALL_COLORS.size(); ++i) {
            if (ALL_COLORS[i] == color) {
                return static_cast<int>(i);
            }
        }
        return -1;  // Not in palette
    }
}

ColorConfig& ColorConfig::getInstance() {
    static ColorConfig instance;
    return instance;
}

ColorConfig::ColorConfig() {
    resetToDefaults();
}

unsigned long ColorConfig::getColor(ColorSlot slot) const {
    size_t index = static_cast<size_t>(slot);
    if (index < m_colors.size()) {
        return m_colors[index];
    }
    return ColorPalette::WHITE;  // Fallback
}

void ColorConfig::setColor(ColorSlot slot, unsigned long color) {
    size_t index = static_cast<size_t>(slot);
    if (index < m_colors.size()) {
        m_colors[index] = color;
        DEBUG_INFO_F("ColorConfig: %s set to %s (0x%08lX)",
            getSlotName(slot), ColorPalette::getColorName(color), color);
    }
}

void ColorConfig::cycleColor(ColorSlot slot, bool forward) {
    size_t slotIndex = static_cast<size_t>(slot);
    if (slotIndex >= m_colors.size()) return;

    unsigned long currentColor = m_colors[slotIndex];
    int paletteIndex = ColorPalette::getColorIndex(currentColor);

    if (paletteIndex < 0) {
        // Color not in palette, start from beginning
        paletteIndex = 0;
    } else {
        // Cycle to next/previous
        int paletteSize = static_cast<int>(ColorPalette::ALL_COLORS.size());
        if (forward) {
            paletteIndex = (paletteIndex + 1) % paletteSize;
        } else {
            paletteIndex = (paletteIndex - 1 + paletteSize) % paletteSize;
        }
    }

    unsigned long newColor = ColorPalette::ALL_COLORS[paletteIndex];
    m_colors[slotIndex] = newColor;

    DEBUG_INFO_F("ColorConfig: %s cycled to %s (0x%08lX)",
        getSlotName(slot), ColorPalette::getColorName(newColor), newColor);
}

void ColorConfig::resetToDefaults() {
    m_colors[static_cast<size_t>(ColorSlot::PRIMARY)] = getDefaultColor(ColorSlot::PRIMARY);
    m_colors[static_cast<size_t>(ColorSlot::SECONDARY)] = getDefaultColor(ColorSlot::SECONDARY);
    m_colors[static_cast<size_t>(ColorSlot::TERTIARY)] = getDefaultColor(ColorSlot::TERTIARY);
    m_colors[static_cast<size_t>(ColorSlot::MUTED)] = getDefaultColor(ColorSlot::MUTED);
    m_colors[static_cast<size_t>(ColorSlot::BACKGROUND)] = getDefaultColor(ColorSlot::BACKGROUND);
    m_colors[static_cast<size_t>(ColorSlot::POSITIVE)] = getDefaultColor(ColorSlot::POSITIVE);
    m_colors[static_cast<size_t>(ColorSlot::WARNING)] = getDefaultColor(ColorSlot::WARNING);
    m_colors[static_cast<size_t>(ColorSlot::NEUTRAL)] = getDefaultColor(ColorSlot::NEUTRAL);
    m_colors[static_cast<size_t>(ColorSlot::NEGATIVE)] = getDefaultColor(ColorSlot::NEGATIVE);
    m_colors[static_cast<size_t>(ColorSlot::ACCENT)] = getDefaultColor(ColorSlot::ACCENT);
    m_bGridSnapping = true;  // Grid snapping enabled by default

    DEBUG_INFO("ColorConfig: Reset to defaults");
}

const char* ColorConfig::getSlotName(ColorSlot slot) {
    switch (slot) {
        case ColorSlot::PRIMARY:    return "Primary";
        case ColorSlot::SECONDARY:  return "Secondary";
        case ColorSlot::TERTIARY:   return "Tertiary";
        case ColorSlot::MUTED:      return "Muted";
        case ColorSlot::BACKGROUND: return "Background";
        case ColorSlot::POSITIVE:   return "Positive";
        case ColorSlot::WARNING:    return "Warning";
        case ColorSlot::NEUTRAL:    return "Neutral";
        case ColorSlot::NEGATIVE:   return "Negative";
        case ColorSlot::ACCENT:     return "Accent";
        default:                    return "Unknown";
    }
}

unsigned long ColorConfig::getDefaultColor(ColorSlot slot) {
    // Default values match the original TextColors and SemanticColors
    switch (slot) {
        case ColorSlot::PRIMARY:    return ColorPalette::WHITE;       // #ffffff
        case ColorSlot::SECONDARY:  return ColorPalette::LIGHT_GRAY;  // #bebebe
        case ColorSlot::TERTIARY:   return ColorPalette::GRAY;        // #8c8c8c
        case ColorSlot::MUTED:      return ColorPalette::DARK_GRAY;   // #646464
        case ColorSlot::BACKGROUND: return ColorPalette::BLACK;       // #000000
        case ColorSlot::POSITIVE:   return ColorPalette::GREEN;       // #00ff00
        case ColorSlot::WARNING:    return ColorPalette::ORANGE;      // #ffa500
        case ColorSlot::NEUTRAL:    return ColorPalette::YELLOW;      // #ffff00
        case ColorSlot::NEGATIVE:   return ColorPalette::RED;         // #ff0000
        case ColorSlot::ACCENT:     return ColorPalette::PINK;        // #ff69b4
        default:                    return ColorPalette::WHITE;
    }
}
