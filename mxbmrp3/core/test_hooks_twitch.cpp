// ============================================================================
// core/test_hooks_twitch.cpp
// The MXBMRP3_Test_* exports for the Twitch chat: feed raw IRC lines through
// the manager's own parse-and-queue path (no network), and read back what the
// chat HUD would draw, row by row, so wrap, filters, colours and moderation are
// assertable headless. Pinned by tests/integration/tests/twitch_chat_test.cpp.
//
// Same rules as core/test_hooks.cpp: gated on MXBMRP3_TEST_BUILD, and
// mxbmrp3/CMakeLists.txt removes this file from every shipping target.
// ============================================================================
#include "../game/game_config.h"

#if defined(MXBMRP3_TEST_BUILD)

#include "asset_manager.h"
#include "hud_manager.h"
#include "twitch_chat_manager.h"
#include "../hud/stream_chat_hud.h"

#include <cstdio>
#include <string>

extern "C" {

// One raw IRC line ("@tags :prefix PRIVMSG #chan :text"), as the socket would
// deliver it. Queued like a received line; the HUD drains it on its next update.
__declspec(dllexport) void MXBMRP3_Test_TwitchInjectLine(const char* raw) {
    if (raw) TwitchChatManager::getInstance().testInjectLine(raw);
}

// The same, stamped with the PREVIOUS generation: a line read just before a
// channel switch or disconnect, which must be dropped.
__declspec(dllexport) void MXBMRP3_Test_TwitchInjectStaleLine(const char* raw) {
    if (raw) TwitchChatManager::getInstance().testInjectLine(raw, /*stale=*/true);
}

// The channel as stored (normalized), into a caller buffer.
__declspec(dllexport) void MXBMRP3_Test_TwitchChannel(char* out, int cap) {
    if (!out || cap <= 0) return;
    snprintf(out, static_cast<size_t>(cap), "%s", TwitchChatManager::getInstance().getChannel().c_str());
}

// Messages the HUD holds (filtered ones included), and rows it last rendered.
__declspec(dllexport) int MXBMRP3_Test_ChatEntryCount() {
    return HudManager::getInstance().getStreamChatHud().testEntryCount();
}
__declspec(dllexport) int MXBMRP3_Test_ChatRowCount() {
    return HudManager::getInstance().getStreamChatHud().testRenderedRowCount();
}

// Rendered row `i` (0 = top) as "name|text" ("|text" for a wrapped continuation,
// "~|text" for the connection status line).
__declspec(dllexport) void MXBMRP3_Test_ChatRowText(int i, char* out, int cap) {
    if (!out || cap <= 0) return;
    const std::string row = HudManager::getInstance().getStreamChatHud().testRowText(i);
    snprintf(out, static_cast<size_t>(cap), "%s", row.c_str());
}

// Resolved (readability-lifted) name colour of held message `i`, game ABGR.
__declspec(dllexport) unsigned long MXBMRP3_Test_ChatNameColor(int i) {
    return HudManager::getInstance().getStreamChatHud().testNameColor(i);
}

// Force the reported status (-1 = real), to draw the panel's status line offline.
__declspec(dllexport) void MXBMRP3_Test_TwitchForceStatus(int status) {
    TwitchChatManager::getInstance().testForceStatus(status);
}

// Switch the chat on for "testchan" and report it connected, without a socket
// (a forced status keeps the worker from starting): the state the chat
// renders in, for cases about the chat itself rather than the connection.
__declspec(dllexport) void MXBMRP3_Test_TwitchSimulateConnected() {
    TwitchChatManager& mgr = TwitchChatManager::getInstance();
    mgr.testForceStatus(static_cast<int>(TwitchChatManager::Status::CONNECTED));
    mgr.setChannel("testchan");
    mgr.setEnabled(true);
}

// Texture sprite a Twitch role's badge resolves to (0 = none). `role` is one
// Chat::Role bit.
__declspec(dllexport) int MXBMRP3_Test_TwitchBadgeSprite(int role) {
    return HudManager::getInstance().getStreamChatHud().testBadgeSprite(Chat::Platform::Twitch,
                                                                        static_cast<uint16_t>(role));
}

// Sprite AssetManager gives a texture by base name (variant 1): the side of the
// user-badge check that does not go through the HUD.
__declspec(dllexport) int MXBMRP3_Test_TextureSprite(const char* baseName) {
    return baseName ? AssetManager::getInstance().getSpriteIndex(baseName, 1) : 0;
}

// Worker status (TwitchChatManager::Status) -- OFF unless a session is wanted.
__declspec(dllexport) int MXBMRP3_Test_TwitchStatus() {
    return static_cast<int>(TwitchChatManager::getInstance().getStatus());
}

}  // extern "C"

#endif  // MXBMRP3_TEST_BUILD
