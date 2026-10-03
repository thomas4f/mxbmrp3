// ============================================================================
// core/test_hooks_stats.cpp
// The MXBMRP3_Test_* exports for StatsManager: the injected clock the odometer
// tests drive, the live odometer state a save cannot show, and a forced save.
//
// SPLIT OUT OF core/test_hooks.cpp when it hit its stated budget again, which
// is what that file's budget line asks for rather than a bigger number. The
// family was already scattered before this: MXBMRP3_Test_StatsSave had been
// moved to test_hooks_achievements.cpp while its comment stayed behind in
// test_hooks.cpp, describing a function that was no longer under it. Both are
// reunited here.
//
// Same rules as its parent: the whole file is gated on MXBMRP3_TEST_BUILD and
// mxbmrp3/CMakeLists.txt removes it from every shipping target's source list,
// so these exports cannot exist in a released DLL.
// ============================================================================
#include "../game/game_config.h"

#if defined(MXBMRP3_TEST_BUILD)

#include "stats_manager.h"

extern "C" {

// --- Stats odometer seam. Distance integrates speed over the WALL-CLOCK gap
// between telemetry calls, so the odometer test injects the clock (µs; -1
// restores the real one) to make each tick's dt — and the expected distance —
// exact. ---
__declspec(dllexport) void MXBMRP3_Test_StatsSetNowUs(long long us) {
    StatsManager::testSetNowUs(us);
}

// Read the live odometer state: the current bike's odometer + the session trip
// (both meters), plus the ~100m dirty-coalescing internals (distance accumulated
// since the last dirty mark, and the dirty flag itself) — not all observable
// through the stats file, which is written only off track. Any out-pointer may
// be null.
__declspec(dllexport) void MXBMRP3_Test_StatsOdometerState(double* bikeOdometer,
        double* sessionTrip, double* unsavedDistance, int* dirty) {
    const StatsManager& sm = StatsManager::getInstance();
    if (bikeOdometer)    *bikeOdometer    = sm.getOdometerForCurrentBike();
    if (sessionTrip)     *sessionTrip     = sm.getSessionTripDistance();
    if (unsavedDistance) *unsavedDistance = sm.testUnsavedDistance();
    if (dirty)           *dirty           = sm.testIsDirty() ? 1 : 0;
}

// Force a stats save (the same save() the RunStop/RunDeinit leave-track flush
// calls; a no-op when clean). Lets a test establish a known-clean baseline
// before asserting the dirty-coalescing behaviour.
__declspec(dllexport) void MXBMRP3_Test_StatsSave() {
    StatsManager::getInstance().save();
}

}  // extern "C"

#endif  // MXBMRP3_TEST_BUILD
