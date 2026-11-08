// ============================================================================
// core/test_hooks_title_art.cpp
// The MXBMRP3_Test_* exports for a theme's optional title_* art set: inject it
// into the synthetic theme, and read back which sprite a HUD quad draws, so
// title_band_test can tell the band's art from the card's.
//
// Same rules as core/test_hooks.cpp: the whole file is gated on
// MXBMRP3_TEST_BUILD and mxbmrp3/CMakeLists.txt removes it from every shipping
// target's source list, so these exports cannot exist in a released DLL.
// ============================================================================
#include "../game/game_config.h"

#if defined(MXBMRP3_TEST_BUILD)

#include "asset_manager.h"
#include "hud_manager.h"
#include "../hud/base_hud.h"

#include <cstring>

namespace {
// The same registration-name lookup as test_hooks.cpp's (static there).
const BaseHud* hudByName(const char* name) {
    if (!name) return nullptr;
    for (const auto& hud : HudManager::getInstance().getHuds()) {
        if (hud && std::strcmp(hud->getHarnessId(), name) == 0) {
            return static_cast<const BaseHud*>(hud.get());
        }
    }
    return nullptr;
}
}  // namespace

extern "C" {

__declspec(dllexport) int MXBMRP3_Test_SetThemeTitleArt(int sprite) {
    if (!AssetManager::getInstance().testSetThemeTitleArt(sprite)) return 0;
    HudManager::getInstance().markAllHudsDirty();
    return 1;
}

// The sprite index of one of a HUD's quads (0 = untextured), -1 when there is no
// such HUD or quad.
__declspec(dllexport) int MXBMRP3_Test_HudQuadSprite(const char* name, int index) {
    const BaseHud* hud = hudByName(name);
    if (!hud) return -1;
    const auto& quads = hud->getQuads();
    if (index < 0 || index >= static_cast<int>(quads.size())) return -1;
    return quads[static_cast<size_t>(index)].m_iSprite;
}

}  // extern "C"

#endif  // MXBMRP3_TEST_BUILD
