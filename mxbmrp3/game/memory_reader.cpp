// ============================================================================
// game/memory_reader.cpp
// Safe memory reading utility implementation
// ============================================================================
#include "memory_reader.h"
#include "../diagnostics/logger.h"

#include <windows.h>
#include <algorithm>
#include <cstring>

namespace Memory {

// ============================================================================
// ReadResult implementation
// ============================================================================

std::string ReadResult::asString() const {
    if (!success || data.empty()) {
        return {};
    }

    // Find null terminator
    auto nullPos = std::find(data.begin(), data.end(), 0);
    return std::string(data.begin(), nullPos);
}

int ReadResult::asByte() const {
    if (!success || data.empty()) {
        return 0;
    }
    return static_cast<int>(data[0]);
}

uint16_t ReadResult::asUint16() const {
    if (!success || data.size() < 2) {
        return 0;
    }
    // Little-endian
    return static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8);
}

float ReadResult::asFloat() const {
    if (!success || data.size() < sizeof(float)) {
        return 0.0f;
    }
    float value;
    std::memcpy(&value, data.data(), sizeof(float));
    return value;
}

// ============================================================================
// MemoryReader implementation
// ============================================================================

MemoryReader& MemoryReader::getInstance() {
    static MemoryReader instance;
    return instance;
}

bool MemoryReader::initialize() {
    if (m_baseAddress != 0) {
        return true;  // Already initialized
    }

    HMODULE hModule = GetModuleHandle(nullptr);
    if (hModule == nullptr) {
        DEBUG_WARN("MemoryReader: Failed to get module handle");
        return false;
    }

    m_baseAddress = reinterpret_cast<uintptr_t>(hModule);
    DEBUG_INFO_F("MemoryReader: Initialized with base address 0x%llX",
                 static_cast<unsigned long long>(m_baseAddress));
    return true;
}

bool MemoryReader::safeMemcpy(void* dst, const void* src, size_t bytes) {
    __try {
        std::memcpy(dst, src, bytes);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        // Access violation or other exception - return failure
        return false;
    }
}

ReadResult MemoryReader::readAtOffset(uintptr_t offset, size_t size) const {
    if (m_baseAddress == 0) {
        return {};  // Not initialized
    }
    return readAtAddress(m_baseAddress + offset, size);
}

ReadResult MemoryReader::readAtAddress(uintptr_t address, size_t size) const {
    ReadResult result;

    if (size == 0 || address == 0) {
        return result;
    }

    result.data.resize(size);

    if (!safeMemcpy(result.data.data(),
                    reinterpret_cast<const void*>(address),
                    size)) {
        // Memory access failed - return empty result
        result.data.clear();
        return result;
    }

    result.success = true;
    return result;
}

bool isValidServerName(const std::string& name) {
    // Requires at least 3 characters to avoid false positives from random memory
    if (name.length() < 3) {
        return false;
    }
    // All characters must be printable ASCII (32-125)
    return std::all_of(name.begin(), name.end(), [](unsigned char c) {
        return c >= 32 && c <= 125;
    });
}

