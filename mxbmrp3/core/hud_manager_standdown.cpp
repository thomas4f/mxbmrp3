// ============================================================================
// core/hud_manager_standdown.cpp
// WHICH HUD STANDS DOWN for another one showing the same thing: the settings
// menu behind the Direct GL prompt, and the toast card while the Notice deck
// carries the toasts (one toast, one place). What each means is at its
// declaration in hud_manager.h; the frame and the input pass both ask here.
//
// Pinned by tests/integration/tests/deck_hud_test.cpp (the Notice deck
// cases) and gl_render_test.cpp (the prompt holding the menu back).
// ============================================================================
#include "hud_manager.h"

#include "../hud/achievement_widget.h"
#include "../hud/deck_hud.h"
#include "../hud/gl_confirm_hud.h"
#include "../hud/settings_hud.h"

bool HudManager::standsDown(const BaseHud* hud) const {
    if (!hud) return false;
    if (hud == m_pAchievement) return noticeDeckCarriesToasts();
    return m_pGlConfirm && m_pGlConfirm->isActive() && hud == m_pSettingsHud;
}

bool HudManager::noticeDeckCarriesToasts() const {
    const DeckHud* deck = m_pDecks[static_cast<int>(DeckHud::Kind::Notice)];
    return deck && isGroupEnabled(MasterGroup::Decks) && deck->isVisibleAnySurface();
}
