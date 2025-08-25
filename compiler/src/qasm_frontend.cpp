#include "qbin_compiler/qasm_frontend.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <sstream>
#include <string>

namespace qbin_compiler {
    namespace frontend {

        static inline std::string trim_copy(std::string s) {
            auto is_space = [](unsigned char ch) { return std::isspace(ch) != 0; };
            s.erase(s.begin(), std::find_if(s.begin(), s.end(), [&](char c) { return !is_space((unsigned char)c); }));
            s.erase(std::find_if(s.rbegin(), s.rend(), [&](char c) { return !is_space((unsigned char)c); }).base(), s.end());
            return s;
        }

        static bool parse_qubit_index(const std::string& tok, int& idx) {
            // expects q[<int>]
            if (tok.size() < 4) return false;
            if (tok[0] != 'q') return false;
            size_t lb = tok.find('[');
            size_t rb = tok.find(']');
            if (lb == std::string::npos || rb == std::string::npos || rb <= lb + 1) return false;
            std::string inside = tok.substr(lb + 1, rb - lb - 1);
            try { idx = std::stoi(inside); return true; }
            catch (...) { return false; }
        }

        static bool parse_bit_index(const std::string& tok, int& idx) {
            // expects c[<int>]
            if (tok.size() < 4) return false;
            if (tok[0] != 'c') return false;
            size_t lb = tok.find('[');
            size_t rb = tok.find(']');
            if (lb == std::string::npos || rb == std::string::npos || rb <= lb + 1) return false;
            std::string inside = tok.substr(lb + 1, rb - lb - 1);
            try { idx = std::stoi(inside); return true; }
            catch (...) { return false; }
        }

        static bool parse_angle_from(const std::string& t, float& val) {
            size_t lp = t.find('(');
            size_t rp = t.find(')');
            if (lp == std::string::npos || rp == std::string::npos || rp <= lp + 1) return false;
            std::string a = t.substr(lp + 1, rp - lp - 1);
            try { val = std::stof(a); return true; }
            catch (...) { return false; }
        }

        Program parse_qasm_subset(std::string_view text, bool verbose) {
            Program P;
            std::istringstream iss{ std::string(text) };
            std::string line;
            size_t lineno = 0;
            while (std::getline(iss, line)) {
                ++lineno;
                std::string s = trim_copy(line);
                if (s.empty() || s[0] == '/' || s[0] == '#') continue;

                // Keep a lowercase copy for quick checks
                std::string lower = s;
                std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });

                auto warn_skip = [&](const char* reason) {
                    if (verbose) std::fprintf(stderr, "[skip line %zu] %s: %s\n", lineno, reason, s.c_str());
                    };

                // Ignore declarations (we infer sizes)
                if (lower.rfind("openqasm", 0) == 0) continue;
                if (lower.rfind("include", 0) == 0) continue;
                if (lower.rfind("qubit", 0) == 0)   continue;
                if (lower.rfind("bit", 0) == 0)     continue;

                // MEASURE: c[k] = measure q[i];
                {
                    // robust check for pattern
                    size_t eq = lower.find('=');
                    if (lower.rfind("c[", 0) == 0 && eq != std::string::npos && lower.find("measure", eq) != std::string::npos) {
                        std::string lhs = trim_copy(s.substr(0, eq));
                        std::string rhs = trim_copy(s.substr(eq + 1));
                        int bit_idx = -1, q_idx = -1;
                        if (!parse_bit_index(lhs, bit_idx)) { warn_skip("bad bit index on measure LHS"); goto after_measure; }
                        // rhs expected: "measure q[i];" (semicolon optional at this point)
                        // Normalize spaces and commas
                        for (char& c : rhs) if (c == ',') c = ' ';
                        std::istringstream rs(rhs);
                        std::string tok0, tok1;
                        if (!(rs >> tok0 >> tok1)) { warn_skip("measure RHS too short"); goto after_measure; }
                        std::string t0l = tok0; std::transform(t0l.begin(), t0l.end(), t0l.begin(), ::tolower);
                        if (t0l != "measure") { warn_skip("RHS missing 'measure'"); goto after_measure; }
                        if (!parse_qubit_index(tok1, q_idx)) { warn_skip("bad qubit on measure RHS"); goto after_measure; }
                        // Emit
                        Instr I{};
                        I.op = Opcode::MEASURE;
                        I.a = q_idx;
                        I.has_aux = true;
                        I.aux_u32 = static_cast<uint32_t>(bit_idx);
                        P.instrs.push_back(I);
                        continue;
                    }
                }
            after_measure:;

