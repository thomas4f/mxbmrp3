// ============================================================================
// core/test_hooks_updater.cpp
// The MXBMRP3_Test_* export that records what the Updates tab's step rows
// showed during an install (updater_test), split out of core/test_hooks.cpp,
// which is at its file budget.
//
// Same rules as its parent: the whole file is gated on MXBMRP3_TEST_BUILD and
// mxbmrp3/CMakeLists.txt removes it from every shipping target's source list,
// so these exports cannot exist in a released DLL.
// ============================================================================
#include "../game/game_config.h"

#if defined(MXBMRP3_TEST_BUILD)

#include "update_downloader.h"
#include <string>
#include <vector>

extern "C" {

// Run the install pipeline like MXBMRP3_Test_ExtractAndInstall and record, at
// every UI refresh it triggers, what step `step`'s row would show: its
// percentage while in progress (-1 = no percentage), 101 once it reads OK.
// Repeats are dropped. Writes up to `cap` samples to `out` and returns how many
// were recorded (-1 on a bad step); *installOk gets the install's result.
// debugMode runs it as the Updates tab's debug mode does (no verify, no backup).
__declspec(dllexport) int MXBMRP3_Test_UpdaterStepTrace(const char* destDir,
        const char* zipData, int zipLen, int step, int* out, int cap, int* installOk,
        int debugMode) {
    using Dl = UpdateDownloader;
    if (step < 0 || step >= static_cast<int>(Dl::Step::STEP_COUNT)) return -1;
    Dl& dl = Dl::getInstance();
    dl.reset();
    const bool wasDebug = dl.isDebugMode();

    std::vector<int> trace;
    dl.setStateChangeCallback([&dl, &trace, step]() {
        const Dl::StepInfo s = dl.getSteps()[static_cast<size_t>(step)];
        int v;
        if (s.status == Dl::StepStatus::COMPLETE) v = 101;
        else if (s.status == Dl::StepStatus::IN_PROGRESS) v = s.percent;
        else return;
        if (trace.empty() || trace.back() != v) trace.push_back(v);
    });

    std::string err;
    std::vector<char> bytes;
    if (zipData && zipLen > 0) bytes.assign(zipData, zipData + zipLen);
    const bool ok = dl.testExtractAndInstall(destDir ? destDir : "", bytes, err, debugMode != 0);
    dl.setStateChangeCallback(nullptr);
    dl.setDebugMode(wasDebug);

    if (installOk) *installOk = ok ? 1 : 0;
    for (size_t i = 0; i < trace.size() && static_cast<int>(i) < cap; ++i) out[i] = trace[i];
    return static_cast<int>(trace.size());
}

}  // extern "C"

#endif  // MXBMRP3_TEST_BUILD
