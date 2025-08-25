#include "qbin_compiler/qasm_frontend.hpp"
#include "qbin_compiler/tools.hpp"

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

        // ---------- QASM2 gate definitions ----------
        struct GateDef {
            string name;                 // lower-case
            vector<string> qformals;     // e.g., q0, q1
            vector<string> pformals;     // e.g., theta, phi, lambda
            vector<string> body;         // statements without trailing ';'
        };

        static string substitute_idents(const string& s, const unordered_map<string, string>& subs) {
            string out; out.reserve(s.size());
            for (size_t i = 0; i < s.size();) {
                unsigned char ch = static_cast<unsigned char>(s[i]);
                if (std::isalpha(ch) || s[i] == '_') {
                    size_t j = i + 1;
                    while (j < s.size()) {
                        unsigned char cj = static_cast<unsigned char>(s[j]);
                        if (std::isalnum(cj) || s[j] == '_' || s[j] == '[' || s[j] == ']') ++j;
                        else break;
                    }
                    string tok = s.substr(i, j - i);
                    string key = to_lower_ascii(tok);
                    auto it = subs.find(key);
                    out += (it != subs.end()) ? it->second : tok;
                    i = j;
                }
                else {
                    out += s[i++];
                }
            }
            return out;
        }

        // Expand one statement into canonical primitives, logging along the way.
        static void expand_stmt_recursive(const string& stmt_in,
            const unordered_map<string, string>& subs,
            const map<string, GateDef>& gates,
            vector<string>& out_stmts,
            bool verbose) {
            string s = trim(stmt_in);
            if (s.empty()) return;

            s = substitute_idents(s, subs);
            string sl = to_lower_ascii(s);

            // ignore barrier/reset
            if (sl.rfind("barrier", 0) == 0) { vlog(verbose, "skip barrier"); return; }
            if (sl.rfind("reset", 0) == 0) { vlog(verbose, "skip reset");   return; }

            // U(theta,phi,lambda) q;
            {
                static regex reU(R"(^u\s*\(\s*([^,]+)\s*,\s*([^,]+)\s*,\s*([^)]+)\)\s+([A-Za-z_][A-Za-z0-9_\[\]]*)\s*;?$)", regex::icase);
                smatch m;
                if (regex_match(s, m, reU)) {
                    double th = eval_expr(m[1].str());
                    double ph = eval_expr(m[2].str());
                    double la = eval_expr(m[3].str());
                    string q = m[4].str();
                    out_stmts.push_back("rz(" + to_string(ph) + ") " + q + ";");
                    out_stmts.push_back("ry(" + to_string(th) + ") " + q + ";");
                    out_stmts.push_back("rz(" + to_string(la) + ") " + q + ";");
                    vlog(verbose, "expand U(...) on " + q + " -> rz,ry,rz");
                    return;
                }
            }

            // cx a,b;
            {
                static regex recx(R"(^cx\s+([A-Za-z_][A-Za-z0-9_\[\]]*)\s*,\s*([A-Za-z_][A-Za-z0-9_\[\]]*)\s*;?$)", regex::icase);
                smatch m;
                if (regex_match(s, m, recx)) {
                    out_stmts.push_back("cx " + m[1].str() + ", " + m[2].str() + ";");
                    vlog(verbose, "emit cx " + m[1].str() + "," + m[2].str());
                    return;
                }
            }

            // 1q with angle: rz/ry/rx/phase
            {
                static regex reang(R"(^\s*(rz|ry|rx|phase)\s*\(\s*([^)]+)\)\s+([A-Za-z_][A-Za-z0-9_\[\]]*)\s*;?$)", regex::icase);
                smatch m;
                if (regex_match(s, m, reang)) {
                    out_stmts.push_back(m[1].str() + "(" + m[2].str() + ") " + m[3].str() + ";");
                    vlog(verbose, "emit angle1 " + to_lower_ascii(m[1].str()) + " " + m[3].str());
                    return;
                }
            }

            // 1q no-angle
            {
                static regex re1q(R"(^\s*(x|y|z|h|s|sdg|t|tdg|sx|sxdg)\s+([A-Za-z_][A-Za-z0-9_\[\]]*)\s*;?$)", regex::icase);
                smatch m;
                if (regex_match(s, m, re1q)) {
                    out_stmts.push_back(to_lower_ascii(m[1].str()) + " " + m[2].str() + ";");
                    vlog(verbose, "emit 1q " + to_lower_ascii(m[1].str()) + " " + m[2].str());
                    return;
                }
            }

            // measure arrow or assignment
            {
                static regex rem1(R"(^\s*measure\s+([A-Za-z_][A-Za-z0-9_\[\]]*)\s*->\s*([A-Za-z_][A-Za-z0-9_\[\]]*)\s*;?$)", regex::icase);
                static regex rem2(R"(^\s*([A-Za-z_][A-Za-z0-9_\[\]]*)\s*=\s*measure\s+([A-Za-z_][A-Za-z0-9_\[\]]*)\s*;?$)", regex::icase);
                smatch m;
                if (regex_match(s, m, rem1)) { out_stmts.push_back(m[2].str() + " = measure " + m[1].str() + ";"); vlog(verbose, "emit measure (arrow)"); return; }
                if (regex_match(s, m, rem2)) { out_stmts.push_back(m[1].str() + " = measure " + m[2].str() + ";"); vlog(verbose, "emit measure (assign)"); return; }
            }

            // Robust nested gate call: NAME[(params)] qargs;
            {
                static regex re_name_only(R"(^\s*([A-Za-z_][A-Za-z0-9_]*)\s*(.*?);?\s*$)");
                smatch m;
                if (regex_match(s, m, re_name_only)) {
                    string name = to_lower_ascii(trim(m[1].str()));
                    auto it = gates.find(name);
                    if (it != gates.end()) {
                        string rest = trim(m[2].str());

                        // Extract "( ... )" at start if present
                        string param_str, qubits_str = rest;
                        if (!rest.empty() && rest[0] == '(') {
                            size_t close = find_matching_paren(rest, 0);
                            if (close != string::npos) {
                                param_str = rest.substr(1, close - 1);
                                qubits_str = trim(rest.substr(close + 1));
                            }
                        }

                        // Split qubits by comma
                        vector<string> qargs = split_commas(qubits_str, /*respect_parens*/true);

                        // Build substitutions
                        unordered_map<string, string> subs2 = subs;

                        // Map params if gate has formals
                        if (!it->second.pformals.empty() && !param_str.empty()) {
                            vector<string> pvals = split_commas(param_str, /*respect_parens*/true);
                            for (size_t k = 0; k < it->second.pformals.size() && k < pvals.size(); ++k) {
                                subs2[to_lower_ascii(it->second.pformals[k])] = pvals[k];
                            }
                        }

                        // Map qubit formals
                        for (size_t i2 = 0; i2 < it->second.qformals.size() && i2 < qargs.size(); ++i2) {
                            subs2[to_lower_ascii(it->second.qformals[i2])] = qargs[i2];
                        }

                        vlog(verbose, string("expand call: ") + name +
                            " p=" + to_string(it->second.pformals.size()) +
                            " q=" + to_string(it->second.qformals.size()) +
                            " with args q=" + to_string(qargs.size()));

                        // Recurse into body
                        for (const auto& st : it->second.body) {
                            expand_stmt_recursive(st, subs2, gates, out_stmts, verbose);
                        }
                        return;
                    }
                    else {
                        vlog(verbose, string("unknown gate call: ") + name + " (no def)");
                    }
                }
            }

            // Pass-through
            out_stmts.push_back(s.back() == ';' ? s : s + ";");
            vlog(verbose, "pass-through stmt");
        }

        // ---------- main parser ----------
        Program parse_qasm_subset(std::string_view text, bool verbose) {
            string src(text);

            // Normalize lines and strip // comments.
            vector<string> raw_lines;
            raw_lines.reserve(src.size() / 16 + 8);
            {
                string cur; cur.reserve(256);
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

            // Reg tables
            struct Reg { int offset = 0; int size = 0; };
            map<string, Reg> qregs, cregs;
            int q_total = 0, c_total = 0;

            // Gate defs
            map<string, GateDef> gates;

            // First pass: collect regs, gate defs, and keep non-definition lines
            vector<string> nondef_lines;
            for (size_t li = 0; li < raw_lines.size(); ++li) {
                string line = trim(raw_lines[li]);
                if (line.empty()) continue;
                string ll = to_lower_ascii(line);

                if (ll.rfind("openqasm", 0) == 0) {
                    vlog(verbose, "header: " + line);
                    continue;
                }
                if (ll.rfind("include", 0) == 0) {
                    vlog(verbose, "include: " + line);
                    continue;
                }

                // qreg / creg
                {
                    static regex req(R"(^qreg\s+([A-Za-z_][A-Za-z0-9_]*)\[(\d+)\]\s*;?$)", regex::icase);
                    smatch m;
                    if (regex_match(line, m, req)) {
                        string name = to_lower_ascii(m[1].str());
                        int n = stoi(m[2].str());
                        qregs[name] = Reg{ q_total, n }; q_total += n;
                        vlog(verbose, "qreg " + name + "[" + to_string(n) + "] -> offset " + to_string(qregs[name].offset));
                        continue;
                    }
                }
                {
                    static regex rec(R"(^creg\s+([A-Za-z_][A-Za-z0-9_]*)\[(\d+)\]\s*;?$)", regex::icase);
                    smatch m;
                    if (regex_match(line, m, rec)) {
                        string name = to_lower_ascii(m[1].str());
                        int n = stoi(m[2].str());
                        cregs[name] = Reg{ c_total, n }; c_total += n;
                        vlog(verbose, "creg " + name + "[" + to_string(n) + "] -> offset " + to_string(cregs[name].offset));
                        continue;
                    }
                }

                // Gate definition: handle braces on same line and across lines
                if (ll.rfind("gate ", 0) == 0) {
                    string header = line;
                    while (header.find('{') == string::npos && li + 1 < raw_lines.size()) {
                        header += " " + trim(raw_lines[++li]);
                    }
                    size_t brace_pos = header.find('{');
                    if (brace_pos == string::npos) continue;

                    // Parse head
                    static regex rehead(R"(^gate\s+([A-Za-z_][A-Za-z0-9_]*)\s*(\(([^)]*)\))?\s+(.+)$)", regex::icase);
                    smatch mh;
                    string head_part = header.substr(0, brace_pos);
                    if (!regex_match(head_part, mh, rehead)) continue;

                    GateDef gd;
                    gd.name = to_lower_ascii(trim(mh[1].str()));
                    string params = mh[3].str();
                    string qargs = trim(mh[4].str());

                    if (!params.empty()) {
                        for (auto& t : split_commas(params, /*respect_parens*/false)) gd.pformals.push_back(t);
                    }
                    if (!qargs.empty()) {
                        for (auto& t : split_commas(qargs, /*respect_parens*/false)) gd.qformals.push_back(t);
                    }

                    // Collect body from remainder of header line and following lines
                    string body;
                    int depth = 1;
                    for (size_t k = brace_pos + 1; k < header.size(); ++k) {
                        char ch = header[k];
                        if (ch == '{') { ++depth; continue; }
                        if (ch == '}') { --depth; if (depth == 0) break; else continue; }
                        body.push_back(ch);
                    }
                    while (depth > 0 && li + 1 < raw_lines.size()) {
                        string nxt = raw_lines[++li];
                        for (size_t k = 0; k < nxt.size(); ++k) {
                            char ch = nxt[k];
                            if (ch == '{') { ++depth; continue; }
                            if (ch == '}') { --depth; if (depth == 0) { continue; } }
                            if (depth >= 1) body.push_back(ch);
                        }
                        if (depth >= 1) body.push_back('\n');
                    }

                    // Split body by ';' respecting parentheses
                    {
                        size_t p = 0, last = 0; int depthP = 0;
                        while (p <= body.size()) {
                            bool at_end = (p == body.size());
                            char ch = at_end ? '\0' : body[p];
                            if (!at_end && ch == '(') ++depthP;
                            else if (!at_end && ch == ')') --depthP;
                            if (at_end || (ch == ';' && depthP == 0)) {
                                string t = trim(string_view(body).substr(last, p - last));
                                if (!t.empty()) gd.body.push_back(t);
                                last = p + 1;
                            }
                            ++p;
                        }
                    }

                    vlog(verbose, "gate def: " + gd.name +
                        " p=" + to_string(gd.pformals.size()) +
                        " q=" + to_string(gd.qformals.size()) +
                        " stmts=" + to_string(gd.body.size()));
                    gates[gd.name] = std::move(gd);
                    continue;
                }

                nondef_lines.push_back(line);
            }

            Program prog;
            prog.max_qubit = q_total - 1;
            prog.max_bit = c_total - 1;

            auto resolve_qubit = [&](const string& token)->int {
                static regex r(R"(^([A-Za-z_][A-Za-z0-9_]*)\[(\d+)\]$)");
                smatch m;
                if (!regex_match(token, m, r)) return -1;
                string reg = to_lower_ascii(m[1].str());
                int idx = stoi(m[2].str());
                auto it = qregs.find(reg); if (it == qregs.end()) return -1;
                return it->second.offset + idx;
                };
            auto resolve_bit = [&](const string& token)->int {
                static regex r(R"(^([A-Za-z_][A-Za-z0-9_]*)\[(\d+)\]$)");
                smatch m;
                if (!regex_match(token, m, r)) return -1;
                string reg = to_lower_ascii(m[1].str());
                int idx = stoi(m[2].str());
                auto it = cregs.find(reg); if (it == cregs.end()) return -1;
                return it->second.offset + idx;
                };

            // Second pass: expand and emit
            vector<string> canonical;
            canonical.reserve(nondef_lines.size() * 2);

            for (const auto& line : nondef_lines) {
                string s = trim(line);
                if (s.empty()) continue;

                // QASM 3 style decls: qubit[N] name; bit[M] name;
                {
                    static regex rq(R"(^qubit\s*\[(\d+)\]\s*([A-Za-z_][A-Za-z0-9_]*)\s*;?$)", regex::icase);
                    smatch m;
                    if (regex_match(s, m, rq)) {
                        string name = to_lower_ascii(m[2].str()); int n = stoi(m[1].str());
                        if (!qregs.count(name)) { qregs[name] = { q_total, n }; q_total += n; prog.max_qubit = q_total - 1; }
                        vlog(verbose, "qubit decl: " + name + "[" + to_string(n) + "]");
                        continue;
                    }
                }
                {
                    static regex rb(R"(^bit\s*\[(\d+)\]\s*([A-Za-z_][A-Za-z0-9_]*)\s*;?$)", regex::icase);
                    smatch m;
                    if (regex_match(s, m, rb)) {
                        string name = to_lower_ascii(m[2].str()); int n = stoi(m[1].str());
                        if (!cregs.count(name)) { cregs[name] = { c_total, n }; c_total += n; prog.max_bit = c_total - 1; }
                        vlog(verbose, "bit decl: " + name + "[" + to_string(n) + "]");
                        continue;
                    }
                }

                // measure arrow or assignment to canonical form
                {
                    static regex rm1(R"(^measure\s+([A-Za-z_][A-Za-z0-9_\[\]]*)\s*->\s*([A-Za-z_][A-Za-z0-9_\[\]]*)\s*;?$)", regex::icase);
                    static regex rm2(R"(^([A-Za-z_][A-Za-z0-9_\[\]]*)\s*=\s*measure\s+([A-Za-z_][A-Za-z0-9_\[\]]*)\s*;?$)", regex::icase);
                    smatch m;
                    if (regex_match(s, m, rm1)) { canonical.push_back(m[2].str() + " = measure " + m[1].str() + ";"); vlog(verbose, "measure arrow -> canonical"); continue; }
                    if (regex_match(s, m, rm2)) { canonical.push_back(m[1].str() + " = measure " + m[2].str() + ";"); vlog(verbose, "measure assign canonical"); continue; }
                }

                // Expand to canonical primitives
                vector<string> expanded;
                expand_stmt_recursive(s, unordered_map<string, string>{}, gates, expanded, verbose);
                if (expanded.empty()) vlog(verbose, "expansion produced 0 statements for: " + s.substr(0, 64));
                canonical.insert(canonical.end(), expanded.begin(), expanded.end());
            }

            vlog(verbose, "canonical statements: " + to_string(canonical.size()));

            // Emit IR
            for (const auto& st : canonical) {
                smatch m;
                // measure
                {
                    static regex r(R"(^([A-Za-z_][A-Za-z0-9_\[\]]*)\s*=\s*measure\s+([A-Za-z_][A-Za-z0-9_\[\]]*)\s*;?$)", regex::icase);
                    if (regex_match(st, m, r)) {
                        int q = resolve_qubit(m[2].str());
                        int c = resolve_bit(m[1].str());
                        if (q >= 0 && c >= 0) { emit_measure(prog.code, q, c); }
                        else vlog(verbose, "measure resolve failed: " + st);
                        continue;
                    }
                }
                // cx
                {
                    static regex r(R"(^cx\s+([A-Za-z_][A-Za-z0-9_\[\]]*)\s*,\s*([A-Za-z_][A-Za-z0-9_\[\]]*)\s*;?$)", regex::icase);
                    if (regex_match(st, m, r)) {
                        int a = resolve_qubit(m[1].str());
                        int b = resolve_qubit(m[2].str());
                        if (a >= 0 && b >= 0) { emit_2q(prog.code, Op::CX, a, b); }
                        else vlog(verbose, "cx resolve failed: " + st);
                        continue;
                    }
                }
                // 1q with angle
                {
                    static regex r(R"(^\s*(rz|ry|rx|phase)\s*\(\s*([^)]+)\)\s+([A-Za-z_][A-Za-z0-9_\[\]]*)\s*;?$)", regex::icase);
                    if (regex_match(st, m, r)) {
                        double ang = eval_expr(m[2].str());
                        int a = resolve_qubit(m[3].str());
                        if (a >= 0) {
                            string g = to_lower_ascii(m[1].str());
                            Op op = Op::RZ;
                            if (g == "rz") op = Op::RZ;
                            else if (g == "ry") op = Op::RY;
                            else if (g == "rx") op = Op::RX;
                            else op = Op::PHASE;
                            emit_angle(prog.code, op, a, ang);
                        }
                        else vlog(verbose, "1q angle resolve failed: " + st);
                        continue;
                    }
                }
                // 1q no-angle
                {
                    static regex r(R"(^\s*(x|y|z|h|s|sdg|t|tdg|sx|sxdg)\s+([A-Za-z_][A-Za-z0-9_\[\]]*)\s*;?$)", regex::icase);
                    if (regex_match(st, m, r)) {
                        int a = resolve_qubit(m[2].str());
                        if (a >= 0) {
                            string g = to_lower_ascii(m[1].str());
                            Op op = Op::X;
                            if (g == "x") op = Op::X;
                            else if (g == "y") op = Op::Y;
                            else if (g == "z") op = Op::Z;
                            else if (g == "h") op = Op::H;
                            else if (g == "s") op = Op::S;
                            else if (g == "sdg") op = Op::SDG;
                            else if (g == "t") op = Op::T;
                            else if (g == "tdg") op = Op::TDG;
                            else if (g == "sx") op = Op::SX;
                            else if (g == "sxdg") op = Op::SXDG;
                            emit_1q(prog.code, op, a);
                        }
                        else vlog(verbose, "1q resolve failed: " + st);
                        continue;
                    }
                }
                // ignore barrier/reset
                {
                    static regex rb(R"(^\s*(barrier|reset)\b)", regex::icase);
                    if (regex_search(st, rb)) continue;
                }
                vlog(verbose, "ignored stmt: " + st.substr(0, 64));
            }

            vlog(verbose, "emitted IR instructions: " + to_string(prog.code.size()));
            return prog;
        }

    }
} // namespace
