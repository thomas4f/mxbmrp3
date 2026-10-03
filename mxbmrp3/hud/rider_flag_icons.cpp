// ============================================================================
// hud/rider_flag_icons.cpp
// See rider_flag_icons.h.
// ============================================================================
#include "rider_flag_icons.h"
#include "../core/asset_manager.h"

void RiderFlagIcons::ensureInitialized() {
    if (m_initialized) return;
    // getIconSpriteIndex, not the base-only lookup: a theme's own flag.tga must
    // resolve to its own sprite (see asset_manager.h, "THE TRAP THIS EXISTS FOR").
    const AssetManager& assets = AssetManager::getInstance();
    m_circleExclamation = assets.getIconSpriteIndex("circle-exclamation");
    m_flag = assets.getIconSpriteIndex("flag");
    m_flagCheckered = assets.getIconSpriteIndex("flag-checkered");
    m_initialized = true;
}

RiderFlagIcons::Icon RiderFlagIcons::get(Kind kind, unsigned long wrongWayColor) {
    ensureInitialized();
    Icon icon;
    switch (kind) {
    case Kind::None:     break;
    case Kind::WrongWay: icon.sprite = m_circleExclamation; icon.color = wrongWayColor;          break;
    case Kind::Hazard:   icon.sprite = m_flag;              icon.color = FlagColors::HAZARD;     break;
    case Kind::Blue:     icon.sprite = m_flag;              icon.color = FlagColors::BLUE;       break;
    case Kind::LastLap:  icon.sprite = m_flag;              icon.color = FlagColors::LAST_LAP;   break;
    case Kind::Finished: icon.sprite = m_flagCheckered;     icon.color = FlagColors::CHECKERED;  break;
    }
    if (icon.sprite < 0) icon.sprite = 0;
    return icon;
}
