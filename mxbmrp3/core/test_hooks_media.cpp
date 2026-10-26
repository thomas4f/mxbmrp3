// ============================================================================
// core/test_hooks_media.cpp
// The MXBMRP3_Test_* exports for Now Playing (now_playing_test): the test build
// has no WinRT, so MediaManager never polls a player. These publish one as its
// worker would, and read back what the widget's and the page's controls queued.
//
// Same rules as core/test_hooks.cpp: the whole file is gated on
// MXBMRP3_TEST_BUILD and mxbmrp3/CMakeLists.txt removes it from every shipping
// target's source list, so these exports cannot exist in a released DLL.
// ============================================================================
#include "../game/game_config.h"

#if defined(MXBMRP3_TEST_BUILD)

#include <cstdio>

#include "media_manager.h"

extern "C" {

// A player's snapshot (MediaManager::State as an int), published as the worker
// publishes one: it bumps the revision the widget, the Dash and the notice follow.
__declspec(dllexport) void MXBMRP3_Test_MediaPublish(const char* title, const char* artist, int state,
                                                     int positionMs, int durationMs, int volume) {
    MediaManager::Snapshot next;
    next.state = static_cast<MediaManager::State>(state);
    snprintf(next.title, sizeof(next.title), "%s", title ? title : "");
    snprintf(next.artist, sizeof(next.artist), "%s", artist ? artist : "");
    next.positionMs = positionMs;
    next.durationMs = durationMs;
    next.volume = volume;
    MediaManager::getInstance().publishForTest(next);
}

// What the controls queued for the worker: 0 = the transport command
// (MediaManager::Command as an int), 1 = the volume (-1 = none).
__declspec(dllexport) int MXBMRP3_Test_MediaPending(int which) {
    const MediaManager& media = MediaManager::getInstance();
    return which == 0 ? media.pendingCommandForTest() : media.pendingVolumeForTest();
}

}  // extern "C"

#endif  // MXBMRP3_TEST_BUILD
