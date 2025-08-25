#include "qbin_compiler/qasm_frontend.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace qbin_compiler {

    // ---------- low-level writers ----------

    static inline void push_u8(std::vector<unsigned char>& out, uint8_t v) {
        out.push_back(v);
    }

    static inline void push_u32le(std::vector<unsigned char>& out, uint32_t v) {
        out.push_back(static_cast<unsigned char>(v & 0xFF));
        out.push_back(static_cast<unsigned char>((v >> 8) & 0xFF));
        out.push_back(static_cast<unsigned char>((v >> 16) & 0xFF));
        out.push_back(static_cast<unsigned char>((v >> 24) & 0xFF));
    }

    static inline void push_bytes(std::vector<unsigned char>& out, const void* data, size_t len) {
        const unsigned char* p = static_cast<const unsigned char*>(data);
        out.insert(out.end(), p, p + len);
    }

    static inline void push_f32le(std::vector<unsigned char>& out, float f) {
        static_assert(sizeof(float) == 4, "float must be 32-bit");
        uint32_t u;
        std::memcpy(&u, &f, 4);
        push_u32le(out, u);
    }

    // Unsigned LEB128
    static inline void push_uleb128(std::vector<unsigned char>& out, uint64_t val) {
        while (true) {
            uint8_t byte = static_cast<uint8_t>(val & 0x7F);
            val >>= 7;
            if (val != 0) {
                byte |= 0x80;
                out.push_back(byte);
            }
            else {
                out.push_back(byte);
                break;
            }
        }
    }

    // ---------- INST section encoder ----------

    static void encode_inst_section(const frontend::Program& prog, std::vector<unsigned char>& out) {
        using frontend::Instr;
        using frontend::Op;

        const uint32_t section_id = static_cast<uint32_t>('I')
            | (static_cast<uint32_t>('N') << 8)
            | (static_cast<uint32_t>('S') << 16)
            | (static_cast<uint32_t>('T') << 24);

        // Build payload in temp buffer
        std::vector<unsigned char> payload;
        payload.reserve(16 + prog.code.size() * 8);

        // "INST"
        payload.push_back('I');
        payload.push_back('N');
        payload.push_back('S');
        payload.push_back('T');

        // Count
        push_uleb128(payload, static_cast<uint64_t>(prog.code.size()));

        // Encode instructions
        for (const auto& I : prog.code) {
            uint8_t opcode = static_cast<uint8_t>(I.op);
            uint8_t mask = 0;
            if (I.a >= 0)      mask |= 0x01;
            if (I.b >= 0)      mask |= 0x02;
            if (I.c >= 0)      mask |= 0x04;
            if (I.has_angle)   mask |= 0x08;
            if (I.has_aux)     mask |= 0x80;

            push_u8(payload, opcode);
            push_u8(payload, mask);

            if (mask & 0x01) push_uleb128(payload, static_cast<uint64_t>(I.a));
            if (mask & 0x02) push_uleb128(payload, static_cast<uint64_t>(I.b));
            if (mask & 0x04) push_uleb128(payload, static_cast<uint64_t>(I.c));

            if (mask & 0x08) {
                // angle tag + payload; tag 0 means f32
                push_u8(payload, 0x00);
                push_f32le(payload, I.angle);
            }

            if (mask & 0x80) {
                // aux u32 (e.g., classical bit index)
                push_u32le(payload, I.aux);
            }

            // IF ops carry an extra imm8 after aux
            if (opcode == static_cast<uint8_t>(Op::IF_EQ) ||
                opcode == static_cast<uint8_t>(Op::IF_NEQ)) {
                push_u8(payload, I.has_imm8 ? I.imm8 : 0);
            }
        }

        // File header
        const uint32_t version = 1;
        const uint32_t section_count = 1;
        const uint32_t table_entry_size = 16;
        const uint32_t table_size = section_count * table_entry_size;
        const uint32_t header_size = 24;

        const uint32_t inst_offset = header_size;
        const uint32_t inst_size = static_cast<uint32_t>(payload.size());
        const uint32_t table_offset = header_size + inst_size;

        out.clear();
        out.reserve(header_size + inst_size + table_size);

        // "QBIN"
        out.push_back('Q'); out.push_back('B'); out.push_back('I'); out.push_back('N');

        // header fields
        push_u32le(out, version);
        push_u32le(out, section_count);
        push_u32le(out, table_offset);
        push_u32le(out, table_size);
        push_u32le(out, 0u); // flags/reserved

        // payload
        push_bytes(out, payload.data(), payload.size());

        // section table (one entry)
        push_u32le(out, section_id);
        push_u32le(out, inst_offset);
        push_u32le(out, inst_size);
        push_u32le(out, 0u); // flags
    }

    // ---------- public API used by main.cpp ----------

    // This provides the missing symbol main.cpp expects.
    std::vector<unsigned char> compile_qasm_to_qbin_min(const std::string& qasm_text, bool verbose) {
        auto prog = frontend::parse_qasm_subset(qasm_text, verbose);
        std::vector<unsigned char> bytes;
        encode_inst_section(prog, bytes);
        return bytes;
    }

    // Convenience helper if you want a file-to-file path somewhere else.
    int compile_file_to_file(const std::string& in_path, const std::string& out_path, bool verbose) {
        std::ifstream ifs(in_path, std::ios::binary);
        if (!ifs) {
            return 1; // cannot open input
        }
        std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

        auto bytes = compile_qasm_to_qbin_min(content, verbose);

        std::ofstream ofs(out_path, std::ios::binary);
        if (!ofs) return 2; // cannot open output
        ofs.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
        if (!ofs) return 3; // write error
        return 0;
    }

} // namespace qbin_compiler
