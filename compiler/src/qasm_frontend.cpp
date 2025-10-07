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

        // Split a comma-separated list respecting parentheses
        static vector<string> split_csv(const string& s) {
            vector<string> out;
            string cur;
            int depth = 0;
            for (size_t i = 0; i < s.size(); ++i) {
                char ch = s[i];
                if (ch == '(') {
                    ++depth;
                    cur.push_back(ch);
                }
                else if (ch == ')') {
                    --depth;
                    cur.push_back(ch);
                }
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

            unordered_map<string, pair<int, int>> qregs;
            unordered_map<string, pair<int, int>> cregs;
            int q_base = 0, c_base = 0;

            vector<string> nondef_lines;

            for (size_t li = 0; li < raw_lines.size(); ++li) {
                string line = trim(raw_lines[li]);
                if (line.empty()) continue;
                string ll = to_lower_ascii(line);

                if (ll.rfind("openqasm", 0) == 0) continue;
                if (ll.rfind("include", 0) == 0) continue;

                // qreg/creg declarations
                {
                    static regex rq1(R"(^\s*qubit\s*\[\s*(\d+)\s*\]\s*([A-Za-z_]\w*)\s*;?\s*$)", regex::icase);
                    static regex rc1(R"(^\s*bit\s*\[\s*(\d+)\s*\]\s*([A-Za-z_]\w*)\s*;?\s*$)", regex::icase);
                    static regex rq2(R"(^\s*qreg\s+([A-Za-z_]\w*)\s*\[\s*(\d+)\s*\]\s*;?\s*$)", regex::icase);
                    static regex rc2(R"(^\s*creg\s+([A-Za-z_]\w*)\s*\[\s*(\d+)\s*\]\s*;?\s*$)", regex::icase);
                    smatch m;
                    if (regex_match(line, m, rq1)) {
                        int sz = stoi(m[1].str());
                        string nm = to_lower_ascii(m[2].str());
                        qregs[nm] = { q_base, sz };
                        q_base += sz;
                        continue;
                    }
                    if (regex_match(line, m, rc1)) {
                        int sz = stoi(m[1].str());
                        string nm = to_lower_ascii(m[2].str());
                        cregs[nm] = { c_base, sz };
                        c_base += sz;
                        continue;
                    }
                    if (regex_match(line, m, rq2)) {
                        string nm = to_lower_ascii(m[1].str());
                        int sz = stoi(m[2].str());
                        qregs[nm] = { q_base, sz };
                        q_base += sz;
                        continue;
                    }
                    if (regex_match(line, m, rc2)) {
                        string nm = to_lower_ascii(m[1].str());
                        int sz = stoi(m[2].str());
                        cregs[nm] = { c_base, sz };
                        c_base += sz;
                        continue;
                    }
                }

                // ---------- Custom gate definition ----------
                if (ll.rfind("gate ", 0) == 0) {
                    string accum = line;
                    while (accum.find('{') == string::npos && li + 1 < raw_lines.size()) {
                        accum.push_back(' ');
                        accum += trim(raw_lines[++li]);
                    }

                    size_t brace_pos = accum.find('{');
                    if (brace_pos == string::npos) {
                        vlog(verbose, "Malformed gate header (no '{'): " + accum);
                        continue;
                    }

                    string head = trim(accum.substr(0, brace_pos));

                    // Two patterns: with params and without
                    static regex re_with_params(
                        R"(^\s*gate\s+([A-Za-z_]\w*)\s*\((.*?)\)\s+([A-Za-z0-9_,\s\[\]]+)\s*$)",
                        regex::icase);
                    static regex re_no_params(
                        R"(^\s*gate\s+([A-Za-z_]\w*)\s+([A-Za-z0-9_,\s\[\]]+)\s*$)",
                        regex::icase);

                    smatch mh;
                    string gname;
                    vector<string> params, qubits;

                    if (regex_match(head, mh, re_with_params)) {
                        gname = to_lower_ascii(trim(mh[1].str()));
                        string plist = trim(mh[2].str());
                        string qlist = trim(mh[3].str());

                        if (!plist.empty()) {
                            stringstream ss(plist);
                            string t;
                            while (getline(ss, t, ',')) {
                                string tt = trim(t);
                                if (!tt.empty()) params.push_back(tt);
                            }
                        }
                        if (!qlist.empty()) {
                            stringstream ss(qlist);
                            string t;
                            while (getline(ss, t, ',')) {
                                string tt = trim(t);
                                if (!tt.empty()) qubits.push_back(tt);
                            }
                        }
                    }
                    else if (regex_match(head, mh, re_no_params)) {
                        gname = to_lower_ascii(trim(mh[1].str()));
                        string qlist = trim(mh[2].str());
                        if (!qlist.empty()) {
                            stringstream ss(qlist);
                            string t;
                            while (getline(ss, t, ',')) {
                                string tt = trim(t);
                                if (!tt.empty()) qubits.push_back(tt);
                            }
                        }
                    }
                    else {
                        vlog(verbose, "Failed to parse gate header: " + head);
                    }

                    // ----- collect gate body -----
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

                    // split body into lines
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

                    if (!gname.empty()) {
                        vector<string> formals;
                        formals.insert(formals.end(), params.begin(), params.end());
                        formals.insert(formals.end(), qubits.begin(), qubits.end());
                        CustomGate g(gname, formals, body_lines);
                        gate_registry.addGate(g);
                        vlog(verbose, "Registered custom gate: " + gname +
                            " (params=" + to_string(params.size()) +
                            ", body=" + to_string(body_lines.size()) + ")");
                    }
                    continue;
                }


                nondef_lines.push_back(line);
            }

            // Canonical expansion
            function<void(const string&, const unordered_map<string, string>&, vector<string>&)> expand_stmt_recursive;

            expand_stmt_recursive = [&](const string& stmt_in,
                const unordered_map<string, string>& subs,
                vector<string>& out_stmts) {
                    string s = trim(stmt_in);
                    if (s.empty()) return;
                    string sl = to_lower_ascii(s);

                    auto ensure_semi = [](const string& t) {
                        if (!t.empty() && t.back() == ';') return t;
                        string r = t; r.push_back(';'); return r;
                        };

                    if (sl.rfind("barrier", 0) == 0) return;
                    if (sl.rfind("reset", 0) == 0) return;

                    // measure
                    {
                        static regex rem1(R"(^\s*measure\s+(.+?)\s*->\s*(.+?)\s*;?$)", regex::icase);
                        static regex rem2(R"(^\s*(.+?)\s*=\s*measure\s+(.+?)\s*;?$)", regex::icase);
                        smatch m;
                        if (regex_match(s, m, rem1)) {
                            out_stmts.push_back(ensure_semi(trim(m[2].str()) + " = measure " + trim(m[1].str())));
                            return;
                        }
                        if (regex_match(s, m, rem2)) {
                            out_stmts.push_back(ensure_semi(trim(m[1].str()) + " = measure " + trim(m[2].str())));
                            return;
                        }
                    }

                    // two-qubit
                    {
                        static regex r2(R"(^\s*(cx|cz|swap)\s+(.+?)\s*,\s*(.+?)\s*;?$)", regex::icase);
                        smatch m;
                        if (regex_match(s, m, r2)) {
                            string op = to_lower_ascii(m[1].str());
                            out_stmts.push_back(ensure_semi(op + " " + trim(m[2].str()) + ", " + trim(m[3].str())));
                            return;
                        }
                    }

                    // one-qubit non-param
                    {
                        static regex r1(R"(^\s*(h|x|y|z|s|sdg|t|tdg|sx|sxdg)\s+(.+?)\s*;?$)", regex::icase);
                        smatch m;
                        if (regex_match(s, m, r1)) {
                            string op = to_lower_ascii(m[1].str());
                            out_stmts.push_back(ensure_semi(op + " " + trim(m[2].str())));
                            return;
                        }
                    }

                    // param rx/ry/rz/phase
                    {
                        static regex rp(R"(^\s*(rx|ry|rz|phase)\s*\(\s*(.+?)\s*\)\s+(.+?)\s*;?$)", regex::icase);
                        smatch m;
                        if (regex_match(s, m, rp)) {
                            string op = to_lower_ascii(m[1].str());
                            out_stmts.push_back(ensure_semi(op + "(" + trim(m[2].str()) + ") " + trim(m[3].str())));
                            return;
                        }
                    }

                    // u-family gates
                    {
                        static regex ru3(R"(^\s*(u|u3)\s*\(\s*([^,]+)\s*,\s*([^,]+)\s*,\s*([^)]+)\)\s+(.+?)\s*;?$)", regex::icase);
                        static regex ru2(R"(^\s*u2\s*\(\s*([^,]+)\s*,\s*([^)]+)\)\s+(.+?)\s*;?$)", regex::icase);
                        static regex ru1(R"(^\s*u1\s*\(\s*([^)]+)\)\s+(.+?)\s*;?$)", regex::icase);
                        smatch m;
                        if (regex_match(s, m, ru3)) {
                            string theta = trim(m[2].str());
                            string phi = trim(m[3].str());
                            string lambda = trim(m[4].str());
                            string q = trim(m[5].str());
                            out_stmts.push_back(ensure_semi("rz(" + phi + ") " + q));
                            out_stmts.push_back(ensure_semi("ry(" + theta + ") " + q));
                            out_stmts.push_back(ensure_semi("rz(" + lambda + ") " + q));
                            return;
                        }
                        if (regex_match(s, m, ru2)) {
                            string phi = trim(m[1].str());
                            string lambda = trim(m[2].str());
                            string q = trim(m[3].str());
                            out_stmts.push_back(ensure_semi("rz(" + phi + ") " + q));
                            out_stmts.push_back(ensure_semi("ry(pi/2) " + q));
                            out_stmts.push_back(ensure_semi("rz(" + lambda + ") " + q));
                            return;
                        }
                        if (regex_match(s, m, ru1)) {
                            string lambda = trim(m[1].str());
                            string q = trim(m[2].str());
                            out_stmts.push_back(ensure_semi("rz(" + lambda + ") " + q));
                            return;
                        }
                    }

                    // custom gate call
                    {
                        static regex rcall(R"(^\s*([A-Za-z_]\w*)\s+(.+?)\s*;?$)");
                        smatch m;
                        if (regex_match(s, m, rcall)) {
                            string name = to_lower_ascii(trim(m[1].str()));
                            string rest = trim(m[2].str());
                            if (gate_registry.hasGate(name)) {
                                vector<string> args = split_csv(rest);
                                auto expanded = gate_registry.getGate(name).expand(args);
                                for (auto& e : expanded)
                                    expand_stmt_recursive(e, subs, out_stmts);
                                return;
                            }
                        }
                    }

                    out_stmts.push_back(ensure_semi(s));
                };

            auto canonical = StatementExpander::expand(nondef_lines, gate_registry, verbose);
            Program prog = IRBuilder::emit(canonical, qregs, cregs, verbose);
            return prog;
        }

    } // namespace frontend
} // namespace qbin_compiler
