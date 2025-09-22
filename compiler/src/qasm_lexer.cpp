#include "qbin_compiler/qasm_lexer.hpp"
#include "qbin_compiler/tools.hpp"  // for util::trim

#include <string>

namespace qbin_compiler {
    namespace qasm {

        std::vector<std::string> LineProcessor::preprocess(const std::string& src) {
            std::vector<std::string> raw_lines;
            std::string cur;
            bool in_block_comment = false;

            for (size_t i = 0; i < src.size(); ++i) {
                char c = src[i];
                char n = (i + 1 < src.size()) ? src[i + 1] : '\0';

                // Line comments //
                if (!in_block_comment && c == '/' && n == '/') {
                    while (i < src.size() && src[i] != '\n') ++i;
                    if (i < src.size() && src[i] == '\n') {
                        raw_lines.push_back(cur);
                        cur.clear();
                    }
                    continue;
                }

                // Block comments /* ... */
                if (!in_block_comment && c == '/' && n == '*') {
                    in_block_comment = true;
                    ++i;
                    continue;
                }
                if (in_block_comment && c == '*' && n == '/') {
                    in_block_comment = false;
                    ++i;
                    continue;
                }
                if (in_block_comment) continue;

                // Normalize CR/LF
                if (c == '\r') continue;
                if (c == '\n') {
                    raw_lines.push_back(cur);
                    cur.clear();
                } else {
                    cur.push_back(c);
                }
            }
            if (!cur.empty()) raw_lines.push_back(cur);

            return raw_lines;
        }

    } // namespace qasm
} // namespace qbin_compiler