MemoryReader::SearchResult MemoryReader::searchAndRead(
    const std::vector<uint8_t>& pattern,
    size_t readOffset,
    size_t readSize) const
{
    SearchResult result;

    if (pattern.empty() || readSize == 0) {
        return result;
    }

    // Log search pattern in hex
#ifdef _DEBUG
    // Log pattern hex in debug builds
    std::string patternHex;
    for (size_t i = 0; i < pattern.size(); ++i) {
        char buf[4];
        snprintf(buf, sizeof(buf), "%02X ", pattern[i]);
        patternHex += buf;
    }
    DEBUG_INFO_F("MemoryReader: Searching for pattern: %s", patternHex.c_str());
#endif

    // Build KMP failure function (lps array)
    std::vector<size_t> lps(pattern.size(), 0);
    for (size_t i = 1, len = 0; i < pattern.size(); ) {
        if (pattern[i] == pattern[len]) {
            lps[i++] = ++len;
        } else if (len > 0) {
            len = lps[len - 1];
        } else {
            lps[i++] = 0;
        }
    }

    // Get memory range to search
    // Heap memory is typically in lower address ranges (below ~2GB on 64-bit)
    // Modules (exe/dlls) load at high addresses (0x7FF7... range)
    // We limit search to first 4GB to focus on heap and avoid module regions
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    uintptr_t addr = reinterpret_cast<uintptr_t>(si.lpMinimumApplicationAddress);
    uintptr_t end = reinterpret_cast<uintptr_t>(si.lpMaximumApplicationAddress);

    // Limit search to first 4GB where heap typically lives
    constexpr uintptr_t HEAP_SEARCH_LIMIT = 0x100000000ULL;  // 4 GB
    if (end > HEAP_SEARCH_LIMIT) {
        end = HEAP_SEARCH_LIMIT;
    }

    constexpr size_t CHUNK_SIZE = 8 * 1024 * 1024;  // 8 MiB chunks

    // Region size filters - server data lives in ~2MB allocations
    // Skip small heap allocations and large buffers (textures, physics, etc.)
    constexpr size_t MIN_REGION_SIZE = 1 * 1024 * 1024;  // 1 MB minimum
    constexpr size_t MAX_REGION_SIZE = 4 * 1024 * 1024;  // 4 MB maximum
    const size_t minRegionSize = (std::max)(MIN_REGION_SIZE, pattern.size() + readOffset + readSize);

    // Tracking stats (used for debug logging)
    size_t totalBytesSearched = 0;
    size_t regionsSearched = 0;
    size_t regionsSkipped = 0;
    (void)regionsSearched; (void)regionsSkipped;  // Suppress unused warnings in release

#ifdef _DEBUG
    DEBUG_INFO_F("MemoryReader: Search range 0x%llX - 0x%llX (%.0f MB limit)",
        static_cast<unsigned long long>(addr),
        static_cast<unsigned long long>(end),
        static_cast<double>(end) / (1024.0 * 1024.0));
#endif

    while (addr < end) {
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery(reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi)) == 0) {
            break;
        }

        // Only search committed private RW memory (heap)
        // MEM_PRIVATE excludes MEM_IMAGE (loaded modules) and MEM_MAPPED (file mappings)
        // Filter by region size: skip small allocations and large buffers
        if (mbi.State != MEM_COMMIT ||
            mbi.Type != MEM_PRIVATE ||
            mbi.Protect != PAGE_READWRITE ||
            (mbi.Protect & PAGE_GUARD) ||
            mbi.RegionSize < minRegionSize ||
            mbi.RegionSize > MAX_REGION_SIZE)
        {
            ++regionsSkipped;
            addr = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
            continue;
        }

        size_t regionSize = mbi.RegionSize;
        ++regionsSearched;
        totalBytesSearched += regionSize;

        // Process region in chunks
        for (size_t regionOff = 0; regionOff < regionSize; regionOff += CHUNK_SIZE) {
            size_t bytesLeft = regionSize - regionOff;
            size_t toRead = (CHUNK_SIZE + pattern.size() - 1 < bytesLeft)
                          ? CHUNK_SIZE + pattern.size() - 1
                          : bytesLeft;

            auto chunk = readAtAddress(addr + regionOff, toRead);
            if (!chunk) {
                break;
            }

            // KMP search in chunk
            size_t R = chunk.data.size();
            size_t P = pattern.size();
            size_t i = 0, j = 0;

            while (i < R) {
                if (chunk.data[i] == pattern[j]) {
                    ++i; ++j;
                    if (j == P) {
                        // Found pattern
                        uintptr_t foundAddr = addr + regionOff + (i - j);
                        uintptr_t dataAddr = foundAddr + readOffset;

                        // Check if data is within this region
                        if (dataAddr + readSize <= addr + regionSize) {
                            auto dataResult = readAtAddress(dataAddr, readSize);
                            if (dataResult) {
                                std::string candidate = dataResult.asString();
                                if (isValidServerName(candidate)) {
                                    result.foundAddress = foundAddr;
                                    result.value = candidate;

#ifdef _DEBUG
                                    // Log detailed search results in debug builds
                                    double mbSearched = static_cast<double>(totalBytesSearched) / (1024.0 * 1024.0);

                                    // Try to get module name for the found address
                                    char moduleName[MAX_PATH] = "unknown";
                                    HMODULE hMod = nullptr;
                                    if (GetModuleHandleExA(
                                            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                            reinterpret_cast<LPCSTR>(foundAddr),
                                            &hMod) && hMod) {
                                        GetModuleFileNameA(hMod, moduleName, MAX_PATH);
                                        // Extract just the filename
                                        char* lastSlash = strrchr(moduleName, '\\');
                                        if (lastSlash) {
                                            memmove(moduleName, lastSlash + 1, strlen(lastSlash + 1) + 1);
                                        }
                                    } else {
                                        snprintf(moduleName, sizeof(moduleName), "heap/private");
                                    }

                                    DEBUG_INFO_F("MemoryReader: Pattern FOUND after searching %.2f MB (%zu regions, %zu skipped)",
                                        mbSearched, regionsSearched, regionsSkipped);
                                    DEBUG_INFO_F("MemoryReader: Found at address 0x%llX in [%s]",
                                        static_cast<unsigned long long>(foundAddr),
                                        moduleName);
                                    DEBUG_INFO_F("MemoryReader: Region: base=0x%llX size=%zu KB allocBase=0x%llX protect=0x%X",
                                        static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(mbi.BaseAddress)),
                                        mbi.RegionSize / 1024,
                                        static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(mbi.AllocationBase)),
                                        mbi.Protect);
                                    DEBUG_INFO_F("MemoryReader: Data at offset +0x%zX: \"%s\"",
                                        readOffset, candidate.c_str());
#endif

                                    return result;
                                }
                            }
                        }
                        j = lps[j - 1];
                    }
                } else if (j > 0) {
                    j = lps[j - 1];
                } else {
                    ++i;
                }
            }
        }

        addr = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + regionSize;
    }

    // Not found
#ifdef _DEBUG
    double mbSearched = static_cast<double>(totalBytesSearched) / (1024.0 * 1024.0);
    DEBUG_INFO_F("MemoryReader: Pattern NOT FOUND after searching %.2f MB (%zu regions, %zu skipped)",
        mbSearched, regionsSearched, regionsSkipped);
#endif

    return result;
}

} // namespace Memory
