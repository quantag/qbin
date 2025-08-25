#ifndef QBIN_DECOMPILER_DECOMPILER_HPP
#define QBIN_DECOMPILER_DECOMPILER_HPP

#include <cstdint>
#include <string>
#include <vector>

// Minimal reusable API for QBIN -> OpenQASM 3.0 decompilation (MVP).
// This library focuses on the v1 core: reads header, section table, and
// decodes the INST section into a QASM 3.0 string.
//
// Limitations in MVP:
// - No compression wrappers (CPRZ) handling.
// - No section-level checksums or signatures.
// - No EXTS or custom gate expansion; CALLG printed as comment.
//
// Returns true on success; on failure returns false and sets 'err'.

namespace qbin_decompiler {

// Decompile a QBIN file buffer into OpenQASM 3.0 text.
bool decode_qbin_to_qasm(const std::vector<uint8_t>& bytes,
                         std::string& qasm_out,
                         std::string& err,
                         bool verbose = false);

} // namespace qbin_decompiler

#endif // QBIN_DECOMPILER_DECOMPILER_HPP
