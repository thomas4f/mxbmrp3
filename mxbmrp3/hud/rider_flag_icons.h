// ============================================================================
// hud/rider_flag_icons.h
// The flag-class markers a rider can carry on the Standings row, the Map and
// the Radar -- wrong way, hazard, blue flag, last lap, finished -- as one
// sprite + colour pairing, so the three HUDs cannot draw the same state in
// different colours.
//
// FLAGS KEEP THEIR REAL COLOURS. A flag's colour is its meaning (yellow
// hazard, blue flag, white last lap, checkered finish, green start), so these
// are fixed palette values, never ColorSlots a theme can repaint. The
// wrong-way marker is not a flag: it is a warning and follows the HUD's
// NEGATIVE slot, like the WRONG WAY notice. The Event Log and the blue-flag
// notice read the same FlagColors.
//
// Only the pairing is shared. WHICH marker wins is each HUD's own order (the
// Standings row also shows a director lock and the pit wrench; the Map skips
// the local player), so that stays with the HUD.
// ============================================================================
#pragma once

#include "../core/color_config.h"
#include "../core/plugin_data_types.h"

namespace FlagColors {
    constexpr unsigned long START = ColorPalette::GREEN;
    constexpr unsigned long HAZARD = ColorPalette::BRIGHT_YELLOW;
    constexpr unsigned long BLUE = ColorPalette::BLUE;
    constexpr unsigned long LAST_LAP = ColorPalette::WHITE;
    constexpr unsigned long CHECKERED = ColorPalette::WHITE;
}

class RiderFlagIcons {
public:
    enum class Kind { None, WrongWay, Hazard, Blue, LastLap, Finished };

    // sprite 0 = the icon is missing from the asset set: draw nothing for it
    struct Icon {
        int sprite = 0;
        unsigned long color = 0;
    };

    // The marker for a rider's hazard state (callers check HazardType::None first)
    static Kind forHazard(HazardType type) {
        return type == HazardType::WrongWay ? Kind::WrongWay : Kind::Hazard;
    }

    // wrongWayColor: the calling HUD's NEGATIVE slot (honours its colour overrides)
    Icon get(Kind kind, unsigned long wrongWayColor);

private:
    void ensureInitialized();

    // Sprite indices, looked up once (a string-keyed lookup per rider per frame otherwise)
    int m_circleExclamation = 0;
    int m_flag = 0;
    int m_flagCheckered = 0;
    bool m_initialized = false;
};
