// ============================================================================
// game/connection_detector.h
// Detects whether player is offline, hosting, or connected to a server
//
// This works by reading game memory at known offsets to check for:
// - Local server name (if hosting)
// - Remote server socket address (if connected as client)
//
// WARNING: Memory offsets are version-specific and may break with game updates.
// Detection gracefully falls back to "Unknown" if memory reading fails.
// ============================================================================
#pragma once

#include <string>
#include <cstdint>

namespace Memory {

// Connection type detected from game memory
enum class ConnectionType {
    Unknown,    // Could not determine (memory read failed or offsets invalid)
    Offline,    // Solo testing/practice (not connected to any server)
    Host,       // Hosting a server
    Client      // Connected to someone else's server
};

// Convert ConnectionType to display string
const char* connectionTypeToString(ConnectionType type);

// ============================================================================
// Memory offsets for MX Bikes (default values)
// These are version-specific and may need updating when the game updates.
// Runtime values can be overridden via INI [Advanced] section.
// ============================================================================
namespace MXBikesOffsets {
    // Default offset values (compile-time constants)
    namespace Defaults {
        constexpr uintptr_t LOCAL_SERVER_NAME = 0x9D7108;
        constexpr uintptr_t LOCAL_SERVER_MAX_CLIENTS = 0x9D71C0;
        constexpr uintptr_t LOCAL_SERVER_PASSWORD = 0x9D714C;
        constexpr uintptr_t REMOTE_SERVER_SOCKADDR = 0x58BC5C;
        constexpr uintptr_t REMOTE_SERVER_PASSWORD = 0x9BE7A4;
        constexpr uintptr_t SERVER_CLIENTS_ARRAY = 0xE4A928;
    }

    // Fixed constants (not configurable)
    constexpr size_t LOCAL_SERVER_NAME_SIZE = 64;
    constexpr size_t LOCAL_SERVER_PASSWORD_SIZE = 32;
    constexpr size_t REMOTE_SERVER_SOCKADDR_SIZE = 28;
    constexpr size_t REMOTE_SERVER_NAME_OFFSET = 0x1B;  // 27 bytes from pattern
    constexpr size_t REMOTE_SERVER_NAME_SIZE = 64;
    constexpr size_t REMOTE_SERVER_PASSWORD_SIZE = 32;
    constexpr size_t REMOTE_SERVER_MAX_CLIENTS_OFFSET = 0x5D;  // Relative to pattern match
    constexpr size_t SERVER_CLIENTS_ENTRY_SIZE = 64;
    constexpr size_t SERVER_CLIENTS_MAX_ENTRIES = 50;
}

// ============================================================================
// Runtime memory offset configuration
// Can be modified via INI and reloaded without recompiling
// ============================================================================
struct MemoryOffsetConfig {
    uintptr_t localServerName = MXBikesOffsets::Defaults::LOCAL_SERVER_NAME;
    uintptr_t localServerMaxClients = MXBikesOffsets::Defaults::LOCAL_SERVER_MAX_CLIENTS;
    uintptr_t localServerPassword = MXBikesOffsets::Defaults::LOCAL_SERVER_PASSWORD;
    uintptr_t remoteServerSockaddr = MXBikesOffsets::Defaults::REMOTE_SERVER_SOCKADDR;
    uintptr_t remoteServerPassword = MXBikesOffsets::Defaults::REMOTE_SERVER_PASSWORD;
    uintptr_t serverClientsArray = MXBikesOffsets::Defaults::SERVER_CLIENTS_ARRAY;

    // Reset all offsets to default values
    void resetToDefaults() {
        localServerName = MXBikesOffsets::Defaults::LOCAL_SERVER_NAME;
        localServerMaxClients = MXBikesOffsets::Defaults::LOCAL_SERVER_MAX_CLIENTS;
        localServerPassword = MXBikesOffsets::Defaults::LOCAL_SERVER_PASSWORD;
        remoteServerSockaddr = MXBikesOffsets::Defaults::REMOTE_SERVER_SOCKADDR;
        remoteServerPassword = MXBikesOffsets::Defaults::REMOTE_SERVER_PASSWORD;
        serverClientsArray = MXBikesOffsets::Defaults::SERVER_CLIENTS_ARRAY;
    }
};

// ============================================================================
// ConnectionDetector - Detects online/offline status from game memory
// ============================================================================
class ConnectionDetector {
public:
    static ConnectionDetector& getInstance();

    // Initialize the detector (also initializes MemoryReader if needed)
    bool initialize();

    // Detect current connection type
    // Call this when a race event starts to determine online/offline status
    // Returns ConnectionType::Unknown if detection fails
    ConnectionType detect();

    // Get the last detected connection type (cached from last detect() call)
    ConnectionType getLastDetected() const { return m_lastDetected; }

    // Check if detection is available (memory reader initialized)
    bool isAvailable() const { return m_initialized; }

    // Get server name (only valid if last detection was Host or Client)
    const std::string& getServerName() const { return m_serverName; }

    // Get server password (only valid when online, empty if no password)
    const std::string& getServerPassword() const { return m_serverPassword; }

    // Get server player counts (only valid when online)
    int getServerClientsCount() const { return m_serverClientsCount; }
    int getServerMaxClients() const { return m_serverMaxClients; }

    // Refresh server client counts (call periodically when online)
    // Lightweight - only reads client count, not full detection
    void refreshClientCounts();

    // Reset cached state (call on event deinit)
    void reset();

    // Memory offset configuration (can be modified via INI)
    MemoryOffsetConfig& getOffsetConfig() { return m_offsetConfig; }
    const MemoryOffsetConfig& getOffsetConfig() const { return m_offsetConfig; }

private:
    ConnectionDetector() = default;
    ~ConnectionDetector() = default;
    ConnectionDetector(const ConnectionDetector&) = delete;
    ConnectionDetector& operator=(const ConnectionDetector&) = delete;

    // Check if we're hosting a server
    bool checkIsHost();

    // Check if we're connected as a client
    bool checkIsClient();

    // Read server client counts
    void readServerCounts(uintptr_t patternMatchAddr = 0);

    bool m_initialized = false;
    ConnectionType m_lastDetected = ConnectionType::Unknown;
    std::string m_serverName;
    std::string m_serverPassword;
    int m_serverClientsCount = 0;
    int m_serverMaxClients = 0;
    uintptr_t m_remotePatternAddr = 0;  // Cached for reading max clients as client
    MemoryOffsetConfig m_offsetConfig;  // Runtime-configurable offsets
};

} // namespace Memory
