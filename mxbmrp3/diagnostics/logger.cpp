// ============================================================================
// diagnostics/logger.cpp
// Unified logging system - file logging in all builds, console in debug builds
// ============================================================================
#include "logger.h"
#include <chrono>
#include <cstring>
#include <ctime>
#include <iostream>
#include <shlobj.h>
#include "../core/atomic_file_writer.h"
#include "../core/plugin_constants.h"

namespace {
    constexpr const char* LOG_SUBDIRECTORY = "mxbmrp3";
    constexpr const char* LOG_FILENAME = "mxbmrp3_log.txt";
    // Reserved once for both buffers: a second of a busy session's logging, so
    // the steady state never grows them.
    constexpr size_t BUFFER_RESERVE = 64 * 1024;
    // Past this much unwritten, log() writes it out itself: the writer thread
    // should have long since, so something is holding it up, and the buffer must
    // not grow without bound meanwhile.
    constexpr size_t PENDING_FLUSH_BYTES = 256 * 1024;

    // Set while THIS thread is inside the Logger's locked code. The crash filter
    // runs on the faulting thread, and if that thread faulted in here, a
    // try_lock is no protection: MSVC's std::mutex may hand a plain mutex back
    // to the thread that already owns it, and the buffer may be mid-append.
    thread_local bool t_inLogger = false;
    struct InLogger {
        InLogger() { t_inLogger = true; }
        ~InLogger() { t_inLogger = false; }
    };
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

    // Open log file (overwrite mode - fresh log each session). Shared for read
    // and write so the crash handler's CopyFileA and a user's editor can open it
    // while we hold it.
    //
    // Under the locks, and the scope is deliberately tight: neither is
    // recursive (see the caution in log()), and every info()/warn() below takes
    // them — holding them across those would deadlock on the startup banner. So
    // the open happens here and the results are carried out in locals; nothing
    // below this block touches the guarded members.
    bool opened = false;
    std::string logPath;
    {
        MutexLock fileLock(m_fileMutex);
        m_logFilePath = getLogFilePath(savePath);
        m_file = CreateFileA(m_logFilePath.c_str(), GENERIC_WRITE,
                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (m_file == INVALID_HANDLE_VALUE) m_file = nullptr;
        opened = (m_file != nullptr);
        logPath = m_logFilePath;
        m_writing.reserve(BUFFER_RESERVE);
        MutexLock lock(m_mutex);
        m_pending.reserve(BUFFER_RESERVE);
        // Before the first line: the banner's "Log file:" path is scrubbed too.
        resolveScrubFolders();
    }

    if (!opened) {
        // In debug builds, warn via console
#ifdef _DEBUG
        std::cerr << "WARNING: Failed to open log file: " << logPath << std::endl;
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

    // Host executable build fingerprint. The PiBoSo games ship no VERSIONINFO
    // resource, so there is no FileVersion to read; instead we pull the PE
    // TimeDateStamp straight from the loaded image headers. This is the same
    // value the Windows Application event log reports as the faulting image's
    // "time stamp" (e.g. 0x6a21833d), so it's the one field that ties a bare
    // crash report (no minidump) back to a specific game build. Game-relative
    // crash offsets are only valid for a given build, so recording it lets a
    // crash log self-identify which build produced it.
    {
        char exeName[MAX_PATH] = { 0 };
        GetModuleFileNameA(nullptr, exeName, MAX_PATH);
        const char* baseName = strrchr(exeName, '\\');
        baseName = baseName ? baseName + 1 : exeName;

        HMODULE hHost = GetModuleHandleW(nullptr);
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(hHost);
        if (hHost && dos->e_magic == IMAGE_DOS_SIGNATURE) {
            const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
                reinterpret_cast<const BYTE*>(hHost) + dos->e_lfanew);
            if (nt->Signature == IMAGE_NT_SIGNATURE) {
                info("Host build: %s TimeDateStamp=0x%08X",
                     baseName[0] ? baseName : "(unknown)",
                     nt->FileHeader.TimeDateStamp);
            }
        }
    }

    if (opened) {
        info("Log file: %s", logPath.c_str());
    } else {
        warn("File logging disabled (could not open log file)");
    }
}

void Logger::resolveScrubFolders() {  // MXB_REQUIRES(m_mutex)
    char folder[MAX_PATH] = { 0 };
    if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_PERSONAL, nullptr, SHGFP_TYPE_CURRENT, folder))) {
        LogScrub::setPrefix(m_scrub[0], folder, "<Documents>");
    }
    const DWORD n = GetEnvironmentVariableA("USERPROFILE", folder, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        LogScrub::setPrefix(m_scrub[1], folder, "%USERPROFILE%");
    }
}

