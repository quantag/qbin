#ifndef QBIN_DECOMPILER_TOOLS_HPP
#define QBIN_DECOMPILER_TOOLS_HPP

#include <cstdint>
#include <string>
#include <vector>
#include <cstring> // for std::memcmp

namespace qbin_decompiler {
namespace tools {

// ---- QBIN v1 header (fixed 20 bytes, no section table) ----
struct FileHeader {
    uint16_t version = 0;
    uint16_t flags = 0;
    uint32_t header_size = 0;
    uint32_t section_count = 0;
    uint32_t reserved = 0;
};

// Little-endian primitives
inline uint32_t rd_u32le(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

uint16_t read_u16(const std::vector<unsigned char>& b, size_t& i);
uint32_t read_u32(const std::vector<unsigned char>& b, size_t& i);

// ULEB128 with end bound
bool read_uleb128_bound(const std::vector<uint8_t>& b, size_t& i, size_t end, uint64_t& v);

// f32 (little-endian) with bound
bool read_f32le_bound(const std::vector<uint8_t>& b, size_t& i, size_t end, float& out);

// Parse QBIN v1 header, advance pos to the first section/tag after header padding
bool read_header_v1(const std::vector<uint8_t>& b, size_t& pos, FileHeader& h, std::string& err, bool verbose);

} // namespace tools
} // namespace qbin_decompiler

#endif // QBIN_DECOMPILER_TOOLS_HPP
