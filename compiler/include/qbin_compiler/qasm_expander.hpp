#pragma once
#include <string>
#include <vector>
#include "qbin_compiler/custom_gate.hpp"

namespace qbin_compiler {
    namespace frontend {

        /**
         * StatementExpander:
         *   Expands raw QASM statements into canonical form:
         *     - Normalizes measure/barrier/reset forms
         *     - Expands custom gates via GateRegistry
         *     - Ensures trailing semicolons
         */
        struct StatementExpander {
            static std::vector<std::string> expand(
                const std::vector<std::string>& nondef_lines,
                const GateRegistry& gates,
                bool verbose);
        };

    } // namespace frontend
} // namespace qbin_compiler
