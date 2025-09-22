#include "qbin_compiler/qasm_frontend.hpp"
#include "qbin_compiler/tools.hpp"
#include "qbin_compiler/custom_gate.hpp"
#include "qbin_compiler/qasm_lexer.hpp"
#include "qbin_compiler/qasm_expander.hpp"
#include "qbin_compiler/qasm_irbuilder.hpp"

#include <algorithm>
#include <cctype>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef WIN32
#include <functional>
#endif

using namespace std;

using qbin_compiler::util::to_lower_ascii;
using qbin_compiler::util::trim;
using qbin_compiler::util::find_matching_paren;
using qbin_compiler::util::eval_expr;
using qbin_compiler::util::vlog;

namespace qbin_compiler {
    namespace frontend {


        static inline void emit_1q(vector<Instr>& out, Op op, int a) {
            Instr i{};
            i.op = op;
            i.a = a;
            out.push_back(i);
        }

        static inline void emit_2q(vector<Instr>& out, Op op, int a, int b) {
            Instr i{};
            i.op = op;
            i.a = a;
            i.b = b;
            out.push_back(i);
        }

        static inline void emit_angle(vector<Instr>& out, Op op, int a, double ang) {
            Instr i{};
            i.op = op;
            i.a = a;
            i.has_angle = true;
            i.angle = static_cast<float>(ang);
            out.push_back(i);
        }

        static inline void emit_measure(vector<Instr>& out, int q, int c) {
            Instr i{};
            i.op = Op::MEASURE;
            i.a = q;
            i.has_aux = true;
            i.aux = static_cast<uint32_t>(c);
            out.push_back(i);
        }

        // Split a comma-separated list respecting simple parentheses; returns trimmed items.
        static vector<string> split_csv(const string& s) {
            vector<string> out;
            string cur;
            int depth = 0;
            for (size_t i = 0; i < s.size(); ++i) {
                char ch = s[i];
                if (ch == '(') { ++depth; cur.push_back(ch); }
                else if (ch == ')') { --depth; cur.push_back(ch); }
                else if (ch == ',' && depth == 0) {
                    auto t = trim(cur);
                    if (!t.empty()) out.push_back(string(t));
                    cur.clear();
                }
                else {
                    cur.push_back(ch);
                }
            }
            auto t = trim(cur);
            if (!t.empty()) out.push_back(string(t));
            return out;
        }

        // ------------------------ The parser ------------------------

