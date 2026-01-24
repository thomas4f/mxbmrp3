// ============================================================================
// game/memory_reader.h
// Safe memory reading utility for reading game process memory
//
// WARNING: This reads memory at hardcoded offsets that are version-specific.
// If the game updates, offsets may change and this will return empty/invalid data.
// All reads are wrapped in SEH to prevent crashes on invalid memory access.
// ============================================================================
#pragma once

#include <vector>
#include <string>
#include <cstdint>

namespace Memory {

// Validate if a string looks like a valid server name
// Used to filter out garbage data from memory reads
// Requires at least 3 printable ASCII characters (32-125)
bool isValidServerName(const std::string& name);

// Result of a memory read operation
struct ReadResult {
    bool success = false;
    std::vector<uint8_t> data;

    // Convenience: check if read succeeded and has data
    explicit operator bool() const { return success && !data.empty(); }

    // Get data as null-terminated string (finds first null, returns up to that point)
    std::string asString() const;

    // Get first byte as int (for single-byte values)
    int asByte() const;

    // Get as uint16 (little-endian)
    uint16_t asUint16() const;

    // Get as float
    float asFloat() const;
};

// MemoryReader - Safe memory reading from current process
// All operations are fail-safe and return empty results on error
class MemoryReader {
public:
    static MemoryReader& getInstance();

    // Initialize with current module base address
    // Call this once at plugin startup
    bool initialize();

    // Check if initialized
    bool isInitialized() const { return m_baseAddress != 0; }

    // Get base address (for debugging)
    uintptr_t getBaseAddress() const { return m_baseAddress; }

    // Read bytes at offset relative to module base
    // Returns empty result on any error (never throws/crashes)
    ReadResult readAtOffset(uintptr_t offset, size_t size) const;

    // Read bytes at absolute address
    // Returns empty result on any error (never throws/crashes)
    ReadResult readAtAddress(uintptr_t address, size_t size) const;

    // Search for a byte pattern in process memory and read data at offset from match
    // Returns: {foundAddress, data} - foundAddress is 0 if not found
    // This scans committed private RW memory regions (slow, use sparingly)
    struct SearchResult {
        uintptr_t foundAddress = 0;
        std::string value;
        explicit operator bool() const { return foundAddress != 0; }
    };
    SearchResult searchAndRead(const std::vector<uint8_t>& pattern,
                               size_t readOffset,
                               size_t readSize) const;

private:
    MemoryReader() = default;
    ~MemoryReader() = default;
    MemoryReader(const MemoryReader&) = delete;
    MemoryReader& operator=(const MemoryReader&) = delete;

    // Perform safe memory copy with SEH protection
    // Returns true if copy succeeded, false on access violation
    static bool safeMemcpy(void* dst, const void* src, size_t bytes);

    uintptr_t m_baseAddress = 0;
};

} // namespace Memory
