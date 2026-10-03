#pragma once

#include <string>

// The plugin's one writer for persisted files (settings, stats, PB traces, tracked riders,
// rumble profiles, analytics identity, reports), and the thread that does its disk I/O.
//
// ATOMIC. Each write goes to a uniquely named temp file and is moved over the target with
// MoveFileExA(REPLACE_EXISTING | WRITE_THROUGH), so a crash mid-write leaves the old file
// whole, never a half-written one.
//
// OFF THE GAME THREAD. submit() hands the bytes to a worker and returns; the game thread
// pays for building the bytes, not for the disk. A WRITE_THROUGH replace plus antivirus can
// stall for milliseconds, and the saves land on callbacks the game thread is still running
// (leaving the track, a settings click). The worker also flushes the log (Logger), about
// once a second and at once on a warning or error, so the game thread never waits on the
// log file either.
//
// ORDER. One worker, one FIFO queue: two files submitted in turn land in that order, and a
// second submit for a path still waiting in the queue replaces its bytes in place (the last
// state wins, the stale one is never written).
//
// WHEN THE WORKER IS NOT RUNNING (before start(), after stop(), or disabled by the test
// harness) submit() writes on the calling thread, so no write is ever dropped. stop() drains
// the queue before it returns, and PluginManager::shutdown() calls it after the last save,
// so a clean exit (menu quit or Alt-F4, both of which reach the Shutdown() export) has every
// file on disk.
class AtomicFileWriter {
public:
    // Queue `bytes` for `path` and return. Returns false only when the write ran inline
    // (worker not running) and failed; a queued write that fails later is logged and counted
    // in failureCount().
    static bool submit(const std::string& path, std::string bytes);

    // Synchronous atomic write on the calling thread. For a caller that reads the file back
    // straight away (launch-time migrations) and for the worker itself.
    static bool writeNow(const std::string& path, const std::string& bytes);

    // Block until every write submitted so far is on disk. Cheap when the queue is empty.
    static void flush();

    // True while the LAST write of `path` failed (inline or on the worker), until one
    // succeeds. A manager's dirty flag clears when its bytes are handed over, so its save()
    // also asks this: a failed write is retried at the next save point even when nothing
    // has changed since.
    static bool needsRetry(const std::string& path);

    // Queued writes that failed since launch. A caller that must know its own write landed
    // (the stats migration deletes the old files only then) compares it around submit+flush.
    static unsigned failureCount();

    // Start / drain-and-join the worker. Both from PluginManager (initialize + its rollback,
    // and shutdown); idempotent.
    static void start();
    static void stop();

    // Logger: a warning or error was logged, flush the log now rather than at the next tick.
    // A no-op while the worker is not running (the Logger then writes inline).
    static void wakeForLog();

    // Whether the worker is up; the Logger writes inline when it is not.
    static bool isRunning();

    // Test harness only (MXBMRP3_Test_SetAsyncWrites): with the worker disabled, start() does
    // nothing and every write is inline, so a test can read a file right after the call that
    // saved it. The tests of the worker itself turn it back on.
    static void testSetWorkerEnabled(bool enabled);

private:
    AtomicFileWriter() = delete;
};
