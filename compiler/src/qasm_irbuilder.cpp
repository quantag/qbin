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
                    static std::regex rif(R"(^\s*if\s*\(\s*([A-Za-z_]\w*\[\d+\])\s*==\s*(\d+)\s*\)\s*(.+)$)", std::regex::icase);
                    static std::regex rifne(R"(^\s*if\s*\(\s*([A-Za-z_]\w*\[\d+\])\s*!=\s*(\d+)\s*\)\s*(.+)$)", std::regex::icase);
                    std::smatch m;

                    if (std::regex_match(s, m, rif)) {
                        int c = resolve_cbit(trim(m[1].str()));
                        int imm = std::stoi(m[2].str());
                        if (c < 0) { vlog(verbose, "if resolve failed: " + s); continue; }

                        Instr i{}; i.op = Op::IF_EQ; i.has_aux = true; i.aux = static_cast<uint32_t>(c);
                        i.has_imm8 = true; i.imm8 = static_cast<uint8_t>(imm);
                        prog.code.push_back(i);

                        // recursively emit body statement(s)
                        std::string body = trim(m[3].str());
                        std::vector<std::string> tmp{ body };
                        Program sub = IRBuilder::emit(tmp, qregs, cregs, verbose);
                        prog.code.insert(prog.code.end(), sub.code.begin(), sub.code.end());

                        // close IF
                        Instr e{}; e.op = Op::ENDIF; prog.code.push_back(e);
                        continue;
                    }

                    if (std::regex_match(s, m, rifne)) {
                        int c = resolve_cbit(trim(m[1].str()));
                        int imm = std::stoi(m[2].str());
                        if (c < 0) { vlog(verbose, "if resolve failed: " + s); continue; }

                        Instr i{}; i.op = Op::IF_NEQ; i.has_aux = true; i.aux = static_cast<uint32_t>(c);
                        i.has_imm8 = true; i.imm8 = static_cast<uint8_t>(imm);
                        prog.code.push_back(i);

                        // recursively emit body
                        std::string body = trim(m[3].str());
                        std::vector<std::string> tmp{ body };
                        Program sub = IRBuilder::emit(tmp, qregs, cregs, verbose);
                        prog.code.insert(prog.code.end(), sub.code.begin(), sub.code.end());

                        // close IF
                        Instr e{}; e.op = Op::ENDIF; prog.code.push_back(e);
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
