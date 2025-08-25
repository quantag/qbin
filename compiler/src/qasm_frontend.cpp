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
            size_t lb = tok.find('[');
            size_t rb = tok.find(']');
            if (lb == std::string::npos || rb == std::string::npos || rb <= lb + 1) return false;
            std::string inside = tok.substr(lb + 1, rb - lb - 1);
            try {
                idx = std::stoi(inside);
                return true;
            }
            catch (...) {
                return false;
            }
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
                // remove trailing ';'
                if (!s.empty() && s.back() == ';') s.pop_back();
                // lowercase for opcode matching
                std::string lower = s;
                std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });

                auto warn_skip = [&](const char* reason) {
                    if (verbose) std::fprintf(stderr, "[skip line %zu] %s: %s\n", lineno, reason, s.c_str());
                    };

                // Ignore declarations
                if (lower.rfind("openqasm", 0) == 0) { continue; }
                if (lower.rfind("include", 0) == 0) { continue; }
                if (lower.rfind("qubit", 0) == 0) { continue; }
                if (lower.rfind("bit", 0) == 0) { continue; }

                // Replace commas with spaces
                for (char& c : s) if (c == ',') c = ' ';
                std::istringstream ts(s);
                std::string tok0; // gate or gate(angle)
                if (!(ts >> tok0)) { warn_skip("empty"); continue; }

                auto parse_angle_from = [](const std::string& t, float& val)->bool {
                    size_t lp = t.find('(');
                    size_t rp = t.find(')');
                    if (lp == std::string::npos || rp == std::string::npos || rp <= lp + 1) return false;
                    std::string a = t.substr(lp + 1, rp - lp - 1);
                    try {
                        val = std::stof(a);
                        return true;
                    }
                    catch (...) { return false; }
                    };

                // Two-qubit without angles
                if (tok0 == "cx" || tok0 == "cz" || tok0 == "swap") {
                    std::string qa, qb;
                    if (!(ts >> qa >> qb)) { warn_skip("expected two qubits"); continue; }
                    int ia = -1, ib = -1;
                    if (!parse_qubit_index(qa, ia) || !parse_qubit_index(qb, ib)) { warn_skip("bad qubit index"); continue; }
                    Instr I{};
                    if (tok0 == "cx") I.op = Opcode::CX;
                    else if (tok0 == "cz") I.op = Opcode::CZ;
                    else I.op = Opcode::SWAP;
                    I.a = ia; I.b = ib;
                    P.instrs.push_back(I);
                    continue;
                }

                // Angle form: name(angle)
                float ang = 0.0f;
                bool has_angle = parse_angle_from(tok0, ang);
                std::string gate = tok0;
                if (has_angle) gate = tok0.substr(0, tok0.find('('));

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
                    else if (gate == "rx") { I.op = Opcode::RX; I.has_angle0 = has_angle; I.angle0 = ang; }
                    else if (gate == "ry") { I.op = Opcode::RY; I.has_angle0 = has_angle; I.angle0 = ang; }
                    else if (gate == "rz") { I.op = Opcode::RZ; I.has_angle0 = has_angle; I.angle0 = ang; }
                    else if (gate == "phase") { I.op = Opcode::PHASE; I.has_angle0 = has_angle; I.angle0 = ang; }
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
