#ifndef QBIN_DECOMPILER_DECOMPILER_HPP
#define QBIN_DECOMPILER_DECOMPILER_HPP

#include <cstdint>
#include <string>
#include <vector>

namespace qbin_decompiler {

    bool decode_qbin_to_qasm(const std::vector<uint8_t>& bytes,
        std::string& qasm_out,
        std::string& err,
        bool verbose = false);

} // namespace qbin_decompiler

#endif // QBIN_DECOMPILER_DECOMPILER_HPP
