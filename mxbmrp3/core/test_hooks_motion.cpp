// ============================================================================
// core/test_hooks_motion.cpp
// The MXBMRP3_Test_* exports for Motion (motion_test): set the level, drive
// its clock, and fingerprint the game frame - every drawn field of every quad
// and string handed over, so "Off is the frame it always was" is checked on
// what is drawn rather than by counting.
//
// Same rules as core/test_hooks.cpp: the whole file is gated on
// MXBMRP3_TEST_BUILD and mxbmrp3/CMakeLists.txt removes it from every shipping
// target's source list, so these exports cannot exist in a released DLL.
// ============================================================================
#include "../game/game_config.h"

#if defined(MXBMRP3_TEST_BUILD)

#include "hud_manager.h"
#include "ui_config.h"
#include "../hud/map_hud.h"

#include <cstdint>
#include <cstring>

namespace {
uint64_t fnv1a(uint64_t h, const void* data, size_t n) {
    const unsigned char* p = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ull; }
    return h;
}
}  // namespace

extern "C" {

// Motion::Level: 0 Off, 1 Subtle, 2 Normal.
__declspec(dllexport) void MXBMRP3_Test_SetMotion(int level) {
    UiConfig::getInstance().setMotion(static_cast<Motion::Level>(level));
}

// Motion's clock in microseconds; -1 = the real clock.
__declspec(dllexport) void MXBMRP3_Test_SetMotionNowUs(long long us) {
    HudManager::testSetMotionNowUs(us);
}

// FNV-1a over every drawn field of the last-collected game frame: each quad's
// corners, sprite and colour, each string's text (to its terminator - the bytes
// after it are whatever the buffer held), position, font, size, justification
// and colour. Fields, not raw structs, so padding cannot make equal frames differ.
__declspec(dllexport) unsigned long long MXBMRP3_Test_GameFrameHash() {
    const HudManager& hm = HudManager::getInstance();
    uint64_t h = 1469598103934665603ull;
    for (const SPluginQuad_t& q : hm.getGameQuads()) {
        h = fnv1a(h, q.m_aafPos, sizeof(q.m_aafPos));
        h = fnv1a(h, &q.m_iSprite, sizeof(q.m_iSprite));
        h = fnv1a(h, &q.m_ulColor, sizeof(q.m_ulColor));
    }
    h = fnv1a(h, "|", 1);
    for (const SPluginString_t& s : hm.getGameStrings()) {
        h = fnv1a(h, s.m_szString, strnlen(s.m_szString, sizeof(s.m_szString)));
        h = fnv1a(h, s.m_afPos, sizeof(s.m_afPos));
        h = fnv1a(h, &s.m_iFont, sizeof(s.m_iFont));
        h = fnv1a(h, &s.m_fSize, sizeof(s.m_fSize));
        h = fnv1a(h, &s.m_iJustify, sizeof(s.m_iJustify));
        h = fnv1a(h, &s.m_ulColor, sizeof(s.m_ulColor));
    }
    return h;
}

// A HUD's on/off on BOTH surfaces, by harness id: companion_demo's "motionhud"
// shows a HUD coming in on the companion window it screenshots. 1 if found.
__declspec(dllexport) int MXBMRP3_Test_SetHudVisibleBoth(const char* name, int visible) {
    for (const auto& hud : HudManager::getInstance().getHuds()) {
        if (hud && name && std::strcmp(hud->getHarnessId(), name) == 0) {
            hud->setVisible(visible != 0);
            hud->setCompanionVisible(visible != 0);
            return 1;
        }
    }
    return 0;
}

// The Map's outline (its Motion under layer) against the fill quad drawn over
// it, as highest alpha in the last game frame. 0 if the map drew no outline.
__declspec(dllexport) int MXBMRP3_Test_MapMotionLayers(int* under, int* over) {
    HudManager& hm = HudManager::getInstance();
    int u = 0, o = 0;
    if (!hm.testMotionUnderAlpha(&hm.getMapHud(), u, o)) return 0;
    if (under) *under = u;
    if (over) *over = o;
    return 1;
}

}  // extern "C"

#endif  // MXBMRP3_TEST_BUILD
