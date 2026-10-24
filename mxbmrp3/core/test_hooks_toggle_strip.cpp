// ============================================================================
// core/test_hooks_toggle_strip.cpp
// The MXBMRP3_Test_* exports for the Toggles widget: where each row is drawn
// and which tab it switches, so toggle_strip_test can click it; its reveal, so a
// case can end one started by a Draw gap rather than race its 4 s; the cursor's
// idle timeout, which hovering the strip holds off; the director's on/off,
// which its Director switch flips; and the hover sample.
//
// Same rules as core/test_hooks.cpp: the whole file is gated on
// MXBMRP3_TEST_BUILD and mxbmrp3/CMakeLists.txt removes it from every shipping
// target's source list, so these exports cannot exist in a released DLL.
// ============================================================================
#include "../game/game_config.h"

#if defined(MXBMRP3_TEST_BUILD)

#include "hud_manager.h"
#include "../hud/toggle_strip_widget.h"
#include "../hud/helmet_overlay_hud.h"
#include "companion_window.h"
#include "steam_friends_manager.h"
#include "xinput_reader.h"
#include "director_manager.h"
#include "input_manager.h"
#include <cstdio>
#include <cstring>

extern "C" {

// Row i's centre (screen units), switch (1 on) and tab name; returns the rows drawn, which is 0
// while the strip is hidden with the cursor.
__declspec(dllexport) int MXBMRP3_Test_ToggleStripRow(int i, float* x, float* y, int* on,
                                                      char* name, int cap) {
    const char* n = nullptr;
    bool lit = false;
    const int rows = HudManager::getInstance().getToggleStripWidget().testRow(i, x, y, &lit, &n);
    if (on) *on = lit ? 1 : 0;
    if (name && cap > 0) snprintf(name, static_cast<size_t>(cap), "%s", n ? n : "");
    return rows;
}

__declspec(dllexport) int MXBMRP3_Test_ToggleStripHoverRow() {
    return HudManager::getInstance().getToggleStripWidget().testHoverRow();
}

// The strip's switch on each surface, and whether its companion copy is configured
// (diverged) rather than mirroring the game's.
__declspec(dllexport) void MXBMRP3_Test_ToggleStripSurfaces(int* game, int* companion, int* configured) {
    const ToggleStripWidget& w = HudManager::getInstance().getToggleStripWidget();
    if (game)       *game       = w.isVisible() ? 1 : 0;
    if (companion)  *companion  = w.getCompanionVisible() ? 1 : 0;
    if (configured) *configured = w.isCompanionConfigured() ? 1 : 0;
}

// Reveal the strip's controls for ms (0 ends a reveal), as entering the track does.
__declspec(dllexport) void MXBMRP3_Test_ToggleStripReveal(int ms) {
    HudManager::getInstance().getToggleStripWidget().reveal(ms);
}

// Age the cursor past its idle timeout (3 s), so a case need not wait it out.
__declspec(dllexport) void MXBMRP3_Test_CursorIdle() {
    InputManager::getInstance().testCursorIdle();
}

// The hover sample: 1 if the HUD with harness id `name` is drawn as one (its
// icon hovered while it is off), 0 if not, -1 if no such HUD.
__declspec(dllexport) int MXBMRP3_Test_HudSampled(const char* name) {
    for (const auto& hud : HudManager::getInstance().getHuds()) {
        if (hud && name && std::strcmp(hud->getHarnessId(), name) == 0) return hud->isSampled() ? 1 : 0;
    }
    return -1;
}

// The alpha of every quad in the last game frame, summed: a hover sample's
// share of it is half its shown one (HudManager::markSample).
__declspec(dllexport) double MXBMRP3_Test_GameFrameAlphaSum() {
    double sum = 0.0;
    for (const auto& q : HudManager::getInstance().getGameQuads()) sum += static_cast<double>((q.m_ulColor >> 24) & 0xFF);
    return sum;
}

__declspec(dllexport) int MXBMRP3_Test_CompanionCaptureRgba(int w, int h, unsigned char* out) {
    return CompanionWindow::getInstance().testRenderRgba(w, h, out) ? 1 : 0;
}

// Demo reels: one staged Steam friend (Steam data is not on a tape).
__declspec(dllexport) void MXBMRP3_Test_FriendsInject(const char* name, const char* server, const char* track,
                                                       const char* session, const char* state, const char* progress,
                                                       int sameServer) {
    SteamFriend f;
    f.name = name; f.server = server; f.track = track; f.session = session; f.state = state;
    f.progress = progress; f.hasData = true; f.sameServer = sameServer == 1;
    f.status = *track ? std::string(track) + " - " + session : SteamFriendsManager::LABEL_IN_MENUS;
    SteamFriendsManager::getInstance().testInjectFriend(f, sameServer == 2);   // 2 = our own row
}

// Demo reels: a connected pad with these sticks and triggers and no buttons held
// (a tape carries the rider's inputs, not the controller). Unlike FakeGamepad this
// leaves the widget's visibility alone: the reel switches it on from Settings.
__declspec(dllexport) void MXBMRP3_Test_FakeGamepadInput(float lx, float ly, float lt, float rt) {
    XInputData d;
    d.isConnected = true;
    d.leftStickX = lx; d.leftStickY = ly;
    d.leftTrigger = lt; d.rightTrigger = rt;
    XInputReader::getInstance().testForceData(d);
}

// Let the helmet draw on the companion window, which the demo reels record (the
// shipped helmet never does: BaseHud::rendersOnCompanion).
__declspec(dllexport) void MXBMRP3_Test_HelmetOnCompanion(int on) {
    HelmetOverlayHud::s_testOnCompanion = on != 0;
}

__declspec(dllexport) int MXBMRP3_Test_DirectorEnabled() {
    return DirectorManager::getInstance().isEnabled() ? 1 : 0;
}

}  // extern "C"

#endif  // MXBMRP3_TEST_BUILD
