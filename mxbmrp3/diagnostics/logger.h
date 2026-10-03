// ============================================================================
// diagnostics/logger.h
// Unified logging system - file logging in all builds, console in debug builds
//
// BUFFERED. log() formats the line into an in-memory buffer and returns; the
// disk writer's thread (AtomicFileWriter) writes the buffer out about once a
// second, and at once after a WARN or ERROR. So a DEBUG_* call on the game
// thread costs a format and a copy, never a disk write. While that thread is
// not running (launch, shutdown, the test harness) every line is written
// inline, as before. The crash handler writes out whatever is still buffered
// before it copies the log beside the dump (flushForCrash()).
//
// The log stays OUT of AtomicFileWriter's atomic replace: it is append-only,
// and rewriting the whole file per flush would cost more the longer the
// session ran.
// ============================================================================
#pragma once

#include <windows.h>
#include <string>
#include <mutex>
#include "../core/thread_safety.h"
#include "log_scrub.h"
#include <atomic>
#include <cstdio>

class Logger {
public:
    static Logger& getInstance();

    void initialize(const char* savePath);
    void shutdown();

    void info(const char* message);
    void warn(const char* message);
    void error(const char* message);

    // Write the buffered lines to the file. The disk writer's thread calls it on
    // every pass; log() calls it inline while that thread is not running.
    void flushPending();

    // From the SEH crash filter only: write the buffered lines out if both locks
    // can be TAKEN WITHOUT WAITING (the faulting thread, or one ExitProcess is
    // about to kill, may hold either), else give up. The dump is the
    // load-bearing artifact; this is what makes the log copied beside it end at
    // the crash rather than up to a second earlier.
    void flushForCrash() MXB_NO_TSA;

    // Template for formatted logging
    template<typename... Args>
    void info(const char* format, Args... args) {
        logFormatted("INFO", format, args...);
    }

    template<typename... Args>
    void warn(const char* format, Args... args) {
        logFormatted("WARN", format, args...);
    }

    template<typename... Args>
    void error(const char* format, Args... args) {
        logFormatted("ERROR", format, args...);
    }

private:
    Logger() : m_initialized(false), m_file(nullptr), m_lastTimestampMs(0) {
        m_cachedTimestamp[0] = '\0';
#ifdef _DEBUG
        m_consoleInitialized = false;
        m_ownConsole = false;
#endif
    }
    ~Logger() { shutdown(); }
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void log(const char* level, const char* message);
    // Reads/refreshes the cached timestamp — called from log() under m_mutex.
    void getCurrentTimestamp(char* buffer, size_t bufferSize) MXB_REQUIRES(m_mutex);
    std::string getLogFilePath(const char* savePath) const;
    // Resolve the Documents and user-profile folders into m_scrub. Under m_mutex.
    void resolveScrubFolders() MXB_REQUIRES(m_mutex);

#ifdef _DEBUG
    void initializeConsole();
    void shutdownConsole();
#endif

    template<typename... Args>
    void logFormatted(const char* level, const char* format, Args... args) {
        char buffer[1024];
        snprintf(buffer, sizeof(buffer), format, args...);
        log(level, buffer);
    }

    // Write `m_writing` to the file and empty it. Under the file lock.
    void writeOut() MXB_REQUIRES(m_fileMutex);

    // Atomic: written under the locks in initialize()/shutdown() but read lock-free
    // at the top of log() (called from the game thread and background threads).
    std::atomic<bool> m_initialized;

    // THE FILE SIDE, under m_fileMutex: the handle, and the buffer being written
    // out. LOCK ORDER: m_fileMutex before m_mutex, never the reverse -- a flush
    // holds the file lock while it takes the line lock to swap the buffers.
    Mutex m_fileMutex;
    HANDLE m_file MXB_GUARDED_BY(m_fileMutex);   // nullptr while not open
    std::string m_logFilePath MXB_GUARDED_BY(m_fileMutex);
    std::string m_writing MXB_GUARDED_BY(m_fileMutex);

    // THE LINE SIDE, under m_mutex: the buffer log() appends to. Swapped with
    // m_writing on a flush, not copied, so both keep their capacity and a
    // steady-state log() allocates nothing. Serializes concurrent log() calls
    // from the game thread and the background threads, so lines never
    // interleave. Also protects m_lastTimestampMs / m_cachedTimestamp.
    Mutex m_mutex;
    std::string m_pending MXB_GUARDED_BY(m_mutex);

    // The folders every line is scrubbed of (log_scrub.h), Documents first: it
    // is the more specific of the two, and usually inside the profile.
    LogScrub::Prefix m_scrub[2] MXB_GUARDED_BY(m_mutex);

    // Timestamp caching for performance
    int64_t m_lastTimestampMs MXB_GUARDED_BY(m_mutex);
    char m_cachedTimestamp[16] MXB_GUARDED_BY(m_mutex);

#ifdef _DEBUG
    bool m_consoleInitialized;
    bool m_ownConsole;
#endif
};

// Logging macros - work in all builds
#define DEBUG_INFO(msg) Logger::getInstance().info(msg)
#define DEBUG_WARN(msg) Logger::getInstance().warn(msg)
#define DEBUG_ERROR(msg) Logger::getInstance().error(msg)

#define DEBUG_INFO_F(...) Logger::getInstance().info(__VA_ARGS__)
#define DEBUG_WARN_F(...) Logger::getInstance().warn(__VA_ARGS__)
#define DEBUG_ERROR_F(...) Logger::getInstance().error(__VA_ARGS__)
