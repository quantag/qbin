// compiler.cpp - Compile OpenQASM (subset) to QBIN using the frontend IR.
// Status: MVP. Self-contained encoder; parser lives in qasm_frontend.*

#include "qbin_compiler/compiler.hpp"
#include "qbin_compiler/qasm_frontend.hpp"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace qbin_compiler {

    // --------------------------- Low-level writers ---------------------------

    static inline void push_u32_le(std::vector<uint8_t>& out, uint32_t v) {
        out.push_back(static_cast<uint8_t>(v & 0xFF));
        out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
        out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
        out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
    }
    static inline void push_bytes(std::vector<uint8_t>& out, const void* data, size_t n) {
        const auto* p = static_cast<const uint8_t*>(data);
        out.insert(out.end(), p, p + n);
    }
    static inline void push_str(std::vector<uint8_t>& out, const char* s) {
        push_bytes(out, s, std::strlen(s));
    }
    // ULEB128
    static inline void push_uleb128(std::vector<uint8_t>& out, uint64_t n) {
        do {
            uint8_t b = static_cast<uint8_t>(n & 0x7Fu);
            n >>= 7;
            if (n != 0) b |= 0x80u;
            out.push_back(b);
        } while (n != 0);
    }
    // Float32 (IEEE-754) write helper
    static inline void push_f32_le(std::vector<uint8_t>& out, float f) {
        static_assert(sizeof(float) == 4, "float must be 32-bit");
        uint32_t u;
        std::memcpy(&u, &f, sizeof(u));
        push_u32_le(out, u);
    }
    // CRC32C (Castagnoli), reflected poly 0x82F63B78
    static inline uint32_t crc32c(const uint8_t* data, size_t len) {
        static uint32_t table[256];
        static bool init = false;
        if (!init) {
            const uint32_t poly = 0x82F63B78u;
            for (uint32_t i = 0; i < 256; ++i) {
                uint32_t c = i;
                for (int k = 0; k < 8; ++k) {
                    c = (c & 1u) ? (c >> 1) ^ poly : (c >> 1);
                }
                table[i] = c;
            }
            init = true;
        }
        uint32_t crc = 0xFFFFFFFFu;
        for (size_t i = 0; i < len; ++i) {
            crc = (crc >> 8) ^ table[(crc ^ data[i]) & 0xFFu];
        }
        return crc ^ 0xFFFFFFFFu;
    }

    // --------------------------- Encoder ---------------------------

    static inline void encode_inst_section(const frontend::Program& prog, std::vector<uint8_t>& out) {
        // INST magic
        push_str(out, "INST");
        // instr_count
        push_uleb128(out, static_cast<uint64_t>(prog.instrs.size()));
        // encode instructions
        for (const auto& I : prog.instrs) {
            // Opcodes in frontend::Opcode are already aligned with spec
            out.push_back(static_cast<uint8_t>(I.op));
            uint8_t mask = 0;
            if (I.a >= 0) mask |= 1u << 0;
            if (I.b >= 0) mask |= 1u << 1;
            if (I.c >= 0) mask |= 1u << 2;
            if (I.has_angle0) mask |= 1u << 3;
            out.push_back(mask);
            if (I.a >= 0) push_uleb128(out, static_cast<uint64_t>(I.a));
            if (I.b >= 0) push_uleb128(out, static_cast<uint64_t>(I.b));
            if (I.c >= 0) push_uleb128(out, static_cast<uint64_t>(I.c));
            if (I.has_angle0) {
                // tag for angle_0: 0 = f32, 1 = param_ref. MVP writes literal f32.
                out.push_back(0);
                push_f32_le(out, I.angle0);
            }
        }
    }

    static inline std::vector<uint8_t> encode_qbin_min(const frontend::Program& prog) {
        // Build INST payload
        std::vector<uint8_t> inst;
        encode_inst_section(prog, inst);

        // Layout
        const uint32_t header_size = 24;
        const uint32_t section_count = 1;
        const uint32_t section_table_offset = header_size;
        const uint32_t section_table_size = section_count * 16; // one entry

        // Header without CRC
        std::vector<uint8_t> header;
        push_str(header, "QBIN");              // 0x00
        header.push_back(1);                   // major
        header.push_back(0);                   // minor
        header.push_back(0);                   // flags (LE, no table hash)
        header.push_back(static_cast<uint8_t>(header_size)); // header size
        push_u32_le(header, section_count);    // count
        push_u32_le(header, section_table_offset);
        push_u32_le(header, section_table_size);
        // CRC32C over 0x00..0x13
        uint32_t crc = crc32c(header.data(), header.size());
        push_u32_le(header, crc);

        // Section table entry for INST
        std::vector<uint8_t> table;
        // Section ID "INST" as u32 little-endian
        uint32_t inst_id = 0;
        std::memcpy(&inst_id, "INST", 4);
        push_u32_le(table, inst_id);
        push_u32_le(table, section_table_offset + section_table_size); // first payload @ offset 40
        push_u32_le(table, static_cast<uint32_t>(inst.size()));
        push_u32_le(table, 0); // flags

        // Assemble file
        std::vector<uint8_t> blob;
        blob.reserve(header.size() + table.size() + inst.size());
        blob.insert(blob.end(), header.begin(), header.end());
        blob.insert(blob.end(), table.begin(), table.end());
        blob.insert(blob.end(), inst.begin(), inst.end()); // write raw payload

        return blob;
    }

    // --------------------------- Public API ---------------------------

    std::vector<uint8_t> compile_qasm_to_qbin_min(const std::string& qasm_text, bool verbose) {
        frontend::Program prog = frontend::parse_qasm_subset(qasm_text, verbose);
        return encode_qbin_min(prog);
    }

} // namespace qbin_compiler