        Program parse_qasm_subset(std::string_view text, bool verbose) {
            GateRegistry gate_registry;
            string src(text);

            auto raw_lines = qbin_compiler::qasm::LineProcessor::preprocess(src);

            // ---- 2) Parse register declarations + custom gate definitions; collect the rest ----
            // Registers: keep simple maps name (base, size).
            unordered_map<string, pair<int, int>> qregs; // qubits
            unordered_map<string, pair<int, int>> cregs; // classical bits
            int q_base = 0, c_base = 0;

            vector<string> nondef_lines;

            for (size_t li = 0; li < raw_lines.size(); ++li) {
                string line = trim(raw_lines[li]);
                if (line.empty()) continue;
                string ll = to_lower_ascii(line);

                // OPENQASM header or includes ignore for now
                if (ll.rfind("openqasm", 0) == 0) continue;
                if (ll.rfind("include", 0) == 0)  continue;

                // qubit[N] name;  or bit[N] name;
                {
                    static regex rq(R"(^\s*qubit\s*\[\s*(\d+)\s*\]\s*([A-Za-z_]\w*)\s*;?\s*$)", regex::icase);
                    static regex rc(R"(^\s*bit\s*\[\s*(\d+)\s*\]\s*([A-Za-z_]\w*)\s*;?\s*$)", regex::icase);
                    smatch m;
                    if (regex_match(line, m, rq)) {
                        int sz = stoi(m[1].str());
                        string nm = to_lower_ascii(m[2].str());
                        qregs[nm] = { q_base, sz };
                        q_base += sz;
                        continue;
                    }
                    if (regex_match(line, m, rc)) {
                        int sz = stoi(m[1].str());
                        string nm = to_lower_ascii(m[2].str());
                        cregs[nm] = { c_base, sz };
                        c_base += sz;
                        continue;
                    }
                }

                // Custom gate definition:
                // We accept both "gate name a,b,c { .. }" and multi-line with nested braces.
                if (ll.rfind("gate ", 0) == 0) {
                    // Accumulate until matching '}' that closes the first '{' we encounter.
                    string accum = line;
                    // If there's no '{' on this line, pull next lines until we see one.
                    while (accum.find('{') == string::npos && li + 1 < raw_lines.size()) {
                        accum.push_back(' ');
                        accum += trim(raw_lines[++li]);
                    }

                    size_t brace_pos = accum.find('{');
                    if (brace_pos == string::npos) {
                        vlog(verbose, "Malformed gate header (no '{'): " + accum);
                        continue;
                    }

                    // Parse the gate header: "gate NAME <maybe params> <maybe args> {"
                    // We keep it simple and collect everything between 'gate NAME' and '{'
                    string head = trim(accum.substr(0, brace_pos));
                    static regex rehead(R"(^\s*gate\s+([A-Za-z_]\w*)\s+(.+?)\s*$)", regex::icase);
                    smatch mh;
                    if (!regex_match(head, mh, rehead)) {
                        vlog(verbose, "Failed to parse gate header: " + head);
                        // try to continue scanning to close braces anyway
                    }

                    string gname;
                    vector<string> formals; // we unify params + qubit formals here (name-level substitution)
                    if (mh.size() >= 3) {
                        gname = to_lower_ascii(trim(mh[1].str()));
                        string tail = trim(mh[2].str());
                        // tail can be "a,b,c" or "(theta,phi) a,b" we flatten everything separated by spaces+commas
                        // First, if there is a ')', split around it to extract "(...)" then the rest.
                        size_t rp = tail.find(')');
                        if (tail.size() && tail[0] == '(' && rp != string::npos) {
                            string plist = tail.substr(1, rp - 1);
                            auto p = split_csv(plist);
                            for (auto& t : p) {
                                auto tt = trim(t);
                                if (!tt.empty()) formals.push_back(string(tt));
                            }
                            string rest = trim(tail.substr(rp + 1));
                            if (!rest.empty()) {
                                auto q = split_csv(rest);
                                for (auto& t : q) {
                                    auto tt = trim(t);
                                    if (!tt.empty()) formals.push_back(string(tt));
                                }
                            }
                        }
                        else {
                            // no parameter list; just qubit names separated by commas
                            auto q = split_csv(tail);
                            for (auto& t : q) {
                                auto tt = trim(t);
                                if (!tt.empty()) formals.push_back(string(tt));
                            }
                        }
                    }

                    // Now collect body, starting AFTER the first '{' we've found
                    string body;
                    int depth = 1;
                    for (size_t k = brace_pos + 1; k < accum.size(); ++k) {
                        char ch = accum[k];
                        if (ch == '{') { ++depth; continue; }
                        if (ch == '}') { --depth; if (depth == 0) goto BODY_DONE_ACCUM; }
                        body.push_back(ch);
                    }
                BODY_DONE_ACCUM:;

                    while (depth > 0 && li + 1 < raw_lines.size()) {
                        string nxt = raw_lines[++li];
                        for (char ch : nxt) {
                            if (ch == '{') { ++depth; continue; }
                            if (ch == '}') { --depth; if (depth == 0) goto BODY_DONE_LOOP; }
                            body.push_back(ch);
                        }
                        body.push_back('\n');
                    }
                BODY_DONE_LOOP:;

                    // Split body on semicolons outside parentheses
                    vector<string> body_lines;
                    {
                        size_t p = 0, last = 0; int d = 0;
                        while (p <= body.size()) {
                            bool at_end = (p == body.size());
                            char ch = at_end ? '\0' : body[p];
                            if (!at_end && ch == '(') ++d;
                            else if (!at_end && ch == ')') --d;
                            if (at_end || (ch == ';' && d == 0)) {
                                string t = trim(string_view(body).substr(last, p - last));
                                if (!t.empty()) body_lines.push_back(string(t) + ";");
                                last = p + 1;
                            }
                            ++p;
                        }
                    }

                    // Register the gate
                    if (!gname.empty()) {
                        CustomGate g(gname, formals, body_lines);
                        gate_registry.addGate(g);
                        vlog(verbose, "Registered custom gate: " + gname +
                            " (params=" + to_string(formals.size()) +
                            ", body=" + to_string(body_lines.size()) + ")");
                    }
                    continue;
                }

                // Any other line goes to nondef_lines for later canonical processing
                nondef_lines.push_back(line);
            }

            // ---- 4) Canonical expansion: expand user statements into primitive strings ----
            function<void(const string&,
                const unordered_map<string, string>&,
                vector<string>&)> expand_stmt_recursive;

            expand_stmt_recursive = [&](const string& stmt_in,
                const unordered_map<string, string>& subs,
                vector<string>& out_stmts) {
                    string s = trim(stmt_in);
                    if (s.empty()) return;
                    string sl = to_lower_ascii(s);

                    // normalize trailing ';'
                    auto ensure_semi = [](const string& t) {
                        if (!t.empty() && t.back() == ';') return t;
                        string r = t; r.push_back(';'); return r;
                        };

                    // ignore barrier/reset
                    if (sl.rfind("barrier", 0) == 0) { vlog(verbose, "skip barrier"); return; }
                    if (sl.rfind("reset", 0) == 0) { vlog(verbose, "skip reset");   return; }

                    // measure: "measure q[i]  c[j];" OR "c[j] = measure q[i];"
                    {
                        static regex rem1(R"(^\s*measure\s+(.+?)\s*->\s*(.+?)\s*;?$)", regex::icase);
                        static regex rem2(R"(^\s*(.+?)\s*=\s*measure\s+(.+?)\s*;?$)", regex::icase);
                        smatch m;
                        if (regex_match(s, m, rem1)) {
                            string q = trim(m[1].str());
                            string c = trim(m[2].str());
                            out_stmts.push_back(ensure_semi(c + " = measure " + q));
                            return;
                        }
                        if (regex_match(s, m, rem2)) {
                            out_stmts.push_back(ensure_semi(trim(m[1].str()) + " = measure " + trim(m[2].str())));
                            return;
                        }
                    }

                    // two-qubit: cx/cz/swap  ARG, ARG;
                    {
                        static regex r2(R"(^\s*(cx|cz|swap)\s+(.+?)\s*,\s*(.+?)\s*;?$)", regex::icase);
                        smatch m;
                        if (regex_match(s, m, r2)) {
                            string op = to_lower_ascii(m[1].str());
                            string a = trim(m[2].str());
                            string b = trim(m[3].str());
                            out_stmts.push_back(ensure_semi(op + " " + a + ", " + b));
                            return;
                        }
                    }

                    // one-qubit non-param: h/x/y/z/s/sdg/t/tdg/sx/sxdg q[i];
                    {
                        static regex r1(R"(^\s*(h|x|y|z|s|sdg|t|tdg|sx|sxdg)\s+(.+?)\s*;?$)", regex::icase);
                        smatch m;
                        if (regex_match(s, m, r1)) {
                            string op = to_lower_ascii(m[1].str());
                            string a = trim(m[2].str());
                            out_stmts.push_back(ensure_semi(op + " " + a));
                            return;
                        }
                    }

                    // param gates: rx/ry/rz/phase(angle) q[i];
                    {
                        static regex rp(R"(^\s*(rx|ry|rz|phase)\s*\(\s*(.+?)\s*\)\s+(.+?)\s*;?$)", regex::icase);
                        smatch m;
                        if (regex_match(s, m, rp)) {
                            string op = to_lower_ascii(m[1].str());
                            string expr = trim(m[2].str());
                            string a = trim(m[3].str());
                            // keep expression as-is for now (eval in emit stage)
                            out_stmts.push_back(ensure_semi(op + "(" + expr + ") " + a));
                            return;
                        }
                    }

                    // custom gate call: NAME args;
                    {
                        static regex rcall(R"(^\s*([A-Za-z_]\w*)\s+(.+?)\s*;?$)");
                        smatch m;
                        if (regex_match(s, m, rcall)) {
                            string name = to_lower_ascii(trim(m[1].str()));
                            string rest = trim(m[2].str());
                            if (gate_registry.hasGate(name)) {
                                vector<string> args = split_csv(rest);
                                auto expanded = gate_registry.getGate(name).expand(args);
                                for (auto& e : expanded) {
                                    expand_stmt_recursive(e, subs, out_stmts);
                                }
                                return;
                            }
                        }
                    }

                    // fallback: pass-through (kept as canonical line)
                    out_stmts.push_back(ensure_semi(s));
                };

            // ---- 5) Expand to canonical sequence (strings) ----
            auto canonical = StatementExpander::expand(nondef_lines, gate_registry, verbose);

            // ---- 6) Emit IR from canonical statements, preserving order (including IF) ----
            Program prog = IRBuilder::emit(canonical, qregs, cregs, verbose);

            return prog;
        }

    } // namespace frontend
} // namespace qbin_compiler
