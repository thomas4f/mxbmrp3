// ============================================================================
// core/test_hooks_finish.cpp
// The MXBMRP3_Test_* exports for the race finish (session_format_test).
//
// SPLIT OUT OF core/test_hooks.cpp, which is at its file budget.
// Same rules as its parent: the whole file is gated on MXBMRP3_TEST_BUILD and
// mxbmrp3/CMakeLists.txt removes it from every shipping target's source list,
// so these exports cannot exist in a released DLL.
// ============================================================================
#include "../game/game_config.h"

#if defined(MXBMRP3_TEST_BUILD)

#include "plugin_data.h"

extern "C" {

// The lap a time+laps leader must complete to finish (-1 = not armed), so a
// test sees the finish lap move without waiting a whole bonus lap for it.
__declspec(dllexport) int MXBMRP3_Test_FinishLap() {
    return PluginData::getInstance().getSessionData().finishLap;
}

// A rider's recorded race time in ms (-1 = not finished or unknown rider).
// No snapshot field carries it; the Dash, Timing HUD and spotter read it.
__declspec(dllexport) int MXBMRP3_Test_RiderFinishTime(int raceNum) {
    const StandingsData* s = PluginData::getInstance().getStanding(raceNum);
    return s ? s->finishTime : -1;
}

}  // extern "C"

#endif  // MXBMRP3_TEST_BUILD
