#include "qbin_compiler/qasm_irbuilder.hpp"
#include "qbin_compiler/tools.hpp"

#include <regex>
#include <sstream>

using qbin_compiler::util::to_lower_ascii;
using qbin_compiler::util::trim;
using qbin_compiler::util::eval_expr;
using qbin_compiler::util::vlog;

namespace qbin_compiler {
    namespace frontend {

        // --- helpers for emitting instructions ---
        static inline void emit_1q(std::vector<Instr>& out, Op op, int a) {
            Instr i{}; i.op = op; i.a = a; out.push_back(i);
        }
        static inline void emit_2q(std::vector<Instr>& out, Op op, int a, int b) {
            Instr i{}; i.op = op; i.a = a; i.b = b; out.push_back(i);
        }
        static inline void emit_angle(std::vector<Instr>& out, Op op, int a, double ang) {
            Instr i{}; i.op = op; i.a = a; i.has_angle = true; i.angle = static_cast<float>(ang); out.push_back(i);
        }
        static inline void emit_measure(std::vector<Instr>& out, int q, int c) {
            Instr i{}; i.op = Op::MEASURE; i.a = q; i.has_aux = true; i.aux = static_cast<uint32_t>(c); out.push_back(i);
        }

        // --- resolver helpers ---
        static int resolve_index(const std::unordered_map<std::string, std::pair<int,int>>& regs,
                                 const std::string& token,
                                 bool /* verbose */ )
        {
            static std::regex ri(R"(^\s*([A-Za-z_]\w*)\s*\[\s*(\d+)\s*\]\s*$)");
            std::smatch m;
            if (!std::regex_match(token, m, ri)) return -1;
            std::string nm = to_lower_ascii(m[1].str());
            int idx = std::stoi(m[2].str());
            auto it = regs.find(nm);
            if (it == regs.end()) return -1;
            int base = it->second.first, sz = it->second.second;
            if (idx < 0 || idx >= sz) return -1;
            return base + idx;
        }

