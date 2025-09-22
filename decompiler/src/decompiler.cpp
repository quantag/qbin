#include "qbin_decompiler/decompiler.hpp"
#include "qbin_decompiler/tools.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace qbin_decompiler {

    struct DecodedInstr {
        uint8_t opcode = 0;
        int a = -1, b = -1, c = -1;
        bool has_angle0 = false;
        float angle0 = 0.0f;
        bool has_aux = false;
        uint32_t aux = 0;
        bool has_imm8 = false;
        uint8_t imm8 = 0;
    };

    // decode INST section (kept internal)
    static bool decode_inst_section(const std::vector<uint8_t>& b, size_t off, size_t size,
        std::vector<DecodedInstr>& out, std::string& err, bool verbose = false) {

        using namespace qbin_decompiler::tools;

        if (off + size > b.size()) { err = "INST OOB"; return false; }
        size_t i = off, end = off + size;
        if (i + 4 > end) { err = "short INST"; return false; }
        if (std::memcmp(&b[i], "INST", 4) != 0) { err = "INST magic missing"; return false; }
        i += 4;

        uint64_t n = 0;
        if (!read_uleb128_bound(b, i, end, n)) { err = "bad instr_count"; return false; }
        out.clear();
        out.reserve((size_t)n);

        for (uint64_t k = 0; k < n; ++k) {
            if (i + 2 > end) { err = "truncated instruction header"; return false; }
            DecodedInstr di{};
            di.opcode = b[i++];
            uint8_t mask = b[i++];

            if (verbose) std::fprintf(stderr, "idx=%llu: op=0x%02X mask=0x%02X @%zu\n",
                (unsigned long long)k, di.opcode, mask, i);

            // a, b, c
            if (mask & (1u << 0)) { uint64_t v; if (!read_uleb128_bound(b, i, end, v)) { err = "bad a (idx=" + std::to_string(k) + ")"; return false; } di.a = (int)v; }
            if (mask & (1u << 1)) { uint64_t v; if (!read_uleb128_bound(b, i, end, v)) { err = "bad b (idx=" + std::to_string(k) + ")"; return false; } di.b = (int)v; }
            if (mask & (1u << 2)) { uint64_t v; if (!read_uleb128_bound(b, i, end, v)) { err = "bad c (idx=" + std::to_string(k) + ")"; return false; } di.c = (int)v; }

            // angle_0
            if (mask & (1u << 3)) {
                if (i >= end) { err = "angle tag OOB"; return false; }
                uint8_t tag = b[i++];
                if (tag == 0) {
                    float ang; if (!read_f32le_bound(b, i, end, ang)) { err = "angle f32 OOB"; return false; }
                    di.has_angle0 = true; di.angle0 = ang;
                }
                else if (tag == 1) {
                    uint64_t dummy; if (!read_uleb128_bound(b, i, end, dummy)) { err = "angle param_ref OOB"; return false; }
                    di.has_angle0 = true; di.angle0 = 0.0f;
                }
                else {
                    err = "unknown angle tag"; return false;
                }
            }

            // aux_u32
            if (mask & (1u << 7)) {
                if (i + 4 > end) { err = "aux OOB"; return false; }
                di.has_aux = true; di.aux = rd_u32le(&b[i]); i += 4;
            }

            // IF imm8
            if (di.opcode == 0x81 || di.opcode == 0x82) {
                if (i >= end) { err = "if imm8 OOB"; return false; }
                di.has_imm8 = true; di.imm8 = b[i++];
            }

            out.push_back(di);
        }
        return true;
    }

    static inline std::string opcode_name(uint8_t op) {
        switch (op) {
        case 0x01: return "x";
        case 0x02: return "y";
        case 0x03: return "z";
        case 0x04: return "h";
        case 0x05: return "s";
        case 0x06: return "sdg";
        case 0x07: return "t";
        case 0x08: return "tdg";
        case 0x09: return "sx";
        case 0x0A: return "sxdg";
        case 0x0B: return "rx";
        case 0x0C: return "ry";
        case 0x0D: return "rz";
        case 0x0E: return "phase";
        case 0x0F: return "u";
        case 0x10: return "cx";
        case 0x11: return "cz";
        case 0x12: return "ecr";
        case 0x13: return "swap";
        case 0x14: return "csx";
        case 0x15: return "crx";
        case 0x16: return "cry";
        case 0x17: return "crz";
        case 0x18: return "cu";
        case 0x20: return "rxx";
        case 0x21: return "ryy";
        case 0x22: return "rzz";
        case 0x30: return "measure";
        case 0x31: return "reset";
        case 0x32: return "barrier";
        case 0x38: return "delay";
        case 0x39: return "frame";
        case 0x40: return "callg";
        case 0x81: return "if_eq";
        case 0x82: return "if_neq";
        case 0x8F: return "endif";
        default:   return "unknown";
        }
    }

    bool decode_qbin_to_qasm(const std::vector<uint8_t>& buf,
        std::string& qasm_out,
        std::string& err,
        bool verbose) {

        using namespace qbin_decompiler::tools;

        // Read v1 header (20 bytes), no section table
        FileHeader hdr{};
        size_t pos = 0;
        if (!read_header_v1(buf, pos, hdr, err, verbose)) {
            return false;
        }

        // After header, the single INST section/tag starts immediately and runs to EOF.
        if (pos + 4 > buf.size()) { err = "no INST tag after header"; return false; }
        if (std::memcmp(&buf[pos], "INST", 4) != 0) {
            err = "expected INST tag after header";
            return false;
        }
        size_t inst_off = pos;
        size_t inst_size = buf.size() - inst_off;

        std::vector<DecodedInstr> instrs;
        if (!decode_inst_section(buf, inst_off, inst_size, instrs, err, verbose)) {
            return false;
        }

        // Infer register sizes
        int max_q = -1, max_c = -1;
        for (const auto& di : instrs) {
            max_q = std::max(max_q, di.a);
            max_q = std::max(max_q, di.b);
            max_q = std::max(max_q, di.c);
            if (di.opcode == 0x30 /*measure*/ && di.has_aux) max_c = std::max(max_c, int(di.aux));
            if ((di.opcode == 0x81 || di.opcode == 0x82) && di.has_aux) max_c = std::max(max_c, int(di.aux));
        }
        int num_qubits = (max_q >= 0) ? (max_q + 1) : 0;
        int num_bits = (max_c >= 0) ? (max_c + 1) : 0;

        // Emit QASM
        std::ostringstream q;
        q << "OPENQASM 3.0;\n";
        if (num_qubits > 0) q << "qubit[" << num_qubits << "] q;\n";
        if (num_bits > 0)   q << "bit[" << num_bits << "] c;\n";
        q << "\n";

        q << std::setprecision(9);
        for (size_t idx = 0; idx < instrs.size(); ++idx) {
            const auto& di = instrs[idx];
            switch (di.opcode) {
            case 0x01: q << "x q[" << di.a << "];\n"; break;
            case 0x02: q << "y q[" << di.a << "];\n"; break;
            case 0x03: q << "z q[" << di.a << "];\n"; break;
            case 0x04: q << "h q[" << di.a << "];\n"; break;
            case 0x05: q << "s q[" << di.a << "];\n"; break;
            case 0x06: q << "sdg q[" << di.a << "];\n"; break;
            case 0x07: q << "t q[" << di.a << "];\n"; break;
            case 0x08: q << "tdg q[" << di.a << "];\n"; break;
            case 0x09: q << "sx q[" << di.a << "];\n"; break;
            case 0x0A: q << "sxdg q[" << di.a << "];\n"; break;
            case 0x0B: q << "rx(" << (di.has_angle0 ? di.angle0 : 0.0f) << ") q[" << di.a << "];\n"; break;
            case 0x0C: q << "ry(" << (di.has_angle0 ? di.angle0 : 0.0f) << ") q[" << di.a << "];\n"; break;
            case 0x0D: q << "rz(" << (di.has_angle0 ? di.angle0 : 0.0f) << ") q[" << di.a << "];\n"; break;
            case 0x0E: q << "phase(" << (di.has_angle0 ? di.angle0 : 0.0f) << ") q[" << di.a << "];\n"; break;
            case 0x10: q << "cx q[" << di.a << "], q[" << di.b << "];\n"; break;
            case 0x11: q << "cz q[" << di.a << "], q[" << di.b << "];\n"; break;
            case 0x13: q << "swap q[" << di.a << "], q[" << di.b << "];\n"; break;
            case 0x15: q << "crx(" << (di.has_angle0 ? di.angle0 : 0.0f) << ") q[" << di.a << "], q[" << di.b << "];\n"; break;
            case 0x16: q << "cry(" << (di.has_angle0 ? di.angle0 : 0.0f) << ") q[" << di.a << "], q[" << di.b << "];\n"; break;
            case 0x17: q << "crz(" << (di.has_angle0 ? di.angle0 : 0.0f) << ") q[" << di.a << "], q[" << di.b << "];\n"; break;

            case 0x20: q << "rxx(" << (di.has_angle0 ? di.angle0 : 0.0f) << ") q[" << di.a << "], q[" << di.b << "];\n"; break;
            case 0x21: q << "ryy(" << (di.has_angle0 ? di.angle0 : 0.0f) << ") q[" << di.a << "], q[" << di.b << "];\n"; break;
            case 0x22: q << "rzz(" << (di.has_angle0 ? di.angle0 : 0.0f) << ") q[" << di.a << "], q[" << di.b << "];\n"; break;
            case 0x30: q << "c[" << (di.has_aux ? int(di.aux) : 0) << "] = measure q[" << di.a << "];\n"; break;
            case 0x31: q << "reset q[" << di.a << "];\n"; break;
            case 0x32: q << "barrier;\n"; break;
            case 0x81:
            case 0x82: {
                int val = di.has_imm8 ? di.imm8 : 0;
                // Try single-instruction inline if followed by body + endif
                if (idx + 2 < instrs.size() && instrs[idx + 2].opcode == 0x8F) {
                    const auto& body = instrs[idx + 1];
                    std::ostringstream one;
                    switch (body.opcode) {
                    case 0x01: one << "x q[" << body.a << "];"; break;
                    case 0x02: one << "y q[" << body.a << "];"; break;
                    case 0x03: one << "z q[" << body.a << "];"; break;
                    case 0x04: one << "h q[" << body.a << "];"; break;
                    case 0x05: one << "s q[" << body.a << "];"; break;
                    case 0x06: one << "sdg q[" << body.a << "];"; break;
                    case 0x07: one << "t q[" << body.a << "];"; break;
                    case 0x08: one << "tdg q[" << body.a << "];"; break;
                    case 0x09: one << "sx q[" << body.a << "];"; break;
                    case 0x0A: one << "sxdg q[" << body.a << "];"; break;
                    case 0x0B: one << "rx(" << (body.has_angle0 ? body.angle0 : 0.0f) << ") q[" << body.a << "];"; break;
                    case 0x0C: one << "ry(" << (body.has_angle0 ? body.angle0 : 0.0f) << ") q[" << body.a << "];"; break;
                    case 0x0D: one << "rz(" << (body.has_angle0 ? body.angle0 : 0.0f) << ") q[" << body.a << "];"; break;
                    case 0x10: one << "cx q[" << body.a << "], q[" << body.b << "];"; break;
                    case 0x13: one << "swap q[" << body.a << "], q[" << body.b << "];"; break;
                    case 0x30: one << "c[" << (body.has_aux ? int(body.aux) : 0) << "] = measure q[" << body.a << "];"; break;
                    default: break;
                    }
                    std::string bs = one.str();
                    if (!bs.empty()) {
                        q << "if (c[" << di.aux << "] " << (di.opcode == 0x81 ? "==" : "!=") << " " << val << ") { " << bs << " }\n";
                        idx += 2;
                        break;
                    }
                }
                // Fallback multi-line
                q << "if (c[" << di.aux << "] " << (di.opcode == 0x81 ? "==" : "!=") << " " << val << ") {\n";
                size_t j = idx + 1;
                for (; j < instrs.size(); ++j) {
                    if (instrs[j].opcode == 0x8F) break;
                    const auto& body = instrs[j];
                    q << "  " << opcode_name(body.opcode) << " ...\n";
                }
                q << "}\n";
                idx = (j < instrs.size()) ? j : instrs.size() - 1;
                break;
            }
            case 0x8F: /* endif */ break;
            default:
                q << "// unknown opcode 0x" << std::hex << int(di.opcode) << std::dec << "\n";
                break;
            }
        }

        qasm_out = q.str();
        return true;
    }

} // namespace qbin_decompiler
