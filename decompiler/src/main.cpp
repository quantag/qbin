// main.cpp — Minimal QBIN -> OpenQASM 3.0 decompiler (MVP)
// Reads a QBIN file, decodes the INST section (subset), and writes QASM text.
// Standalone: no external deps. Matches the MVP encoder format.
//
// Usage:
//   qbin-decompile input.qbin -o output.qasm [--verbose]
//
// Supported opcodes: X,Y,Z,H,S,SDG,T,TDG,SX,SXDG,RX/RY/RZ/PHASE (angle literal),
// CX,CZ,SWAP, RXX/RYY/RZZ, MEASURE, RESET, BARRIER, DELAY (comment), FRAME (comment).
// Unknown opcodes are emitted as comments.
//
// Limitations: No compression, checksums, or EXTS handling in MVP.

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

uint32_t rd_u32le(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

bool read_file(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    std::streamoff n = f.tellg();
    if (n < 0) return false;
    out.resize(static_cast<size_t>(n));
    f.seekg(0, std::ios::beg);
    f.read(reinterpret_cast<char*>(out.data()), n);
    return static_cast<size_t>(f.gcount()) == static_cast<size_t>(n);
}

// ULEB128 decode; returns value and advances index; on error, returns false.
bool read_uleb128(const std::vector<uint8_t>& b, size_t& i, uint64_t& v) {
    v = 0;
    int shift = 0;
    const int maxshift = 63; // guard
    while (i < b.size()) {
        uint8_t byte = b[i++];
        v |= (uint64_t)(byte & 0x7F) << shift;
        if ((byte & 0x80) == 0) return true;
        shift += 7;
        if (shift > maxshift) return false;
    }
    return false;
}

bool read_f32le(const std::vector<uint8_t>& b, size_t& i, float& out) {
    if (i + 4 > b.size()) return false;
    uint32_t u = rd_u32le(&b[i]);
    i += 4;
    std::memcpy(&out, &u, 4);
    return true;
}

struct SectionEntry {
    uint32_t id;
    uint32_t offset;
    uint32_t size;
    uint32_t flags;
};

bool decode_header_and_table(const std::vector<uint8_t>& b,
                             uint8_t& major, uint8_t& minor,
                             uint32_t& table_off, uint32_t& table_size,
                             std::vector<SectionEntry>& table,
                             std::string& err) {
    if (b.size() < 24) { err = "file too small for header"; return false; }
    if (std::memcmp(b.data(), "QBIN", 4) != 0) { err = "bad magic"; return false; }
    major = b[4];
    minor = b[5];
    uint8_t flags = b[6];
    (void)flags; // MVP ignores
    uint8_t hdr_size = b[7];
    if (hdr_size != 24) { err = "unexpected header size"; return false; }
    uint32_t section_count = rd_u32le(&b[8]);
    table_off = rd_u32le(&b[12]);
    table_size = rd_u32le(&b[16]);
    if (table_off + table_size > b.size()) { err = "section table OOB"; return false; }
    if (table_size != section_count * 16) { err = "table size mismatch"; return false; }
    table.clear();
    table.reserve(section_count);
    size_t p = table_off;
    for (uint32_t i = 0; i < section_count; ++i) {
        SectionEntry e;
        e.id     = rd_u32le(&b[p + 0]);
        e.offset = rd_u32le(&b[p + 4]);
        e.size   = rd_u32le(&b[p + 8]);
        e.flags  = rd_u32le(&b[p + 12]);
        if ((size_t)e.offset + (size_t)e.size > b.size()) { err = "section out of bounds"; return false; }
        table.push_back(e);
        p += 16;
    }
    return true;
}

std::string id_to_ascii(uint32_t id) {
    char s[5];
    s[0] = char(id & 0xFF);
    s[1] = char((id >> 8) & 0xFF);
    s[2] = char((id >> 16) & 0xFF);
    s[3] = char((id >> 24) & 0xFF);
    s[4] = 0;
    return std::string(s);
}

struct DecodedInstr {
    uint8_t opcode = 0;
    int a = -1, b = -1, c = -1;
    bool has_angle0 = false;
    float angle0 = 0.0f;
    bool has_aux = false;
    uint32_t aux = 0; // used for bit index or duration
};

bool decode_inst_section(const std::vector<uint8_t>& b, size_t off, size_t size,
                         std::vector<DecodedInstr>& out, std::string& err) {
    if (off + size > b.size()) { err = "INST OOB"; return false; }
    size_t i = off;
    if (i + 4 > off + size) { err = "short INST"; return false; }
    if (std::memcmp(&b[i], "INST", 4) != 0) { err = "INST magic missing"; return false; }
    i += 4;
    uint64_t n = 0;
    if (!read_uleb128(b, i, n)) { err = "bad instr_count"; return false; }
    out.clear();
    out.reserve((size_t)n);
    for (uint64_t k = 0; k < n; ++k) {
        if (i + 2 > off + size) { err = "truncated instruction header"; return false; }
        DecodedInstr di{};
        di.opcode = b[i++];
        uint8_t mask = b[i++];
        if (mask & (1u<<0)) { uint64_t v; if (!read_uleb128(b, i, v)) { err="bad a"; return false; } di.a = (int)v; }
        if (mask & (1u<<1)) { uint64_t v; if (!read_uleb128(b, i, v)) { err="bad b"; return false; } di.b = (int)v; }
        if (mask & (1u<<2)) { uint64_t v; if (!read_uleb128(b, i, v)) { err="bad c"; return false; } di.c = (int)v; }
        if (mask & (1u<<3)) {
            if (i >= off + size) { err = "angle tag OOB"; return false; }
            uint8_t tag = b[i++];
            if (tag == 0) {
                float ang;
                if (!read_f32le(b, i, ang)) { err = "angle f32 OOB"; return false; }
                di.has_angle0 = true; di.angle0 = ang;
            } else if (tag == 1) {
                // param_ref in MVP unsupported; skip varint but note
                uint64_t dummy; if (!read_uleb128(b, i, dummy)) { err = "param_ref OOB"; return false; }
                di.has_angle0 = true; di.angle0 = 0.0f; // placeholder
            } else {
                err = "unknown angle tag"; return false;
            }
        }
        if (mask & (1u<<7)) {
            if (i + 4 > off + size) { err = "aux OOB"; return false; }
            di.has_aux = true;
            di.aux = rd_u32le(&b[i]); i += 4;
        }
        out.push_back(di);
    }
    return true;
}

std::string opcode_name(uint8_t op) {
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
        default:   return "unknown";
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " input.qbin [-o output.qasm] [--verbose]\n";
        return 1;
    }
    std::string in_path, out_path;
    bool verbose = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-o" && i + 1 < argc) {
            out_path = argv[++i];
        } else if (a == "--verbose" || a == "-v") {
            verbose = true;
        } else if (!a.empty() && a[0] != '-') {
            in_path = a;
        } else {
            std::cerr << "Unknown option: " << a << "\n";
            return 1;
        }
    }
    if (in_path.empty()) {
        std::cerr << "No input file provided.\n";
        return 1;
    }

    std::vector<uint8_t> buf;
    if (!read_file(in_path, buf)) {
        std::cerr << "Failed to read: " << in_path << "\n";
        return 1;
    }

    uint8_t major=0, minor=0;
    uint32_t table_off=0, table_size=0;
    std::vector<SectionEntry> table;
    std::string err;
    if (!decode_header_and_table(buf, major, minor, table_off, table_size, table, err)) {
        std::cerr << "Header decode error: " << err << "\n";
        return 1;
    }
    if (verbose) {
        std::cerr << "QBIN v" << int(major) << "." << int(minor) << ", sections=" << (table_size/16) << "\n";
        for (const auto& e : table) {
            std::cerr << "  [" << id_to_ascii(e.id) << "] off=" << e.offset << " size=" << e.size << " flags=" << e.flags << "\n";
        }
    }

    // Find INST
    uint32_t inst_id = 0; std::memcpy(&inst_id, "INST", 4);
    const SectionEntry* inst = nullptr;
    for (const auto& e : table) { if (e.id == inst_id) { inst = &e; break; } }
    if (!inst) {
        std::cerr << "No INST section found.\n";
        return 1;
    }

    std::vector<DecodedInstr> instrs;
    if (!decode_inst_section(buf, inst->offset, inst->size, instrs, err)) {
        std::cerr << "INST decode error: " << err << "\n";
        return 1;
    }

    // Infer qubit/bit counts
    int max_q = -1, max_c = -1;
    for (const auto& di : instrs) {
        max_q = std::max(max_q, di.a);
        max_q = std::max(max_q, di.b);
        max_q = std::max(max_q, di.c);
        if (di.opcode == 0x30 /*MEASURE*/ && di.has_aux) {
            max_c = std::max(max_c, int(di.aux));
        }
    }
    int num_qubits = (max_q >= 0) ? (max_q + 1) : 0;
    int num_bits   = (max_c >= 0) ? (max_c + 1) : 0;

    // Emit QASM 3.0
    std::ostringstream q;
    q << "OPENQASM 3.0;\n";
    if (num_qubits > 0) q << "qubit[" << num_qubits << "] q;\n";
    if (num_bits   > 0) q << "bit[" << num_bits   << "] c;\n";
    q << "\n";

    q << std::setprecision(9);
    for (const auto& di : instrs) {
        std::string name = opcode_name(di.opcode);
        switch (di.opcode) {
            case 0x01: case 0x02: case 0x03: case 0x04:
            case 0x05: case 0x06: case 0x07: case 0x08:
            case 0x09: case 0x0A:
                q << name << " q[" << di.a << "];\n";
                break;
            case 0x0B: case 0x0C: case 0x0D: case 0x0E: { // RX/RY/RZ/PHASE
                float ang = di.has_angle0 ? di.angle0 : 0.0f;
                q << name << "(" << ang << ") q[" << di.a << "];\n";
                break;
            }
            case 0x10: case 0x11: case 0x12: case 0x13: case 0x14:
                q << name << " q[" << di.a << "], q[" << di.b << "];\n";
                break;
            case 0x15: case 0x16: case 0x17: { // CRX/CRY/CRZ
                float ang = di.has_angle0 ? di.angle0 : 0.0f;
                q << name << " q[" << di.a << "], q[" << di.b << "], (" << ang << ");\n";
                break;
            }
            case 0x18: { // CU (angles a0,a1,a2 condensed)
                float ang = di.has_angle0 ? di.angle0 : 0.0f;
                q << name << " q[" << di.a << "], q[" << di.b << "], (" << ang << "); // angles truncated in MVP\n";
                break;
            }
            case 0x20: case 0x21: case 0x22:
                q << name << "(" << (di.has_angle0 ? di.angle0 : 0.0f) << ") q[" << di.a << "], q[" << di.b << "];\n";
                break;
            case 0x30: { // MEASURE
                int bit = di.has_aux ? int(di.aux) : 0;
                q << "c[" << bit << "] = measure q[" << di.a << "];\n";
                break;
            }
            case 0x31: // RESET
                q << "reset q[" << di.a << "];\n";
                break;
            case 0x32: // BARRIER
                q << "barrier;\n";
                break;
            case 0x38: // DELAY
                if (di.has_aux) q << "// delay " << di.aux << " ns on q[" << di.a << "]\n";
                else q << "// delay on q[" << di.a << "]\n";
                break;
            case 0x39: // FRAME
                q << "// frame " << (di.has_angle0 ? di.angle0 : 0.0f) << " on q[" << di.a << "]\n";
                break;
            default:
                q << "// unknown opcode 0x" << std::hex << int(di.opcode) << std::dec << "\n";
                break;
        }
    }

    // Write output
    if (out_path.empty()) {
        std::cout << q.str();
    } else {
        std::ofstream ofs(out_path, std::ios::binary);
        if (!ofs) { std::cerr << "Cannot open output: " << out_path << "\n"; return 1; }
        auto s = q.str();
        ofs.write(s.data(), static_cast<std::streamsize>(s.size()));
        if (!ofs) { std::cerr << "Write failed\n"; return 1; }
    }

    return 0;
}