                // IF single-line: if (c[k] == 1) { <stmt>; }
                if (lower.rfind("if", 0) == 0) {
                    size_t lp = lower.find('(');
                    size_t rp = lower.find(')');
                    size_t lb = lower.find('{');
                    size_t rb = lower.rfind('}');
                    if (lp == std::string::npos || rp == std::string::npos || lb == std::string::npos || rb == std::string::npos || rb < lb) {
                        warn_skip("unsupported if format");
                        continue;
                    }
                    std::string cond = trim_copy(lower.substr(lp + 1, rp - lp - 1)); // e.g. c[1] == 1
                    // parse lhs op rhs
                    bool is_eq = true;
                    size_t pos_eq = cond.find("==");
                    size_t pos_neq = cond.find("!=");
                    if (pos_eq == std::string::npos && pos_neq == std::string::npos) { warn_skip("if condition missing ==/!="); continue; }
                    size_t pos_op = (pos_eq != std::string::npos) ? pos_eq : pos_neq;
                    if (pos_neq != std::string::npos) is_eq = false;
                    std::string lhs = trim_copy(cond.substr(0, pos_op));
                    std::string rhs = trim_copy(cond.substr(pos_op + 2));
                    int bit_idx = -1;
                    int val = 0;
                    if (!parse_bit_index(lhs, bit_idx)) { warn_skip("bad c[k] in if"); continue; }
                    try { val = std::stoi(rhs); }
                    catch (...) { warn_skip("bad compare value in if"); continue; }
                    // Emit IF
                    Instr IF{};
                    IF.op = is_eq ? Opcode::IF_EQ : Opcode::IF_NEQ;
                    IF.has_aux = true;  IF.aux_u32 = static_cast<uint32_t>(bit_idx);
                    IF.has_imm8 = true; IF.imm8 = static_cast<uint8_t>(val & 0xFF);
                    P.instrs.push_back(IF);

                    // Body inside braces
                    std::string body = trim_copy(s.substr(lb + 1, rb - lb - 1));
                    if (!body.empty() && body.back() == ';') body.pop_back();
                    for (char& c : body) if (c == ',') c = ' ';
                    std::istringstream ts(body);
                    std::string tok0;
                    if (!(ts >> tok0)) {
                        // empty body, just ENDIF
                        Instr End{}; End.op = Opcode::ENDIF; P.instrs.push_back(End);
                        continue;
                    }
                    float ang = 0.0f;
                    bool has_ang = parse_angle_from(tok0, ang);
                    std::string gate = tok0;
                    if (has_ang) gate = tok0.substr(0, tok0.find('('));

                    if (gate == "cx" || gate == "cz" || gate == "swap") {
                        std::string qa, qb;
                        if (!(ts >> qa >> qb)) { warn_skip("if-body expects two qubits"); }
                        else {
                            int ia = -1, ib = -1;
                            if (!parse_qubit_index(qa, ia) || !parse_qubit_index(qb, ib)) { warn_skip("if-body bad qubits"); }
                            else {
                                Instr I{};
                                if (gate == "cx") I.op = Opcode::CX;
                                else if (gate == "cz") I.op = Opcode::CZ;
                                else I.op = Opcode::SWAP;
                                I.a = ia; I.b = ib;
                                P.instrs.push_back(I);
                            }
                        }
                    }
                    else if (gate == "h" || gate == "x" || gate == "y" || gate == "z" ||
                        gate == "s" || gate == "sdg" || gate == "t" || gate == "tdg" ||
                        gate == "sx" || gate == "sxdg" || gate == "rx" || gate == "ry" ||
                        gate == "rz" || gate == "phase") {
                        std::string qa;
                        if (!(ts >> qa)) { warn_skip("if-body expects one qubit"); }
                        else {
                            int ia = -1;
                            if (!parse_qubit_index(qa, ia)) { warn_skip("if-body bad qubit"); }
                            else {
                                Instr I{};
                                if (gate == "h") I.op = Opcode::H;
                                else if (gate == "x") I.op = Opcode::X;
                                else if (gate == "y") I.op = Opcode::Y;
                                else if (gate == "z") I.op = Opcode::Z;
                                else if (gate == "s") I.op = Opcode::S;
                                else if (gate == "sdg") I.op = Opcode::SDG;
                                else if (gate == "t") I.op = Opcode::T;
                                else if (gate == "tdg") I.op = Opcode::TDG;
                                else if (gate == "sx") I.op = Opcode::SX;
                                else if (gate == "sxdg") I.op = Opcode::SXDG;
                                else if (gate == "rx") { I.op = Opcode::RX; I.has_angle0 = has_ang; I.angle0 = ang; }
                                else if (gate == "ry") { I.op = Opcode::RY; I.has_angle0 = has_ang; I.angle0 = ang; }
                                else if (gate == "rz") { I.op = Opcode::RZ; I.has_angle0 = has_ang; I.angle0 = ang; }
                                else if (gate == "phase") { I.op = Opcode::PHASE; I.has_angle0 = has_ang; I.angle0 = ang; }
                                I.a = ia;
                                P.instrs.push_back(I);
                            }
                        }
                    }
                    else {
                        warn_skip("unsupported if-body statement");
                    }

                    Instr End{}; End.op = Opcode::ENDIF; P.instrs.push_back(End);
                    continue;
                }

