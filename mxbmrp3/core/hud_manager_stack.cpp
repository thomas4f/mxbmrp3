// ============================================================================
// core/hud_manager_stack.cpp
// HUD STACKING ORDER: which HUD draws over which. Registration order is the
// default (HudManager::initialize); dragging a HUD puts it on top of every
// other one, and that order is saved per profile (Keys::Base::STACK).
//
// The ORDER applies on every surface and both render paths - the engine draws
// panels in it too. What only Direct GL and the companion window can add is
// the back HUD's TEXT going under the front panel (core/render_layers.h).
//
// The settings menu, its button and the pointer (registered last) and the
// Direct GL confirmation prompt draw above every other HUD and never move:
// nothing a user drags may cover them. A HUD that cannot be dragged
// (the helmet, the vignette) keeps its registration place, so full-screen
// effects stay under everything.
//
// Pinned by tests/integration/tests/hud_stack_test.cpp.
// ============================================================================
#include "hud_manager.h"

#include <algorithm>

#include "settings_manager.h"
#include "../hud/breakout_hud.h"
#include "../hud/gl_confirm_hud.h"
#include "../hud/pointer_widget.h"
#include "../hud/toggle_strip_widget.h"
#include "../hud/settings_hud.h"

namespace {
// Rank, then registration index: a strict total order, so std::sort gives one
// answer however a hand-edited INI ties two ranks - and, unlike stable_sort,
// never asks for a buffer in a per-frame call.
struct ByRank {
    const std::vector<std::unique_ptr<BaseHud>>& huds;
    bool operator()(size_t a, size_t b) const {
        const int ra = huds[a]->getStackRank(), rb = huds[b]->getStackRank();
        return ra != rb ? ra < rb : a < b;
    }
};
}  // namespace

bool HudManager::staysOnTop(const BaseHud* hud) const {
    return hud && (hud == m_pGlConfirm || hud == m_pSettingsHud || hud == m_pToggleStrip ||
                   hud == m_pBreakout || hud == m_pPointer);
}

void HudManager::raiseHud(BaseHud* hud) {
    if (!hud || staysOnTop(hud) || !hud->isDraggable()) return;
    int top = 0;
    for (const auto& h : m_huds)
        if (h && h.get() != hud) top = std::max(top, h->getStackRank());
    if (hud->getStackRank() > top) return;   // already on top
    hud->setStackRank(top + 1);

    // Renumber 1..k in the same order, so saved ranks stay small however many
    // drags a session sees. A handful of HUDs, once per drag.
    m_raised.clear();
    for (size_t i = 0; i < m_huds.size(); ++i)
        if (m_huds[i] && m_huds[i]->getStackRank() > 0) m_raised.push_back(i);
    std::sort(m_raised.begin(), m_raised.end(), ByRank{ m_huds });
    for (size_t k = 0; k < m_raised.size(); ++k)
        m_huds[m_raised[k]]->setStackRank(static_cast<int>(k) + 1);
    // A click-drag that ends where it began moves nothing, but the order changed.
    SettingsManager::getInstance().markDirty();
}

// Once per frame: never-dragged HUDs in registration order, then the dragged
// ones by rank, then the menu band. Allocation-free after the first frame; the
// sort only runs over the dragged HUDs, usually none.
void HudManager::buildDrawOrder() {
    m_drawOrder.clear();
    m_raised.clear();
    if (m_drawOrder.capacity() < m_huds.size()) m_drawOrder.reserve(m_huds.size());
    if (m_raised.capacity() < m_huds.size()) m_raised.reserve(m_huds.size());
    for (size_t i = 0; i < m_huds.size(); ++i) {
        const BaseHud* hud = m_huds[i].get();
        if (staysOnTop(hud)) continue;
        if (hud && hud->getStackRank() > 0) m_raised.push_back(i);
        else m_drawOrder.push_back(i);
    }
    if (!m_raised.empty()) {
        std::sort(m_raised.begin(), m_raised.end(), ByRank{ m_huds });
        m_drawOrder.insert(m_drawOrder.end(), m_raised.begin(), m_raised.end());
    }
    for (size_t i = 0; i < m_huds.size(); ++i)
        if (staysOnTop(m_huds[i].get())) m_drawOrder.push_back(i);
}
