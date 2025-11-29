// ============================================================================
// diagnostics/logger.h
// Unified logging system - file logging in all builds, console in debug builds
// ============================================================================
#pragma once

#define NOMINMAX
#include <windows.h>
#include <string>
#include <fstream>
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
    void getCurrentTimestamp(char* buffer, size_t bufferSize);
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

    bool m_initialized;
    std::ofstream m_logFile;
    std::string m_logFilePath;

    // Timestamp caching for performance
    int64_t m_lastTimestampMs;
    char m_cachedTimestamp[16];

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
