// ============================================================================
// core/test_hooks_youtube.cpp
// The MXBMRP3_Test_* exports for the YouTube half of the stream chat: feed a
// get_live_chat response body through YouTubeChatManager's own parse-and-queue
// path (no network), force its status, and read back each rendered row's
// platform and role icons, so a mixed Twitch + YouTube chat is assertable
// headless. The row TEXT comes from the Twitch hooks (MXBMRP3_Test_ChatRowText),
// which read the one chat HUD. Pinned by tests/integration/tests/youtube_chat_test.cpp.
//
// Same rules as core/test_hooks.cpp: gated on MXBMRP3_TEST_BUILD, and
// mxbmrp3/CMakeLists.txt removes this file from every shipping target.
// ============================================================================
#include "../game/game_config.h"

#if defined(MXBMRP3_TEST_BUILD)

#include "hud_manager.h"
#include "youtube_chat_manager.h"
#include "../hud/stream_chat_hud.h"

#include <cstdio>
#include <string>

extern "C" {

// One get_live_chat response body, as a poll would receive it. Returns the
// YouTubeChat::PollResult (0 ok, 1 ended, 2 unrecognized); the HUD drains the
// queued messages on its next update.
__declspec(dllexport) int MXBMRP3_Test_YouTubeInjectPoll(const char* body) {
    return body ? YouTubeChatManager::getInstance().testInjectPoll(body) : -1;
}

// Force the reported status (-1 = real). While forced, no worker is started.
__declspec(dllexport) void MXBMRP3_Test_YouTubeForceStatus(int status) {
    YouTubeChatManager::getInstance().testForceStatus(status);
}

// Switch YouTube on for "@testchan" and report it connected, without a request.
__declspec(dllexport) void MXBMRP3_Test_YouTubeSimulateConnected() {
    YouTubeChatManager& mgr = YouTubeChatManager::getInstance();
    mgr.testForceStatus(static_cast<int>(YouTubeChatManager::Status::CONNECTED));
    mgr.setChannel("@testchan");
    mgr.setEnabled(true);
}

// Set the channel as the Stream Chat tab would (normalized there). With a forced
// status no worker starts, so this only switches what the chat belongs to.
__declspec(dllexport) void MXBMRP3_Test_YouTubeSetChannel(const char* channel) {
    if (channel) YouTubeChatManager::getInstance().setChannel(channel);
}

// Status (YouTubeChatManager::Status) -- OFF unless a session is wanted.
__declspec(dllexport) int MXBMRP3_Test_YouTubeStatus() {
    return static_cast<int>(YouTubeChatManager::getInstance().getStatus());
}

// The channel as stored (normalized), into a caller buffer.
__declspec(dllexport) void MXBMRP3_Test_YouTubeChannel(char* out, int cap) {
    if (!out || cap <= 0) return;
    snprintf(out, static_cast<size_t>(cap), "%s", YouTubeChatManager::getInstance().getChannel().c_str());
}

// Sprite a role's badge resolves to on a platform (0 = none). `platform` is a
// Chat::Platform, `role` one Chat::Role bit.
__declspec(dllexport) int MXBMRP3_Test_ChatBadgeSprite(int platform, int role) {
    return HudManager::getInstance().getStreamChatHud().testBadgeSprite(
        static_cast<Chat::Platform>(platform), static_cast<uint16_t>(role));
}

// Sprite of a platform's per-line icon (0 = not registered).
__declspec(dllexport) int MXBMRP3_Test_ChatPlatformSprite(int platform) {
    return HudManager::getInstance().getStreamChatHud().testPlatformSprite(static_cast<Chat::Platform>(platform));
}

// Rendered row `i`'s platform and role icon sprites (0 = none drawn).
// Returns 0 past the last row.
__declspec(dllexport) int MXBMRP3_Test_ChatRowSprites(int i, int* platformSprite, int* roleSprite) {
    int p = 0, r = 0;
    if (!HudManager::getInstance().getStreamChatHud().testRowSprites(i, p, r)) return 0;
    if (platformSprite) *platformSprite = p;
    if (roleSprite) *roleSprite = r;
    return 1;
}

}  // extern "C"

#endif  // MXBMRP3_TEST_BUILD
