#pragma once
#include <string>
#include <vector>

namespace qbin_compiler {
    namespace qasm {

        /**
         * LineProcessor: takes raw QASM text and produces cleaned lines.
         * Responsibilities:
         *  - Remove // comments
         *  - Remove /* ... * / block comments
         *  - Normalize CR/LF
         *  - Return non-empty raw lines
         */
        class LineProcessor {
        public:
            /// Process raw QASM source text into a vector of lines.
            static std::vector<std::string> preprocess(const std::string& src);
        };

    } // namespace qasm
} // namespace qbin_compiler