void Logger::shutdown() {
    if (!m_initialized) return;

    info("Logger shutting down...");

    // Write out what is buffered, then close the file and clear the flag under
    // both locks. Today the locking is redundant (PluginManager joins every
    // background thread before shutting the logger down last), but a future
    // thread that logs past this point would otherwise race the close.
    {
        MutexLock fileLock(m_fileMutex);
        {
            MutexLock lock(m_mutex);
            m_writing.swap(m_pending);
            m_initialized = false;
        }
        writeOut();
        if (m_file != nullptr) {
            CloseHandle(m_file);
            m_file = nullptr;
        }
    }

#ifdef _DEBUG
    shutdownConsole();
#endif
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

    // Serialize concurrent log() calls from the game thread and the
    // background threads (HttpServer, Discord, UpdateChecker, RecordsHud,
    // UpdateDownloader). Without this, simultaneous appends to the line
    // buffer are UB and lines mangle in practice.
    //
    // CAUTION: m_mutex is not recursive. Do not call log() (or anything
    // that may transitively call log()) from inside any code path that
    // already holds it — e.g. don't route an exception's what() through
    // a logger that itself logs. Doing so will deadlock the calling
    // thread. The crash filter in crash_handler.cpp only ever TRY-locks
    // (flushForCrash) for this reason.
    bool writeNow = false;
    {
        InLogger inLogger;
        MutexLock lock(m_mutex);

        char timestamp[16];
        getCurrentTimestamp(timestamp, sizeof(timestamp));

        // The user folder never reaches the file (log_scrub.h). Wider than the
        // 1024-char format buffer: the label can be longer than the folder it
        // replaces, and a line naming it more than once must not lose its tail.
        char scrubbed[1280];
        LogScrub::scrub(message, scrubbed, sizeof(scrubbed), m_scrub, sizeof(m_scrub) / sizeof(m_scrub[0]));

        // Format the log line. CRLF by hand: the file is written raw, and the logs
        // users send are read in Notepad.
        char logLine[1360];  // scrubbed message + timestamp + level + formatting
        int len = snprintf(logLine, sizeof(logLine) - 2, "[%s] [%s] %s", timestamp, level, scrubbed);
        if (len < 0) len = 0;
        if (len > static_cast<int>(sizeof(logLine)) - 3) len = static_cast<int>(sizeof(logLine)) - 3;
        logLine[len++] = '\r';
        logLine[len++] = '\n';
        m_pending.append(logLine, static_cast<size_t>(len));

        // Inline while the writer thread is down, or when it has fallen far behind.
        writeNow = !AtomicFileWriter::isRunning() || m_pending.size() >= PENDING_FLUSH_BYTES;

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

            std::cout.write(logLine, len - 2);   // the CRLF is the file's; endl below
            std::cout << "\033[0m"; // Reset color
            std::cout << std::endl;
        }
#endif
    }

    if (writeNow) {
        flushPending();
    } else if (level[0] == 'W' || level[0] == 'E') {
        // WARN / ERROR: on disk now, not at the next tick -- the line that says
        // what went wrong is the one a crash right after must not lose.
        AtomicFileWriter::wakeForLog();
    }
}

void Logger::flushPending() {
    if (!m_initialized) return;   // shutdown() wrote out the last of it
    InLogger inLogger;
    MutexLock fileLock(m_fileMutex);
    {
        MutexLock lock(m_mutex);
        if (m_pending.empty()) return;
        m_writing.swap(m_pending);   // both keep their capacity
    }
    writeOut();
}

void Logger::writeOut() {  // MXB_REQUIRES(m_fileMutex)
    if (m_file != nullptr && !m_writing.empty()) {
        DWORD written = 0;
        WriteFile(m_file, m_writing.data(), static_cast<DWORD>(m_writing.size()), &written, nullptr);
    }
    m_writing.clear();
}

// MXB_NO_TSA (declared in the header): TSA cannot follow capabilities taken by
// try_lock through the branches below.
void Logger::flushForCrash() {
    if (t_inLogger) return;
    if (!m_fileMutex.try_lock()) return;
    if (m_mutex.try_lock()) {
        if (m_file != nullptr && !m_pending.empty()) {
            DWORD written = 0;
            WriteFile(m_file, m_pending.data(), static_cast<DWORD>(m_pending.size()), &written, nullptr);
            m_pending.clear();
        }
        m_mutex.unlock();
    }
    m_fileMutex.unlock();
}

void Logger::getCurrentTimestamp(char* buffer, size_t bufferSize) {  // MXB_REQUIRES(m_mutex)
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
        // localtime_s writes timeinfo (output param); cppcheck can't model that.
        // cppcheck-suppress uninitvar
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

    // Check if a console already exists (e.g., when loaded by mxbmrp3_replay)
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
