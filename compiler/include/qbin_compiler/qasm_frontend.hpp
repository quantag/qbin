#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace qbin_compiler {
    namespace frontend {

        enum class Op : uint8_t {
            X = 0x01, Y = 0x02, Z = 0x03, H = 0x04, S = 0x05, SDG = 0x06, T = 0x07, TDG = 0x08, SX = 0x09, SXDG = 0x0A,
            RX = 0x0B, RY = 0x0C, RZ = 0x0D, PHASE = 0x0E,
            CX = 0x10, CZ = 0x11, ECR = 0x12, SWAP = 0x13, CSX = 0x14, CRX = 0x15, CRY = 0x16, CRZ = 0x17, CU = 0x18,
            RXX = 0x20, RYY = 0x21, RZZ = 0x22,
            MEASURE = 0x30, RESET = 0x31, BARRIER = 0x32, DELAY = 0x38, FRAME = 0x39, CALLG = 0x40,
            IF_EQ = 0x81, IF_NEQ = 0x82, ENDIF = 0x8F
        };

        struct Instr {
            Op op;
            int a = -1, b = -1, c = -1;      // qubit indices
            bool has_angle = false; float angle = 0.0f;  // for single-angle ops (rx/ry/rz/phase)
            bool has_aux = false; uint32_t aux = 0;     // for measure bit index or IF bit index
            bool has_imm8 = false; uint8_t imm8 = 0;    // for IF compare constant
        };

        struct Program {
            std::vector<Instr> code;
            int max_qubit = -1;
            int max_bit = -1;
        };

        // Auto-detects QASM 2.0 or 3.0 and parses into our canonical IR.
        // Supports QASM2 'gate' definitions with parameter substitution and 'u' decomposition.
        Program parse_qasm_subset(std::string_view text, bool verbose = false);

    } // namespace frontend
} // namespace qbin_compiler
