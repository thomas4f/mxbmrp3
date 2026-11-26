// ============================================================================
// core/hud_manager_groups.cpp
// THE MASTER SWITCHES (HudManager::MasterGroup): which HUDs each covers and the
// one table every reader goes through -- the hide pass (isHeldBack), the
// hotkeys, the sidebar checkbox, the saved key and the analytics flag. A new
// family is one enum value, one row here and its inGroup() branch.
//
// Pinned by tests/integration/tests/hidden_input_test.cpp and
// toggle_strip_test.cpp (a hidden family), analytics_wiring_test.cpp (the
// flags) and deck_hud_test.cpp (the Decks switch stops the history).
// ============================================================================
#include "hud_manager.h"

#include "hotkey_config.h"
#include "../hud/deck_hud.h"
#include "../hud/settings_hud.h"
#include "../hud/bars_widget.h"
#include "../hud/clock_widget.h"
#include "../hud/compass_widget.h"
#include "../hud/crash_widget.h"
#include "../hud/fuel_widget.h"
#include "../hud/gamepad_widget.h"
#include "../hud/gear_widget.h"
#include "../hud/gforce_widget.h"
#include "../hud/lap_widget.h"
#include "../hud/lean_widget.h"
#include "../hud/now_playing_widget.h"
#include "../hud/position_widget.h"
#include "../hud/prestige_widget.h"
#include "../hud/rpm_widget.h"
#include "../hud/slide_widget.h"
#include "../hud/speed_widget.h"
#include "../hud/speedo_widget.h"
#include "../hud/stance_widget.h"
#include "../hud/tacho_widget.h"
#include "../hud/time_widget.h"
#include "../hud/wheelie_widget.h"

const HudManager::MasterGroupInfo& HudManager::masterGroupInfo(MasterGroup group) {
    static const MasterGroupInfo kGroups[MASTER_GROUP_COUNT] = {
        {HotkeyAction::TOGGLE_WIDGETS, SettingsHud::TAB_WIDGETS, "widgetsEnabled", "feat_widgets"},
        {HotkeyAction::TOGGLE_DECKS,   SettingsHud::TAB_DECKS,  "decksEnabled",   "feat_decks"},
    };
    return kGroups[static_cast<int>(group)];
}

HudManager::MasterGroup HudManager::groupForTab(int tab) {
    for (int g = 0; g < MASTER_GROUP_COUNT; ++g) {
        if (masterGroupInfo(static_cast<MasterGroup>(g)).tab == tab) return static_cast<MasterGroup>(g);
    }
    return MasterGroup::Count;
}

// EVERY row of the Widgets tab, which is what the master toggle and its hotkey
// mean by "the widgets". The Crash and Prestige widgets were missing and so sat
// through a toggle that took every other one off screen - a hand-kept list, and
// the two newest names are exactly what a hand-kept list loses.
bool HudManager::inGroup(MasterGroup group, const BaseHud* hud) const {
    if (!hud) return false;
    if (group == MasterGroup::Decks) {
        for (const DeckHud* deck : m_pDecks) {
            if (hud == deck) return true;
        }
        return false;
    }
    return hud == m_pLap || hud == m_pPosition || hud == m_pTime ||
           hud == m_pSpeed || hud == m_pGear || hud == m_pSpeedo || hud == m_pTacho ||
           hud == m_pBars || hud == m_pFuel ||
           hud == m_pGamepad || hud == m_pLean || hud == m_pSlide || hud == m_pWheelie || hud == m_pStance ||
           hud == m_pGforce || hud == m_pCompass ||
           hud == m_pClock || hud == m_pCrash || hud == m_pPrestige || hud == m_pRpm ||
           hud == m_pNowPlaying;
}

bool HudManager::heldByGroup(const BaseHud* hud) const {
    for (int g = 0; g < MASTER_GROUP_COUNT; ++g) {
        if (m_groupOff[g] && inGroup(static_cast<MasterGroup>(g), hud)) return true;
    }
    return false;
}
