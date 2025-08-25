#include "qbin_decompiler/tools.hpp"

#include <stdexcept>
#include <cstdio>

namespace qbin_decompiler {
namespace tools {

uint16_t read_u16(const std::vector<unsigned char>& b, size_t& i) {
    if (i + 2 > b.size()) throw std::runtime_error("eof reading u16");
    uint16_t v = (uint16_t)b[i] | ((uint16_t)b[i + 1] << 8);
    i += 2;
    return v;
}

uint32_t read_u32(const std::vector<unsigned char>& b, size_t& i) {
    if (i + 4 > b.size()) throw std::runtime_error("eof reading u32");
    uint32_t v = (uint32_t)b[i]
        | ((uint32_t)b[i + 1] << 8)
        | ((uint32_t)b[i + 2] << 16)
        | ((uint32_t)b[i + 3] << 24);
    i += 4;
    return v;
}

bool read_uleb128_bound(const std::vector<uint8_t>& b, size_t& i, size_t end, uint64_t& v) {
    v = 0;
    int shift = 0;
    while (i < end) {
        uint8_t byte = b[i++];
        v |= (uint64_t)(byte & 0x7F) << shift;
        if ((byte & 0x80) == 0) return true;
        shift += 7;
        if (shift > 63) return false;
    }
    return false;
}

bool read_f32le_bound(const std::vector<uint8_t>& b, size_t& i, size_t end, float& out) {
    if (i + 4 > end) return false;
    uint32_t u = rd_u32le(&b[i]);
    i += 4;
    std::memcpy(&out, &u, 4);
    return true;
}

bool read_header_v1(const std::vector<uint8_t>& b, size_t& pos, FileHeader& h, std::string& err, bool verbose) {
    if (b.size() < 20) { err = "file too small for header"; return false; }
    if (std::memcmp(b.data(), "QBIN", 4) != 0) { err = "bad magic (not QBIN)"; return false; }

    pos = 4;
    h.version = read_u16(b, pos);
    h.flags = read_u16(b, pos);
    h.header_size = read_u32(b, pos);
    h.section_count = read_u32(b, pos);
    h.reserved = read_u32(b, pos);

    const uint32_t kMinHeaderSize = 20;
    if (h.header_size < kMinHeaderSize) { err = "header too small: " + std::to_string(h.header_size); return false; }
    if (h.header_size > b.size()) { err = "header claims bigger than file: " + std::to_string(h.header_size); return false; }

    // Skip any extra header padding to land at first section/tag
    if (h.header_size > kMinHeaderSize) {
        size_t extra = h.header_size - kMinHeaderSize;
        if (pos + extra > b.size()) { err = "header padding runs past file"; return false; }
        pos += extra;
    }

    if (verbose) {
        std::fprintf(stderr, "[qbin] header: ver=%u flags=0x%04x header_size=%u sections=%u\n",
            (unsigned)h.version, (unsigned)h.flags,
            (unsigned)h.header_size, (unsigned)h.section_count);
    }
    return true;
}

} // namespace tools
} // namespace qbin_decompiler
