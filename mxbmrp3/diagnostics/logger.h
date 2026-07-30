// ============================================================================
// diagnostics/logger.h
// Unified logging system - file logging in all builds, console in debug builds
// ============================================================================
#pragma once

#include <windows.h>
#include <string>
#include <fstream>
#include <mutex>
#include "../core/thread_safety.h"
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
    Logger() : m_initialized(false), m_lastTimestampMs(0) {
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

    // Atomic: written under m_logMutex in initialize()/shutdown() but read lock-free
    // at the top of log() (called from the game thread and background threads).
    std::atomic<bool> m_initialized;
    // The members the mutex actually exists for — concurrent writes to an
    // ofstream's streambuf are the race it serializes. These used to carry
    // prose ("guarded by m_mutex after init") instead of the annotation,
    // because initialize() opened the file outside the lock; it no longer does,
    // so the analysis checks them like everything else.
    std::ofstream m_logFile MXB_GUARDED_BY(m_mutex);
    std::string m_logFilePath MXB_GUARDED_BY(m_mutex);

    // Serializes log() so concurrent calls from the game thread and
    // background threads (HttpServer, Discord, UpdateChecker, RecordsHud,
    // UpdateDownloader) don't race on the ofstream's streambuf — which
    // is UB under the C++ standard and tends to mangle output lines in
    // practice. Also protects m_lastTimestampMs / m_cachedTimestamp.
    Mutex m_mutex;

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
