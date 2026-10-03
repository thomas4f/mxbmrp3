// ============================================================================
// core/test_hooks_disk.cpp
// The MXBMRP3_Test_* exports for the shared disk writer (AtomicFileWriter):
// a write through the real queue, the worker switch the harness flips, and a
// look at whether the worker is up.
//
// SPLIT OUT OF core/test_hooks.cpp, which is at its file budget; the writer's
// one hook moved here with the ones the worker added.
//
// Same rules as its parent: the whole file is gated on MXBMRP3_TEST_BUILD and
// mxbmrp3/CMakeLists.txt removes it from every shipping target's source list,
// so these exports cannot exist in a released DLL.
// ============================================================================
#include "../game/game_config.h"

#if defined(MXBMRP3_TEST_BUILD)

#include "atomic_file_writer.h"

extern "C" {

// A write through the real path (atomic_writer_test): submitted, then waited
// for, so the caller can read the file back. With the worker running that is
// the queue and the worker thread; without it, the inline write. 1 = it landed.
__declspec(dllexport) int MXBMRP3_Test_WriteFileAtomic(const char* path, const char* bytes) {
    if (!path || !bytes) return 0;
    const unsigned failuresBefore = AtomicFileWriter::failureCount();
    const bool ok = AtomicFileWriter::submit(path, bytes);
    AtomicFileWriter::flush();
    return (ok && AtomicFileWriter::failureCount() == failuresBefore) ? 1 : 0;
}

// Queue a write and return without waiting (the ordering and coalescing cases).
__declspec(dllexport) void MXBMRP3_Test_SubmitWrite(const char* path, const char* bytes) {
    if (!path || !bytes) return;
    AtomicFileWriter::submit(path, bytes);
}

// Wait for everything queued to reach the disk.
__declspec(dllexport) void MXBMRP3_Test_FlushWrites() {
    AtomicFileWriter::flush();
}

// PluginHost turns the worker OFF before Startup, so every save is on disk by
// the time the call that made it returns and a test may read the file at once.
// The worker's own tests (atomic_writer_test, teardown_test) turn it back on.
__declspec(dllexport) void MXBMRP3_Test_SetAsyncWrites(int enabled) {
    AtomicFileWriter::testSetWorkerEnabled(enabled != 0);
}

// Whether the last write of `path` failed (AtomicFileWriter::needsRetry).
__declspec(dllexport) int MXBMRP3_Test_WriteNeedsRetry(const char* path) {
    return (path && AtomicFileWriter::needsRetry(path)) ? 1 : 0;
}

__declspec(dllexport) int MXBMRP3_Test_WriterRunning() {
    return AtomicFileWriter::isRunning() ? 1 : 0;
}

}  // extern "C"

#endif  // MXBMRP3_TEST_BUILD
