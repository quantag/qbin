#include "qbin_compiler/qasm_frontend.hpp"
#include "qbin_compiler/tools.hpp"
#include "qbin_compiler/custom_gate.hpp"

#include <map>
#include <regex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace std;
using qbin_compiler::util::to_lower_ascii;
using qbin_compiler::util::trim;
using qbin_compiler::util::split_commas;
using qbin_compiler::util::find_matching_paren;
using qbin_compiler::util::eval_expr;
using qbin_compiler::util::vlog;

namespace qbin_compiler {
    namespace frontend {

        // ---------- IR emit helpers ----------
        static inline void emit_1q(vector<Instr>& out, Op op, int a) {
            Instr i{}; i.op = op; i.a = a; out.push_back(i);
        }
        static inline void emit_2q(vector<Instr>& out, Op op, int a, int b) {
            Instr i{}; i.op = op; i.a = a; i.b = b; out.push_back(i);
        }
        static inline void emit_angle(vector<Instr>& out, Op op, int a, double ang) {
            Instr i{}; i.op = op; i.a = a; i.has_angle = true; i.angle = float(ang); out.push_back(i);
        }
        static inline void emit_measure(vector<Instr>& out, int q, int c) {
            Instr i{}; i.op = Op::MEASURE; i.a = q; i.has_aux = true; i.aux = (uint32_t)c; out.push_back(i);
        }

        // ---------- IF matcher ----------
        static bool match_if_one_stmt(const std::string& line,
            std::string& creg_name,
            int& cidx,
            bool& is_eq,
            int& imm,
            std::string& body_stmt) {
            static const std::regex re(
                R"(^\s*if\s*\(\s*([A-Za-z_][A-Za-z0-9_]*)\s*\[(\d+)\]\s*(==|!=)\s*([0-9]+)\s*\)\s*\{\s*(.*?)\s*\}\s*;?\s*$)",
                std::regex::icase);

            std::smatch m;
            if (!std::regex_match(line, m, re)) return false;
            creg_name = to_lower_ascii(m[1].str());
            cidx = std::stoi(m[2].str());
            is_eq = (m[3].str() == "==");
            imm = std::stoi(m[4].str());
            body_stmt = m[5].str();
            return true;
        }

        // Expand one statement into canonical primitives
        static void expand_stmt_recursive(const string& stmt_in,
            const unordered_map<string, string>& subs,
            const GateRegistry& gate_registry,
            vector<string>& out_stmts,
            bool verbose) {
            string s = trim(stmt_in);
            if (s.empty()) return;

            string sl = to_lower_ascii(s);

            // ignore barrier/reset
            if (sl.rfind("barrier", 0) == 0) { vlog(verbose, "skip barrier"); return; }
            if (sl.rfind("reset", 0) == 0) { vlog(verbose, "skip reset");   return; }

            // cx a,b;
            {
                static regex recx(R"(^cx\s+(\S+)\s*,\s*(\S+)\s*;?$)", regex::icase);
                smatch m;
                if (regex_match(s, m, recx)) {
                    out_stmts.push_back("cx " + m[1].str() + ", " + m[2].str() + ";");
                    return;
                }
            }

            // measure
            {
                static regex rem1(R"(^\s*measure\s+(\S+)\s*->\s*(\S+)\s*;?$)", regex::icase);
                static regex rem2(R"(^\s*(\S+)\s*=\s*measure\s+(\S+)\s*;?$)", regex::icase);
                smatch m;
                if (regex_match(s, m, rem1)) { out_stmts.push_back(m[2].str() + " = measure " + m[1].str() + ";"); return; }
                if (regex_match(s, m, rem2)) { out_stmts.push_back(m[1].str() + " = measure " + m[2].str() + ";"); return; }
            }

            // custom gate call
            {
                static regex re_name_only(R"(^\s*([A-Za-z_][A-Za-z0-9_]*)\s*(.*?);?$)");
                smatch m;
                if (regex_match(s, m, re_name_only)) {
                    string name = to_lower_ascii(trim(m[1].str()));
                    if (gate_registry.hasGate(name)) {
                        string rest = trim(m[2].str());
                        vector<string> args = split_commas(rest, true);
                        auto expanded = gate_registry.getGate(name).expand(args);
                        for (auto& e : expanded)
                            expand_stmt_recursive(e, subs, gate_registry, out_stmts, verbose);
                        return;
                    }
                }
            }

            // pass-through
            out_stmts.push_back(s.back() == ';' ? s : s + ";");
        }

        // ---------- main parser ----------
        Program parse_qasm_subset(std::string_view text, bool verbose) {
            string src(text);
            GateRegistry gate_registry;

            // Normalize lines, strip comments
            vector<string> raw_lines;
            {
                string cur;
                for (size_t i = 0; i < src.size(); ++i) {
                    char c = src[i];
                    if (c == '\r') continue;
                    if (c == '/' && i + 1 < src.size() && src[i + 1] == '/') {
                        while (i < src.size() && src[i] != '\n') ++i;
                    }
                    if (i < src.size() && src[i] == '\n') { raw_lines.push_back(cur); cur.clear(); }
                    else if (i < src.size()) { cur.push_back(src[i]); }
                }
                raw_lines.push_back(cur);
            }

            // parse regs + gates
            vector<string> nondef_lines;
            for (size_t li = 0; li < raw_lines.size(); ++li) {
                string line = trim(raw_lines[li]);
                if (line.empty()) continue;
                string ll = to_lower_ascii(line);

                if (ll.rfind("gate ", 0) == 0) {
                    // Regex for: gate NAME params { body }
                    static regex rg(R"(^\s*gate\s+([A-Za-z_][A-Za-z0-9_]*)\s+([^{}]+)\{(.*)\}\s*$)",
                        regex::icase);
                    smatch m;
                    if (!regex_match(line, m, rg)) {
                        vlog(verbose, "Failed to parse gate definition: " + line);
                        continue;
                    }

                    string name = to_lower_ascii(trim(m[1].str()));
                    string paramlist = trim(m[2].str());
                    string body = trim(m[3].str());

                    // split params by comma
                    vector<string> params = split_commas(paramlist, true);

                    // split body by semicolon
                    vector<string> body_lines;
                    {
                        string token;
                        stringstream ss(body);
                        while (getline(ss, token, ';')) {
                            string t = trim(token);
                            if (!t.empty())
                                body_lines.push_back(t + ";");
                        }
                    }

                    CustomGate g(name, params, body_lines);
                    gate_registry.addGate(g);

                    vlog(verbose, "Registered custom gate: " + name +
                        " with " + to_string(params.size()) + " params and " +
                        to_string(body_lines.size()) + " body stmts");
                    continue; // skip emission
                }


                nondef_lines.push_back(line);
            }

            Program prog;
            // expand and emit non-def lines
            vector<string> canonical;
            for (auto& s : nondef_lines) {
                vector<string> expanded;
                expand_stmt_recursive(s, {}, gate_registry, expanded, verbose);
                canonical.insert(canonical.end(), expanded.begin(), expanded.end());
            }

            vlog(verbose, "canonical statements: " + to_string(canonical.size()));
            return prog;
        }

    } // namespace frontend
} // namespace qbin_compiler
