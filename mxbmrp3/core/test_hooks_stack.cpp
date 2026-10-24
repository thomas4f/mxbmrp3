// ============================================================================
// core/test_hooks_stack.cpp
// The MXBMRP3_Test_* exports for the HUD stacking order
// (core/hud_manager_stack.cpp). Pinned by tests/integration/tests/hud_stack_test.cpp.
//
// Same rules as core/test_hooks.cpp: gated on MXBMRP3_TEST_BUILD, and
// mxbmrp3/CMakeLists.txt removes this file from every shipping target.
// ============================================================================
#include "../game/game_config.h"

#if defined(MXBMRP3_TEST_BUILD)

#include <cstring>

#include "hud_manager.h"

extern "C" {

// Where a HUD (registration name) sat in last frame's draw order, 0 = bottom;
// -1 = no such HUD or no order built yet.
__declspec(dllexport) int MXBMRP3_Test_HudDrawPosition(const char* name) {
    if (!name) return -1;
    const HudManager& hm = HudManager::getInstance();
    const auto& huds = hm.getHuds();
    const auto& order = hm.drawOrder();
    for (size_t k = 0; k < order.size(); ++k) {
        const BaseHud* hud = order[k] < huds.size() ? huds[order[k]].get() : nullptr;
        if (hud && std::strcmp(hud->getHarnessId(), name) == 0) return static_cast<int>(k);
    }
    return -1;
}

// The highest layer a HUD landed in on the companion window's last frame
// (core/render_layers.h); -1 = drew none there.
__declspec(dllexport) int MXBMRP3_Test_CompanionFrameHudLayer(const char* name) {
    if (!name) return -1;
    const HudManager& hm = HudManager::getInstance();
    for (const auto& hud : hm.getHuds())
        if (hud && std::strcmp(hud->getHarnessId(), name) == 0) return hm.frameHudLayer(hud.get(), true);
    return -1;
}

// A HUD's saved stacking rank (BaseHud::getStackRank); -1 = no such HUD.
__declspec(dllexport) int MXBMRP3_Test_HudStackRank(const char* name) {
    if (!name) return -1;
    for (const auto& hud : HudManager::getInstance().getHuds())
        if (hud && std::strcmp(hud->getHarnessId(), name) == 0) return hud->getStackRank();
    return -1;
}

}  // extern "C"

#endif  // MXBMRP3_TEST_BUILD
