#include "qbin_compiler/qasm_frontend.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

    // ---- little-endian writers ----
    inline void push_u8(std::vector<unsigned char>& out, uint8_t v) {
        out.push_back(v);
    }
    inline void push_u16(std::vector<unsigned char>& out, uint16_t v) {
        out.push_back(static_cast<unsigned char>(v & 0xFF));
        out.push_back(static_cast<unsigned char>((v >> 8) & 0xFF));
    }
    inline void push_u32(std::vector<unsigned char>& out, uint32_t v) {
        out.push_back(static_cast<unsigned char>(v & 0xFF));
        out.push_back(static_cast<unsigned char>((v >> 8) & 0xFF));
        out.push_back(static_cast<unsigned char>((v >> 16) & 0xFF));
        out.push_back(static_cast<unsigned char>((v >> 24) & 0xFF));
    }
    inline void push_bytes(std::vector<unsigned char>& out, const void* data, size_t len) {
        const auto* p = static_cast<const unsigned char*>(data);
        out.insert(out.end(), p, p + len);
    }
    inline void push_f32le(std::vector<unsigned char>& out, float f) {
        static_assert(sizeof(float) == 4, "float must be 32-bit");
        uint32_t u;
        std::memcpy(&u, &f, 4);
        push_u32(out, u);
    }

    // Unsigned LEB128
    inline void push_uleb128(std::vector<unsigned char>& out, uint64_t val) {
        for (;;) {
            uint8_t byte = static_cast<uint8_t>(val & 0x7F);
            val >>= 7;
            if (val != 0) { byte |= 0x80; out.push_back(byte); }
            else { out.push_back(byte); break; }
        }
    }

    // ---- QBIN v1 fixed header (20 bytes) ----
    // Layout (little-endian):
    // 0:  'Q''B''I''N'
    // 4:  version   (u16) = 1
    // 6:  flags     (u16) = 0
    // 8:  header_sz (u32) = 20
    // 12: section_count (u32) = 1
    // 16: reserved  (u32) = 0
    inline void write_qbin_header(std::vector<unsigned char>& out,
        uint16_t version,
        uint16_t flags,
        uint32_t section_count) {
        constexpr uint32_t kHeaderSize = 20;
        out.push_back('Q'); out.push_back('B'); out.push_back('I'); out.push_back('N');
        push_u16(out, version);
        push_u16(out, flags);
        push_u32(out, kHeaderSize);
        push_u32(out, section_count);
        push_u32(out, 0u); // reserved
    }

} // namespace


namespace qbin_compiler {

    // ---- INST section payload encoder ----
    // Format:
    //   "INST"                      4 bytes
    //   instr_count                 uleb128
    //   repeated instr {
    //     opcode                    u8
    //     mask                      u8   (bit0=a, bit1=b, bit2=c, bit3=angle, bit7=aux)
    //     a,b,c                     uleb128 each if present
    //     if angle:  tag=0x00 u8,  angle_f32_le
    //     if aux:    aux_u32_le
    //     if opcode in {IF_EQ, IF_NEQ}: imm8 (one byte), after aux if aux present
    //   }
    static void encode_inst_payload(const frontend::Program& prog,
        std::vector<unsigned char>& payload) {
        using frontend::Op;

        payload.clear();
        payload.reserve(16 + prog.code.size() * 8);

        // Tag
        payload.push_back('I'); payload.push_back('N'); payload.push_back('S'); payload.push_back('T');

        // Count
        push_uleb128(payload, static_cast<uint64_t>(prog.code.size()));

        // Body
        for (const auto& I : prog.code) {
            const uint8_t opcode = static_cast<uint8_t>(I.op);
            uint8_t mask = 0;
            if (I.a >= 0)    mask |= 0x01;
            if (I.b >= 0)    mask |= 0x02;
            if (I.c >= 0)    mask |= 0x04;
            if (I.has_angle) mask |= 0x08;
            if (I.has_aux)   mask |= 0x80;

            push_u8(payload, opcode);
            push_u8(payload, mask);

            if (mask & 0x01) push_uleb128(payload, static_cast<uint64_t>(I.a));
            if (mask & 0x02) push_uleb128(payload, static_cast<uint64_t>(I.b));
            if (mask & 0x04) push_uleb128(payload, static_cast<uint64_t>(I.c));

            if (mask & 0x08) {
                // angle tag 0x00 = f32
                push_u8(payload, 0x00);
                push_f32le(payload, I.angle);
            }

            if (mask & 0x80) {
                push_u32(payload, I.aux);
            }

            if (opcode == static_cast<uint8_t>(Op::IF_EQ) ||
                opcode == static_cast<uint8_t>(Op::IF_NEQ)) {
                push_u8(payload, I.has_imm8 ? I.imm8 : 0);
            }
        }
    }

    // ---- public API used by main.cpp ----

    std::vector<unsigned char> compile_qasm_to_qbin_min(const std::string& qasm_text, bool verbose) {
        // 1) QASM -> IR
        auto prog = frontend::parse_qasm_subset(qasm_text, verbose);

        // 2) Build file
        std::vector<unsigned char> out;
        out.reserve(64);

        // Header (v1, flags=0, one section)
        write_qbin_header(out, /*version*/ 1, /*flags*/ 0, /*section_count*/ 1);

        // INST payload
        std::vector<unsigned char> inst_payload;
        encode_inst_payload(prog, inst_payload);
        out.insert(out.end(), inst_payload.begin(), inst_payload.end());

        return out;
    }

    int compile_file_to_file(const std::string& in_path, const std::string& out_path, bool verbose) {
        std::ifstream ifs(in_path, std::ios::binary);
        if (!ifs) return 1;
        std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

        auto bytes = compile_qasm_to_qbin_min(content, verbose);

        std::ofstream ofs(out_path, std::ios::binary);
        if (!ofs) return 2;
        ofs.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
        if (!ofs) return 3;
        return 0;
    }

} // namespace qbin_compiler
