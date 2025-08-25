#ifndef QBIN_COMPILER_COMPILER_HPP
#define QBIN_COMPILER_COMPILER_HPP

#include <cstdint>
#include <string>
#include <vector>

// ASCII-only header.
// Minimal public API for compiling OpenQASM (subset) to QBIN bytes.
// This MVP does not depend on libqbin. It is intended to be replaced by
// a full frontend + writer later, keeping the same function signature.

namespace qbin_compiler {

// Compile a subset of OpenQASM text into a QBIN blob.
// On success, returns the full .qbin file bytes.
// Notes:
//  - Unsupported statements are skipped (best-effort).
//  - The output contains a single INST section; optional sections (STRS, META, etc.)
//    are omitted in this MVP.
std::vector<uint8_t> compile_qasm_to_qbin_min(const std::string& qasm_text, bool verbose);

} // namespace qbin_compiler

#endif // QBIN_COMPILER_COMPILER_HPP