                // Regular statements
                // Remove trailing ';' then normalize commas to spaces
                if (!s.empty() && s.back() == ';') s.pop_back();
                for (char& c : s) if (c == ',') c = ' ';
                std::istringstream ts(s);
                std::string tok0;
                if (!(ts >> tok0)) { warn_skip("empty"); continue; }

                float ang = 0.0f;
                bool has_ang = parse_angle_from(tok0, ang);
                std::string gate = tok0;
                if (has_ang) gate = tok0.substr(0, tok0.find('('));

                // Two-qubit without angles
                if (gate == "cx" || gate == "cz" || gate == "swap") {
                    std::string qa, qb;
                    if (!(ts >> qa >> qb)) { warn_skip("expected two qubits"); continue; }
                    int ia = -1, ib = -1;
                    if (!parse_qubit_index(qa, ia) || !parse_qubit_index(qb, ib)) { warn_skip("bad qubit index"); continue; }
                    Instr I{};
                    if (gate == "cx") I.op = Opcode::CX;
                    else if (gate == "cz") I.op = Opcode::CZ;
                    else I.op = Opcode::SWAP;
                    I.a = ia; I.b = ib;
                    P.instrs.push_back(I);
                    continue;
                }

                // One-qubit gates
                if (gate == "h" || gate == "x" || gate == "y" || gate == "z" ||
                    gate == "s" || gate == "sdg" || gate == "t" || gate == "tdg" ||
                    gate == "sx" || gate == "sxdg" || gate == "rx" || gate == "ry" ||
                    gate == "rz" || gate == "phase") {
                    std::string qa;
                    if (!(ts >> qa)) { warn_skip("expected one qubit"); continue; }
                    int ia = -1;
                    if (!parse_qubit_index(qa, ia)) { warn_skip("bad qubit index"); continue; }
                    Instr I{};
                    if (gate == "h") I.op = Opcode::H;
                    else if (gate == "x") I.op = Opcode::X;
                    else if (gate == "y") I.op = Opcode::Y;
                    else if (gate == "z") I.op = Opcode::Z;
                    else if (gate == "s") I.op = Opcode::S;
                    else if (gate == "sdg") I.op = Opcode::SDG;
                    else if (gate == "t") I.op = Opcode::T;
                    else if (gate == "tdg") I.op = Opcode::TDG;
                    else if (gate == "sx") I.op = Opcode::SX;
                    else if (gate == "sxdg") I.op = Opcode::SXDG;
                    else if (gate == "rx") { I.op = Opcode::RX; I.has_angle0 = has_ang; I.angle0 = ang; }
                    else if (gate == "ry") { I.op = Opcode::RY; I.has_angle0 = has_ang; I.angle0 = ang; }
                    else if (gate == "rz") { I.op = Opcode::RZ; I.has_angle0 = has_ang; I.angle0 = ang; }
                    else if (gate == "phase") { I.op = Opcode::PHASE; I.has_angle0 = has_ang; I.angle0 = ang; }
                    I.a = ia;
                    P.instrs.push_back(I);
                    continue;
                }

                // Unsupported
                warn_skip("unsupported");
            }
            return P;
        }

    } // namespace frontend
} // namespace qbin_compiler
