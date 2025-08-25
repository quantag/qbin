#ifndef QBIN_COMPILER_QASM_FRONTEND_HPP
#define QBIN_COMPILER_QASM_FRONTEND_HPP

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace qbin_compiler {
    namespace frontend {

        // Core opcode values (aligned with spec draft)
        enum class Opcode : uint8_t {
            X = 0x01, Y = 0x02, Z = 0x03, H = 0x04,
            S = 0x05, SDG = 0x06, T = 0x07, TDG = 0x08,
            SX = 0x09, SXDG = 0x0A,
            RX = 0x0B, RY = 0x0C, RZ = 0x0D, PHASE = 0x0E,
            U = 0x0F,
            CX = 0x10, CZ = 0x11, ECR = 0x12, SWAP = 0x13, CSX = 0x14,
            CRX = 0x15, CRY = 0x16, CRZ = 0x17, CU = 0x18,
            RXX = 0x20, RYY = 0x21, RZZ = 0x22,
            MEASURE = 0x30, RESET = 0x31, BARRIER = 0x32,
            DELAY = 0x38, FRAME = 0x39,
            // Structured control (MVP subset)
            IF_EQ = 0x81, IF_NEQ = 0x82, ENDIF = 0x8F,
            CALLG = 0x40
        };

        struct Instr {
            Opcode op;
            int a = -1;        // qubit a
            int b = -1;        // qubit b
            int c = -1;        // qubit c (unused here)

            // Angle slot 0
            bool has_angle0 = false;
            bool angle0_is_param = false; // reserved
            float angle0 = 0.0f;

            // Aux 32-bit payload (e.g., classical bit index for MEASURE/IF)
            bool has_aux = false;
            uint32_t aux_u32 = 0;

            // Small immediate byte (e.g., IF compare value 0/1)
            bool has_imm8 = false;
            uint8_t imm8 = 0;
        };

        struct Program {
            std::vector<Instr> instrs;
        };

        // Parse minimal OpenQASM subset used by the MVP compiler:
        //  - h/x/y/z/s/sdg/t/tdg/sx/sxdg q[i];
        //  - rx/ry/rz/phase(<angle>) q[i];
        //  - cx/cz/swap q[i], q[j];
        //  - c[k] = measure q[i];
        //  - if (c[k] == 1) { <single stmt>; }     (also supports != 0/1)
        Program parse_qasm_subset(std::string_view text, bool verbose);

    } // namespace frontend
} // namespace qbin_compiler

#endif // QBIN_COMPILER_QASM_FRONTEND_HPP
