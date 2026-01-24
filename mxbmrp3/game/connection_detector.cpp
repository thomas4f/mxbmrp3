// ============================================================================
// game/connection_detector.cpp
// Connection detection implementation
// ============================================================================
#include "connection_detector.h"
#include "memory_reader.h"
#include "game_config.h"
#include "../diagnostics/logger.h"

#include <algorithm>
#include <cctype>

namespace Memory {

const char* connectionTypeToString(ConnectionType type) {
    switch (type) {
        case ConnectionType::Offline: return "Offline";
        case ConnectionType::Host:    return "Host";
        case ConnectionType::Client:  return "Client";
        default:                      return "Unknown";
    }
}

ConnectionDetector& ConnectionDetector::getInstance() {
    static ConnectionDetector instance;
    return instance;
}

bool ConnectionDetector::initialize() {
    if (m_initialized) {
        return true;
    }

    // Initialize memory reader
    auto& memReader = MemoryReader::getInstance();
    if (!memReader.initialize()) {
        DEBUG_WARN("ConnectionDetector: Failed to initialize MemoryReader");
        return false;
    }

    m_initialized = true;
    DEBUG_INFO("ConnectionDetector: Initialized successfully");
    return true;
}

void ConnectionDetector::reset() {
    m_lastDetected = ConnectionType::Unknown;
    m_serverName.clear();
    m_serverPassword.clear();
    m_serverClientsCount = 0;
    m_serverMaxClients = 0;
    m_remotePatternAddr = 0;
}

bool ConnectionDetector::checkIsHost() {
    if (!m_initialized) {
        return false;
    }

#if defined(GAME_MXBIKES)
    auto& memReader = MemoryReader::getInstance();

    auto result = memReader.readAtOffset(
        m_offsetConfig.localServerName,
        MXBikesOffsets::LOCAL_SERVER_NAME_SIZE
    );

    if (!result) {
        return false;
    }

    std::string serverName = result.asString();
    if (!serverName.empty() && isValidServerName(serverName)) {
        m_serverName = serverName;

        // Read server password
        auto pwResult = memReader.readAtOffset(
            m_offsetConfig.localServerPassword,
            MXBikesOffsets::LOCAL_SERVER_PASSWORD_SIZE
        );
        if (pwResult) {
            m_serverPassword = pwResult.asString();
        }

        DEBUG_INFO_F("ConnectionDetector: Detected as Host (server: %s, password: %s)",
            serverName.c_str(), m_serverPassword.empty() ? "(none)" : "(set)");
        return true;
    }
#endif

    return false;
}

bool ConnectionDetector::checkIsClient() {
    if (!m_initialized) {
        return false;
    }

#if defined(GAME_MXBIKES)
    auto& memReader = MemoryReader::getInstance();

    auto result = memReader.readAtOffset(
        m_offsetConfig.remoteServerSockaddr,
        MXBikesOffsets::REMOTE_SERVER_SOCKADDR_SIZE
    );

    if (!result || result.data.size() < 28) {
        return false;
    }

    // Check if socket address is non-zero (all zeroes = not connected)
    bool hasData = std::any_of(result.data.begin(), result.data.end(),
                               [](uint8_t b) { return b != 0; });

    if (!hasData) {
        return false;
    }

    // We're connected as a client - try to find the server name
    // Check for IPv6-mapped-IPv4 address (bytes 22-23 should be 0xFF 0xFF)
    if (result.data[22] == 0xFF && result.data[23] == 0xFF) {
        // Extract search pattern: IPv6 marker + IPv4 address + port
        // Pattern: bytes 22-27 (IP) + bytes 6-7 (port)
        std::vector<uint8_t> pattern;
        pattern.reserve(8);
        pattern.insert(pattern.end(), result.data.begin() + 22, result.data.begin() + 28);
        pattern.insert(pattern.end(), result.data.begin() + 6, result.data.begin() + 8);

        // Search for pattern and read server name
        // NOTE: This is a slow operation (~100ms) that scans memory for the server data.
        // It only runs once per event init (when detect() is called), not per-frame.
        auto searchResult = memReader.searchAndRead(
            pattern,
            MXBikesOffsets::REMOTE_SERVER_NAME_OFFSET,
            MXBikesOffsets::REMOTE_SERVER_NAME_SIZE
        );

        if (searchResult && !searchResult.value.empty()) {
            m_serverName = searchResult.value;
            m_remotePatternAddr = searchResult.foundAddress;  // Save for reading max clients
            DEBUG_INFO_F("ConnectionDetector: Detected as Client (server: %s)", m_serverName.c_str());
        } else {
            DEBUG_INFO("ConnectionDetector: Detected as Client (server name not found)");
        }
    } else {
        DEBUG_INFO("ConnectionDetector: Detected as Client (non-IPv4 address)");
    }

    // Read server password from direct offset
    auto pwResult = memReader.readAtOffset(
        m_offsetConfig.remoteServerPassword,
        MXBikesOffsets::REMOTE_SERVER_PASSWORD_SIZE
    );
    if (pwResult) {
        m_serverPassword = pwResult.asString();
        DEBUG_INFO_F("ConnectionDetector: Client password: %s",
            m_serverPassword.empty() ? "(none)" : "(set)");
    }

    return true;
#endif

    return false;
}

void ConnectionDetector::readServerCounts(uintptr_t patternMatchAddr) {
#if defined(GAME_MXBIKES)
    auto& memReader = MemoryReader::getInstance();

    // Read current client count from client array
    // Each entry is SERVER_CLIENTS_ENTRY_SIZE bytes, first byte non-zero = connected
    size_t arraySize = MXBikesOffsets::SERVER_CLIENTS_ENTRY_SIZE * MXBikesOffsets::SERVER_CLIENTS_MAX_ENTRIES;
    auto clientsResult = memReader.readAtOffset(m_offsetConfig.serverClientsArray, arraySize);

    if (clientsResult) {
#ifdef _DEBUG
        // Verbose hex dump logging for debugging offset issues
        DEBUG_INFO_F("ConnectionDetector: Reading clients from offset 0x%llX, got %zu bytes",
            static_cast<unsigned long long>(m_offsetConfig.serverClientsArray),
            clientsResult.data.size());

        // Hex dump first 64 bytes
        std::string hexDump;
        size_t dumpSize = (std::min)(static_cast<size_t>(64), clientsResult.data.size());
        for (size_t i = 0; i < dumpSize; ++i) {
            char buf[4];
            snprintf(buf, sizeof(buf), "%02X ", clientsResult.data[i]);
            hexDump += buf;
            if ((i + 1) % 16 == 0) hexDump += "\n";
        }
        DEBUG_INFO_F("ConnectionDetector: First 64 bytes:\n%s", hexDump.c_str());

        // Log first byte of each entry (up to first 10 entries)
        std::string entryDump = "Entry first bytes: ";
        for (size_t i = 0; i < (std::min)(static_cast<size_t>(10), MXBikesOffsets::SERVER_CLIENTS_MAX_ENTRIES); ++i) {
            size_t offset = i * MXBikesOffsets::SERVER_CLIENTS_ENTRY_SIZE;
            if (offset < clientsResult.data.size()) {
                char buf[8];
                snprintf(buf, sizeof(buf), "[%zu]=0x%02X ", i, clientsResult.data[offset]);
                entryDump += buf;
            }
        }
        DEBUG_INFO_F("ConnectionDetector: %s", entryDump.c_str());
#endif

        m_serverClientsCount = 1;  // Local player is always present
        for (size_t i = 0; i < MXBikesOffsets::SERVER_CLIENTS_MAX_ENTRIES; ++i) {
            size_t offset = i * MXBikesOffsets::SERVER_CLIENTS_ENTRY_SIZE;
            if (offset < clientsResult.data.size() && clientsResult.data[offset] != 0) {
                ++m_serverClientsCount;
            }
        }
    }

    // Read max clients
    if (m_lastDetected == ConnectionType::Host) {
        // Host: direct offset read
        auto maxResult = memReader.readAtOffset(m_offsetConfig.localServerMaxClients, 1);
        if (maxResult) {
            m_serverMaxClients = maxResult.asByte();
        }
    } else if (m_lastDetected == ConnectionType::Client && patternMatchAddr != 0) {
        // Client: read relative to pattern match address
        auto maxResult = memReader.readAtAddress(
            patternMatchAddr + MXBikesOffsets::REMOTE_SERVER_MAX_CLIENTS_OFFSET, 1);
        if (maxResult) {
            m_serverMaxClients = maxResult.asByte();
        }
    }

    // Clamp client count to max (in case memory contains garbage)
    if (m_serverMaxClients > 0 && m_serverClientsCount > m_serverMaxClients) {
        m_serverClientsCount = m_serverMaxClients;
    }

    DEBUG_INFO_F("ConnectionDetector: Server clients %d/%d", m_serverClientsCount, m_serverMaxClients);
#endif
}

void ConnectionDetector::refreshClientCounts() {
#if defined(GAME_MXBIKES)
    // Only refresh if we're online and initialized
    if (!m_initialized) return;
    if (m_lastDetected != ConnectionType::Host && m_lastDetected != ConnectionType::Client) return;

    // Read server counts using cached pattern address
    readServerCounts(m_remotePatternAddr);
#endif
}

ConnectionType ConnectionDetector::detect() {
    if (!m_initialized) {
        // Try to initialize if not already
        if (!initialize()) {
            m_lastDetected = ConnectionType::Unknown;
            return m_lastDetected;
        }
    }

    // Reset cached data
    m_serverName.clear();
    m_serverPassword.clear();
    m_serverClientsCount = 0;
    m_serverMaxClients = 0;
    m_remotePatternAddr = 0;

    // Check in order: Host first (has local server name), then Client, else Offline
    if (checkIsHost()) {
        m_lastDetected = ConnectionType::Host;
        readServerCounts(0);
    }
    else if (checkIsClient()) {
        m_lastDetected = ConnectionType::Client;
        readServerCounts(m_remotePatternAddr);
    }
    else {
        // Neither host nor client = offline
        m_lastDetected = ConnectionType::Offline;
        DEBUG_INFO("ConnectionDetector: Detected as Offline");
    }

    return m_lastDetected;
}

} // namespace Memory
