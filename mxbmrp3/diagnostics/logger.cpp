// ============================================================================
// diagnostics/logger.cpp
// Unified logging system - file logging in all builds, console in debug builds
// ============================================================================
#include "logger.h"
#include <chrono>
#include <ctime>
#include <iostream>
#include "../core/plugin_constants.h"

namespace {
    constexpr const char* LOG_SUBDIRECTORY = "mxbmrp3";
    constexpr const char* LOG_FILENAME = "mxbmrp3_log.txt";
}

Logger& Logger::getInstance() {
    static Logger instance;
    return instance;
}

std::string Logger::getLogFilePath(const char* savePath) const {
    std::string path;

    if (!savePath || savePath[0] == '\0') {
        // Use relative path when savePath is not provided
        path = std::string(".\\") + LOG_SUBDIRECTORY;
    } else {
        path = savePath;
        // Ensure path ends with backslash
        if (!path.empty() && path.back() != '/' && path.back() != '\\') {
            path += '\\';
        }
        path += LOG_SUBDIRECTORY;
    }

    // Create directory if it doesn't exist
    if (!CreateDirectoryA(path.c_str(), NULL)) {
        DWORD error = GetLastError();
        if (error != ERROR_ALREADY_EXISTS) {
            // Can't log this error since logger isn't initialized yet
            // Just continue - file open will fail with clearer message
        }
    }

    path += '\\';
    path += LOG_FILENAME;
    return path;
}

void Logger::initialize(const char* savePath) {
    if (m_initialized) return;

#ifdef _DEBUG
    initializeConsole();
#endif

    // Open log file (overwrite mode - fresh log each session)
    m_logFilePath = getLogFilePath(savePath);
    m_logFile.open(m_logFilePath, std::ios::out | std::ios::trunc);

    if (!m_logFile.is_open()) {
        // In debug builds, warn via console
#ifdef _DEBUG
        std::cerr << "WARNING: Failed to open log file: " << m_logFilePath << std::endl;
#endif
        // Continue without file logging - at least debug console works
    }

    m_initialized = true;

    // Log startup banner
    info("========================================");
    char banner[128];
    snprintf(banner, sizeof(banner), "%s v%s",
             PluginConstants::PLUGIN_DISPLAY_NAME,
             PluginConstants::PLUGIN_VERSION);
    info(banner);
    info("========================================");
    info("Logger initialized");

    if (m_logFile.is_open()) {
        info("Log file: %s", m_logFilePath.c_str());
    } else {
        warn("File logging disabled (could not open log file)");
    }
}

void Logger::shutdown() {
    if (!m_initialized) return;

    info("Logger shutting down...");

    if (m_logFile.is_open()) {
        m_logFile.close();
    }

#ifdef _DEBUG
    shutdownConsole();
#endif

    m_initialized = false;
}

void Logger::info(const char* message) {
    log("INFO", message);
}

void Logger::warn(const char* message) {
    log("WARN", message);
}

void Logger::error(const char* message) {
    log("ERROR", message);
}

void Logger::log(const char* level, const char* message) {
    if (!m_initialized) return;

    char timestamp[16];
    getCurrentTimestamp(timestamp, sizeof(timestamp));

    // Format the log line
    char logLine[1100];  // 1024 message + timestamp + level + formatting
    snprintf(logLine, sizeof(logLine), "[%s] [%s] %s", timestamp, level, message);

    // Write to file
    if (m_logFile.is_open()) {
        m_logFile << logLine << std::endl;
        // Flush immediately for reliability (no buffering complexity)
        m_logFile.flush();
    }

#ifdef _DEBUG
    // Also write to console in debug builds
    if (m_consoleInitialized) {
        // Set colors based on log level
        if (strcmp(level, "ERROR") == 0) {
            std::cout << "\033[31m"; // Red
        } else if (strcmp(level, "WARN") == 0) {
            std::cout << "\033[33m"; // Yellow
        } else {
            std::cout << "\033[37m"; // White
        }

        std::cout << logLine;
        std::cout << "\033[0m"; // Reset color
        std::cout << std::endl;
    }
#endif
}

void Logger::getCurrentTimestamp(char* buffer, size_t bufferSize) {
    // Cache timestamp at millisecond granularity to avoid expensive
    // system clock calls and localtime_s conversions on every log statement
    auto now = std::chrono::system_clock::now();
    auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();

    // Only regenerate timestamp if millisecond changed
    if (nowMs != m_lastTimestampMs) {
        auto time_t = std::chrono::system_clock::to_time_t(now);
        auto ms = nowMs % 1000;

        struct tm timeinfo;
        localtime_s(&timeinfo, &time_t);

        snprintf(m_cachedTimestamp, sizeof(m_cachedTimestamp), "%02d:%02d:%02d.%03d",
            timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec, (int)ms);

        m_lastTimestampMs = nowMs;
    }

    // Copy cached timestamp to output buffer
    strncpy_s(buffer, bufferSize, m_cachedTimestamp, _TRUNCATE);
}

#ifdef _DEBUG
void Logger::initializeConsole() {
    if (m_consoleInitialized) return;

    // Check if a console already exists (e.g., when loaded by replay_tool)
    HWND consoleWindow = GetConsoleWindow();
    bool consoleAlreadyExists = (consoleWindow != NULL);

    if (!consoleAlreadyExists) {
        // Allocate a console for this GUI application
        AllocConsole();

        // Redirect stdout, stdin, stderr to console
        freopen_s((FILE**)stdout, "CONOUT$", "w", stdout);
        freopen_s((FILE**)stderr, "CONOUT$", "w", stderr);
        freopen_s((FILE**)stdin, "CONIN$", "r", stdin);

        // Set console title dynamically from constants
        std::wstring title = std::wstring(L"") +
            std::wstring(PluginConstants::PLUGIN_DISPLAY_NAME, PluginConstants::PLUGIN_DISPLAY_NAME + strlen(PluginConstants::PLUGIN_DISPLAY_NAME)) +
            L" v" +
            std::wstring(PluginConstants::PLUGIN_VERSION, PluginConstants::PLUGIN_VERSION + strlen(PluginConstants::PLUGIN_VERSION));
        SetConsoleTitle(title.c_str());

        m_ownConsole = true;
    } else {
        m_ownConsole = false;
    }

    // Get console handle and set colors
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hConsole != INVALID_HANDLE_VALUE) {
        DWORD dwMode = 0;
        GetConsoleMode(hConsole, &dwMode);
        dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        SetConsoleMode(hConsole, dwMode);
    }

    m_consoleInitialized = true;

    if (consoleAlreadyExists) {
        std::cout << "(Using existing console)\n";
    }
}

void Logger::shutdownConsole() {
    if (!m_consoleInitialized) return;

    // Only free the console if we created it
    if (m_ownConsole) {
        FreeConsole();
    }

    m_consoleInitialized = false;
}
#endif
