// ============================================================================
// tests/integration/harness/zipwrite.h
// Minimal STORED (uncompressed) ZIP writer for the updater test — builds a valid
// archive in memory from {name, content} pairs, with correct CRC32s so the
// plugin's miniz reader accepts and extracts it. Not a general zip library: no
// compression, no timestamps, ASCII names. Just enough to feed
// MXBMRP3_Test_ExtractAndInstall a real release-shaped zip without shipping a
// binary fixture.
// ============================================================================
#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace zipw {

inline uint32_t crc32(const std::string& s) {
    uint32_t crc = 0xFFFFFFFFu;
    for (unsigned char c : s) {
        crc ^= c;
        for (int k = 0; k < 8; ++k)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return crc ^ 0xFFFFFFFFu;
}

inline void put16(std::string& o, uint16_t v) {
    o.push_back((char)(v & 0xFF)); o.push_back((char)((v >> 8) & 0xFF));
}
inline void put32(std::string& o, uint32_t v) {
    o.push_back((char)(v & 0xFF));        o.push_back((char)((v >> 8) & 0xFF));
    o.push_back((char)((v >> 16) & 0xFF)); o.push_back((char)((v >> 24) & 0xFF));
}

// Build a stored zip from (filename, content) pairs. Returns the archive bytes.
inline std::string build(const std::vector<std::pair<std::string, std::string>>& files) {
    std::string out;
    struct Central { std::string name; uint32_t crc, size, offset; };
    std::vector<Central> central;

    for (const auto& f : files) {
        const std::string& name = f.first;
        const std::string& data = f.second;
        const uint32_t crc = crc32(data);
        const uint32_t size = (uint32_t)data.size();
        const uint32_t offset = (uint32_t)out.size();

        put32(out, 0x04034b50);          // local file header signature
        put16(out, 20);                  // version needed
        put16(out, 0);                   // flags
        put16(out, 0);                   // method = store
        put16(out, 0);                   // mod time
        put16(out, 0x21);                // mod date (1980-01-01; nonzero so readers don't warn)
        put32(out, crc);
        put32(out, size);                // compressed size
        put32(out, size);                // uncompressed size
        put16(out, (uint16_t)name.size());
        put16(out, 0);                   // extra len
        out += name;
        out += data;

        central.push_back({ name, crc, size, offset });
    }

    const uint32_t cdStart = (uint32_t)out.size();
    for (const auto& c : central) {
        put32(out, 0x02014b50);          // central directory header signature
        put16(out, 20);                  // version made by
        put16(out, 20);                  // version needed
        put16(out, 0);                   // flags
        put16(out, 0);                   // method
        put16(out, 0);                   // mod time
        put16(out, 0x21);                // mod date
        put32(out, c.crc);
        put32(out, c.size);
        put32(out, c.size);
        put16(out, (uint16_t)c.name.size());
        put16(out, 0);                   // extra len
        put16(out, 0);                   // comment len
        put16(out, 0);                   // disk number start
        put16(out, 0);                   // internal attrs
        put32(out, 0);                   // external attrs
        put32(out, c.offset);            // local header offset
        out += c.name;
    }
    const uint32_t cdSize = (uint32_t)out.size() - cdStart;

    put32(out, 0x06054b50);              // end of central directory signature
    put16(out, 0);                       // disk number
    put16(out, 0);                       // disk with cd
    put16(out, (uint16_t)central.size());
    put16(out, (uint16_t)central.size());
    put32(out, cdSize);
    put32(out, cdStart);
    put16(out, 0);                       // comment len
    return out;
}

}  // namespace zipw
