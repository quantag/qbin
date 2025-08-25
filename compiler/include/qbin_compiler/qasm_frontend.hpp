#ifndef QBIN_COMPILER_QASM_FRONTEND_HPP
#define QBIN_COMPILER_QASM_FRONTEND_HPP

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// ASCII-only header.
// Minimal OpenQASM (subset) frontend producing a normalized IR that can be
// consumed by the encoder. This IR mirrors the subset used by the MVP:
//   - 1-qubit gates: h, x, y, z, s, sdg, t, tdg, sx, sxdg
//   - param gates: rx(a), ry(a), rz(a), phase(a)
//   - 2-qubit gates: cx, cz, swap
// Declarations like OPENQASM/ qubit[]/ bit[] are ignored.
// Unknown lines are skipped (optionally logged in verbose mode).

namespace qbin_compiler {
namespace frontend {

enum class Opcode : uint8_t {
    X = 0x01, Y = 0x02, Z = 0x03, H = 0x04,
    S = 0x05, SDG = 0x06, T = 0x07, TDG = 0x08,
    SX = 0x09, SXDG = 0x0A,
    RX = 0x0B, RY = 0x0C, RZ = 0x0D, PHASE = 0x0E,
    U  = 0x0F, // reserved in this MVP
    CX = 0x10, CZ = 0x11, ECR = 0x12, SWAP = 0x13, CSX = 0x14,
    CRX = 0x15, CRY = 0x16, CRZ = 0x17, CU = 0x18,
    RXX = 0x20, RYY = 0x21, RZZ = 0x22,
    MEASURE = 0x30, RESET = 0x31, BARRIER = 0x32,
    DELAY = 0x38, FRAME = 0x39,
    CALLG = 0x40
};

struct Instr {
    Opcode op;
    int a = -1;        // qubit a
    int b = -1;        // qubit b
    int c = -1;        // qubit c (unused in MVP)
    bool has_angle0 = false;
    bool angle0_is_param = false; // MVP always false
    float angle0 = 0.0f;
};

struct Program {
    std::vector<Instr> instrs;
};

// Parse minimal QASM subset into IR. Unknown lines are skipped.
// If verbose=true, skipped lines are printed to stderr.
Program parse_qasm_subset(std::string_view text, bool verbose);

} // namespace frontend
} // namespace qbin_compiler

#endif // QBIN_COMPILER_QASM_FRONTEND_HPP

