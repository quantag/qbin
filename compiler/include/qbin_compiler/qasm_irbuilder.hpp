#pragma once
#include <string>
#include <vector>
#include "qbin_compiler/qasm_frontend.hpp"
#include "qbin_compiler/custom_gate.hpp"

namespace qbin_compiler {
    namespace frontend {

        /**
         * IRBuilder:
         *   Converts canonical QASM statements into Program (IR).
         */
        struct IRBuilder {
            static Program emit(
                const std::vector<std::string>& canonical,
                const std::unordered_map<std::string, std::pair<int,int>>& qregs,
                const std::unordered_map<std::string, std::pair<int,int>>& cregs,
                bool verbose);
        };

    } // namespace frontend
} // namespace qbin_compiler
