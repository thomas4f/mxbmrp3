// ============================================================================
// core/test_hooks_messages.cpp
// The MXBMRP3_Test_* exports for SystemMessages (core/system_messages.h): the
// toast ledger and the card showing one, a hotkey fired without a keyboard,
// the startup-popup bookkeeping, and the Version widget's popup and countdown.
//
// Same rules as core/test_hooks.cpp: the whole file is gated on
// MXBMRP3_TEST_BUILD and mxbmrp3/CMakeLists.txt removes it from every shipping
// target's source list, so these exports cannot exist in a released DLL.
// ============================================================================
#include "../game/game_config.h"

#if defined(MXBMRP3_TEST_BUILD)

#include "hotkey_config.h"
#include "hotkey_manager.h"
#include "hud_manager.h"
#include "system_messages.h"
#include "../hud/achievement_widget.h"
#include "../hud/settings_hud.h"
#include "../hud/version_widget.h"

#include <cstdio>
#include <cstring>

extern "C" {

// Fire a hotkey on the next draw, by its INI config name ("standings",
// "all_elements", "all_widgets"). 0 = no such action.
__declspec(dllexport) int MXBMRP3_Test_HotkeyInject(const char* name) {
    if (!name) return 0;
    for (int i = 0; i < static_cast<int>(HotkeyAction::COUNT); ++i) {
        const HotkeyAction a = static_cast<HotkeyAction>(i);
        if (std::strcmp(getActionConfigName(a), name) == 0) {
            HotkeyManager::getInstance().testInject(a);
            return 1;
        }
    }
    return 0;
}

// The settings menu's keyboard binding: a VK code (0 = none) plus ModifierFlags
// bits. Redraws every HUD so the Version widget's welcome names the new key.
__declspec(dllexport) void MXBMRP3_Test_SetSettingsKey(int vk, int mods) {
    HotkeyManager& hk = HotkeyManager::getInstance();
    if (vk == 0) hk.clearKeyboardBinding(HotkeyAction::TOGGLE_SETTINGS);
    else hk.setKeyboardBinding(HotkeyAction::TOGGLE_SETTINGS,
                               KeyBinding(static_cast<uint8_t>(vk), static_cast<ModifierFlags>(mods)));
    HudManager::getInstance().markAllHudsDirty();
}

// Every toast posted this run (shown or not), and the last one as
// "title|detail|icon|tab|severity" (severity 0 info, 1 warning).
__declspec(dllexport) unsigned int MXBMRP3_Test_MessagesPosted() {
    return SystemMessages::getInstance().posted();
}
__declspec(dllexport) void MXBMRP3_Test_MessageLast(char* out, int cap) {
    if (!out || cap <= 0) return;
    const SystemMessages::Toast& t = SystemMessages::getInstance().lastPosted();
    std::snprintf(out, static_cast<size_t>(cap), "%s|%s|%s|%d|%d", t.title, t.detail, t.icon,
                  t.tab, t.severity == SystemMessages::Severity::Warning ? 1 : 0);
}
// 1 while the card shows a system toast (after a draw()); its title into `out`.
__declspec(dllexport) int MXBMRP3_Test_MessageShowing(char* out, int cap) {
    if (out && cap > 0) out[0] = '\0';
    const AchievementWidget* w = HudManager::getInstance().getAchievementWidget();
    if (!w || !w->isShowingSystem()) return 0;
    if (out && cap > 0) std::snprintf(out, static_cast<size_t>(cap), "%s", w->testSystemToast().title);
    return 1;
}

// The startup-popup decision, re-run with chosen inputs (the real one ran at
// startup against the test's save dir). Resets the queue and stored state first.
__declspec(dllexport) void MXBMRP3_Test_MessagesStartup(int freshInstall, const char* line,
                                                        const char* storedLine) {
    SystemMessages& m = SystemMessages::getInstance();
    m.testReset();
    if (storedLine) m.setStoredLine(storedLine);
    if (!freshInstall) m.setWelcomeState(1);
    m.onStartup(freshInstall != 0, line);
}

// Answer every startup popup still owed, as a returning player would have:
// the harness does this after each startup (PluginHost::startup).
__declspec(dllexport) void MXBMRP3_Test_MessagesSettle() {
    SystemMessages& m = SystemMessages::getInstance();
    m.resolvePopup(SystemMessages::Popup::Updated);
    m.resolvePopup(SystemMessages::Popup::Welcome);
}

// The Version widget's popup: its kind (VersionWidget::popupKind) and the
// countdown second showing into *countdown.
__declspec(dllexport) int MXBMRP3_Test_VersionPopup(int* countdown) {
    const VersionWidget& v = HudManager::getInstance().getVersionWidget();
    if (countdown) *countdown = v.testCountdown();
    return v.popupKind();
}
// A HUD the frame holds back (HudManager::isHeldBack), by harness id: 1 held
// back, 0 drawn by its own visibility, -1 no such HUD.
__declspec(dllexport) int MXBMRP3_Test_HudHeldBack(const char* name) {
    HudManager& hm = HudManager::getInstance();
    for (const auto& hud : hm.getHuds()) {
        if (hud && name && std::strcmp(hud->getHarnessId(), name) == 0)
            return hm.isHeldBack(hud.get()) ? 1 : 0;
    }
    return -1;
}
__declspec(dllexport) void MXBMRP3_Test_VersionPopupAdvance(int ms) {
    HudManager::getInstance().getVersionWidget().testAdvancePopup(static_cast<float>(ms));
}
__declspec(dllexport) void MXBMRP3_Test_VersionPopupDismiss() {
    HudManager::getInstance().getVersionWidget().testDismiss();
}

// One click on the active tab's Reset: arms "Confirm?" and resets nothing.
// (MXBMRP3_Test_SettingsClickResetTab clicks twice, the whole reset.)
__declspec(dllexport) int MXBMRP3_Test_SettingsClickResetTabOnce() {
    return HudManager::getInstance().getSettingsHud().testClickResetTab(1) ? 1 : 0;
}

}  // extern "C"

#endif  // MXBMRP3_TEST_BUILD