        Program IRBuilder::emit(const std::vector<std::string>& canonical,
                                const std::unordered_map<std::string, std::pair<int,int>>& qregs,
                                const std::unordered_map<std::string, std::pair<int,int>>& cregs,
                                bool verbose)
        {
            Program prog;

            auto resolve_qubit = [&](const std::string& t){ return resolve_index(qregs, t, verbose); };
            auto resolve_cbit  = [&](const std::string& t){ return resolve_index(cregs, t, verbose); };

            for (const auto& st : canonical) {
                std::string s = trim(st);
                if (s.empty()) continue;

                // measure
                {
                    static std::regex rc(R"(^\s*(.+?)\s*=\s*measure\s+(.+?)\s*;?$)", std::regex::icase);
                    std::smatch m;
                    if (std::regex_match(s, m, rc)) {
                        int q = resolve_qubit(trim(m[2].str()));
                        int c = resolve_cbit(trim(m[1].str()));
                        if (q < 0 || c < 0) { vlog(verbose, "measure resolve failed: " + s); continue; }
                        emit_measure(prog.code, q, c);
                        continue;
                    }
                }

                // two-qubit
                {
                    static std::regex r2(R"(^\s*(cx|cz|swap)\s+(.+?)\s*,\s*(.+?)\s*;?$)", std::regex::icase);
                    std::smatch m;
                    if (std::regex_match(s, m, r2)) {
                        int a = resolve_qubit(trim(m[2].str()));
                        int b = resolve_qubit(trim(m[3].str()));
                        if (a < 0 || b < 0) { vlog(verbose, "2q resolve failed: " + s); continue; }
                        std::string op = to_lower_ascii(m[1].str());
                        if (op == "cx") emit_2q(prog.code, Op::CX, a, b);
                        else if (op == "cz") emit_2q(prog.code, Op::CZ, a, b);
                        else if (op == "swap") emit_2q(prog.code, Op::SWAP, a, b);
                        continue;
                    }
                }

                // one-qubit
                {
                    static std::regex r1(R"(^\s*(h|x|y|z|s|sdg|t|tdg|sx|sxdg)\s+(.+?)\s*;?$)", std::regex::icase);
                    std::smatch m;
                    if (std::regex_match(s, m, r1)) {
                        int a = resolve_qubit(trim(m[2].str()));
                        if (a < 0) { vlog(verbose, "1q resolve failed: " + s); continue; }
                        std::string op = to_lower_ascii(m[1].str());
                        if (op == "h") emit_1q(prog.code, Op::H, a);
                        else if (op == "x") emit_1q(prog.code, Op::X, a);
                        else if (op == "y") emit_1q(prog.code, Op::Y, a);
                        else if (op == "z") emit_1q(prog.code, Op::Z, a);
                        else if (op == "s") emit_1q(prog.code, Op::S, a);
                        else if (op == "sdg") emit_1q(prog.code, Op::SDG, a);
                        else if (op == "t") emit_1q(prog.code, Op::T, a);
                        else if (op == "tdg") emit_1q(prog.code, Op::TDG, a);
                        else if (op == "sx") emit_1q(prog.code, Op::SX, a);
                        else if (op == "sxdg") emit_1q(prog.code, Op::SXDG, a);
                        continue;
                    }
                }

                // param gates
                {
                    static std::regex rp(R"(^\s*(rx|ry|rz|phase)\s*\(\s*(.+?)\s*\)\s+(.+?)\s*;?$)", std::regex::icase);
                    std::smatch m;
                    if (std::regex_match(s, m, rp)) {
                        std::string op = to_lower_ascii(m[1].str());
                        std::string expr = trim(m[2].str());
                        int a = resolve_qubit(trim(m[3].str()));
                        if (a < 0) { vlog(verbose, "param resolve failed: " + s); continue; }
                        double ang = 0.0;
                        try { ang = eval_expr(expr); }
                        catch (...) { vlog(verbose, "param eval failed: " + expr); }
                        if (op == "rx") emit_angle(prog.code, Op::RX, a, ang);
                        else if (op == "ry") emit_angle(prog.code, Op::RY, a, ang);
                        else if (op == "rz") emit_angle(prog.code, Op::RZ, a, ang);
                        else if (op == "phase") emit_angle(prog.code, Op::PHASE, a, ang);
                        continue;
                    }
                }

                // IF / ENDIF
                {
                    // Matches: if (c[1] == 1) body
                    static std::regex rif(R"(^\s*if\s*\(\s*([A-Za-z_]\w*\s*\[\s*\d+\s*\])\s*==\s*(\d+)\s*\)\s*(.+?)\s*;?\s*$)", std::regex::icase);
                    static std::regex rifne(R"(^\s*if\s*\(\s*([A-Za-z_]\w*\s*\[\s*\d+\s*\])\s*!=\s*(\d+)\s*\)\s*(.+?)\s*;?\s*$)", std::regex::icase);
                    std::smatch m;

                    auto emit_if_block = [&](bool is_eq, const std::string& ctoken, int imm, const std::string& body_str) {
                        // Resolve cbit index
                        int cidx = resolve_cbit(trim(ctoken));
                        if (cidx < 0) { vlog(verbose, std::string("if resolve failed: ") + ctoken); return; }

                        // Emit IF opcode
                        Instr ifi{};
                        ifi.op = is_eq ? Op::IF_EQ : Op::IF_NEQ;
                        ifi.has_aux = true;
                        ifi.aux = static_cast<uint32_t>(cidx);
                        ifi.has_imm8 = true;
                        ifi.imm8 = static_cast<uint8_t>(imm);
                        prog.code.push_back(ifi);

                        // Extract body. Accept either single statement or a braced block.
                        std::string body = trim(body_str);

                        // If body starts with '{', peel the braces and split into statements on semicolons at depth 0.
                        std::vector<std::string> stmts;
                        if (!body.empty() && body.front() == '{') {
                            // Find matching closing brace for the first '{'
                            int depth = 0;
                            size_t endpos = std::string::npos;
                            for (size_t i = 0; i < body.size(); ++i) {
                                char ch = body[i];
                                if (ch == '{') ++depth;
                                else if (ch == '}') {
                                    --depth;
                                    if (depth == 0) { endpos = i; break; }
                                }
                            }
                            if (endpos == std::string::npos) {
                                vlog(verbose, "if body brace mismatch");
                            }
                            else {
                                std::string inner = trim(body.substr(1, endpos - 1));
                                // Split inner on semicolons outside parentheses and braces
                                size_t p = 0, last = 0; int dpar = 0, dcurly = 0;
                                while (p <= inner.size()) {
                                    bool at_end = (p == inner.size());
                                    char ch = at_end ? '\0' : inner[p];
                                    if (!at_end) {
                                        if (ch == '(') ++dpar;
                                        else if (ch == ')') --dpar;
                                        else if (ch == '{') ++dcurly;
                                        else if (ch == '}') --dcurly;
                                    }
                                    if (at_end || (ch == ';' && dpar == 0 && dcurly == 0)) {
                                        std::string t = trim(std::string_view(inner).substr(last, p - last));
                                        if (!t.empty()) stmts.push_back(t + ";");
                                        last = p + 1;
                                    }
                                    ++p;
                                }
                            }
                        }
                        else {
                            // Single statement body, ensure it ends with ';'
                            std::string one = body;
                            if (!one.empty() && one.back() != ';') one.push_back(';');
                            if (!one.empty()) stmts.push_back(one);
                        }

                        // Recursively emit body statements
                        if (!stmts.empty()) {
                            Program sub = IRBuilder::emit(stmts, qregs, cregs, verbose);
                            prog.code.insert(prog.code.end(), sub.code.begin(), sub.code.end());
                        }
                        else {
                            vlog(verbose, "empty if body");
                        }

                        // Close IF
                        Instr endi{}; endi.op = Op::ENDIF; prog.code.push_back(endi);
                        };

                    if (std::regex_match(s, m, rif)) {
                        std::string ctoken = m[1].str();
                        int imm = std::stoi(m[2].str());
                        std::string body = m[3].str();
                        emit_if_block(true, ctoken, imm, body);
                        continue;
                    }
                    if (std::regex_match(s, m, rifne)) {
                        std::string ctoken = m[1].str();
                        int imm = std::stoi(m[2].str());
                        std::string body = m[3].str();
                        emit_if_block(false, ctoken, imm, body);
                        continue;
                    }
                }

                // ignore barrier/reset defensively
                {
                    static std::regex rb(R"(^\s*(barrier|reset)\b)", std::regex::icase);
                    if (std::regex_search(s, rb)) continue;
                }

                vlog(verbose, "ignored stmt: " + (s.size() > 64 ? s.substr(0, 64) : s));
            }

            return prog;
        }

    } // namespace frontend
} // namespace qbin_compiler
