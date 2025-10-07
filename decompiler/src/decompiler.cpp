#include "qbin_compiler/decompiler.hpp"
#include "qbin_compiler/tools.hpp"

#include <regex>
#include <string>
#include <vector>
#include <sstream>

using std::string;
using std::vector;
using std::regex;
using std::smatch;

using qbin_compiler::util::trim;
using qbin_compiler::util::to_lower_ascii;
using qbin_compiler::util::vlog;

namespace qbin_compiler {
    namespace frontend {

        // Small helper: ensure we re-emit statements with a trailing semicolon (if needed).
        static inline string ensure_semi(const string& t) {
            if (!t.empty() && t.back() == ';') return t;
            string r = t;
            r.push_back(';');
            return r;
        }

        // Tokenizes a single "gate(argument) target" like statement
        // rx( theta_expr ) q[0]  -> op="rx", expr="theta_expr", qubit="q[0]"
        struct OneQParam {
            string op;
            string expr;
            string q;
        };

        static bool parse_oneq_param(const string& s, OneQParam& out) {
            static regex re(R"(^\s*([A-Za-z_]\w*)\s*\(\s*(.+?)\s*\)\s+(.+?)\s*;?\s*$)", std::regex::icase);
            smatch m;
            if (!std::regex_match(s, m, re)) return false;
            out.op = to_lower_ascii(trim(m[1].str()));
            out.expr = trim(m[2].str());
            out.q = trim(m[3].str());
            return true;
        }

        // Tokenizes "op target" (no params), e.g. "x q[0]"
        struct OneQ {
            string op;
            string q;
        };

        static bool parse_oneq(const string& s, OneQ& out) {
            static regex re(R"(^\s*([A-Za-z_]\w*)\s+(.+?)\s*;?\s*$)", std::regex::icase);
            smatch m;
            if (!std::regex_match(s, m, re)) return false;
            out.op = to_lower_ascii(trim(m[1].str()));
            out.q = trim(m[2].str());
            return true;
        }

        // Matches rz/ry/rz sequence that we can lift back to a u-family gate.
        static bool try_lift_sequence_to_u(const vector<string>& in, size_t i, string& out_stmt, bool verbose) {
            // Case A: u3 / u (theta, phi, lambda): rz(phi) q; ry(theta) q; rz(lambda) q;
            if (i + 2 < in.size()) {
                OneQParam a, b, c;
                if (parse_oneq_param(in[i], a) && parse_oneq_param(in[i + 1], b) && parse_oneq_param(in[i + 2], c)) {
                    if (a.op == "rz" && b.op == "ry" && c.op == "rz" && a.q == b.q && b.q == c.q) {
                        const string& phi = a.expr;
                        const string& theta = b.expr;
                        const string& lambda = c.expr;

                        // Specialize u2 if the middle is exactly pi/2
                        if (to_lower_ascii(trim(theta)) == "pi/2") {
                            out_stmt = ensure_semi("u2(" + phi + ", " + lambda + ") " + a.q);
                            vlog(verbose, "decompiler: lifted rz/ry(pi/2)/rz -> u2");
                            return true;
                        }

                        // Otherwise u3
                        out_stmt = ensure_semi("u3(" + theta + ", " + phi + ", " + lambda + ") " + a.q);
                        vlog(verbose, "decompiler: lifted rz/ry/rz -> u3");
                        return true;
                    }
                }
            }

            // Case B: u1(lambda): single rz(lambda) q;
            {
                OneQParam a;
                if (parse_oneq_param(in[i], a) && a.op == "rz") {
                    out_stmt = ensure_semi("u1(" + a.expr + ") " + a.q);
                    vlog(verbose, "decompiler: lifted rz -> u1");
                    return true;
                }
            }

            return false;
        }

        // Returns true if s looks like a custom gate definition header (with or without params).
        static bool is_custom_gate_header(const string& s) {
            static regex re_with_params(R"(^\s*gate\s+[A-Za-z_]\w*\s*\(.*\)\s+[A-Za-z0-9_,\s\[\]]+\s*\{\s*$)", std::regex::icase);
            static regex re_no_params(R"(^\s*gate\s+[A-Za-z_]\w*\s+[A-Za-z0-9_,\s\[\]]+\s*\{\s*$)", std::regex::icase);
            return std::regex_match(s, re_with_params) || std::regex_match(s, re_no_params);
        }

        // Returns true if s looks like the closing brace of a custom gate definition body.
        static bool is_closing_brace(const string& s) {
            static regex re(R"(^\s*\}\s*;?\s*$)");
            return std::regex_match(s, re);
        }

        // True if looks like qreg/creg declarations
        static bool is_reg_decl(const string& s) {
            static regex re(R"(^\s*(qreg|creg)\s+[A-Za-z_]\w*\s*\[\s*\d+\s*\]\s*;?\s*$)", std::regex::icase);
            return std::regex_match(s, re);
        }

        // True if looks like measurement in either syntax
        static bool is_measure_stmt(const string& s) {
            static regex re1(R"(^\s*measure\s+.+?\s*->\s*.+?\s*;?\s*$)", std::regex::icase);
            static regex re2(R"(^\s*.+?\s*=\s*measure\s+.+?\s*;?\s*$)", std::regex::icase);
            return std::regex_match(s, re1) || std::regex_match(s, re2);
        }

        // True if looks like reset or barrier
        static bool is_reset_or_barrier(const string& s) {
            static regex re(R"(^\s*(reset|barrier)\b.*;?\s*$)", std::regex::icase);
            return std::regex_match(s, re);
        }

        // True if looks like 2q / 3q primitive we keep as-is (cx, cz, swap, ccx)
        static bool is_multiq_primitive(const string& s) {
            static regex re2(R"(^\s*(cx|cz|swap)\s+.+,\s*.+\s*;?\s*$)", std::regex::icase);
            static regex re3(R"(^\s*(ccx)\s+.+,\s*.+,\s*.+\s*;?\s*$)", std::regex::icase);
            return std::regex_match(s, re2) || std::regex_match(s, re3);
        }

        // True if looks like a simple 1q gate we keep as-is (h/x/y/z/s/sdg/t/tdg/sx/sxdg)
        static bool is_oneq_simple(const string& s) {
            static regex re(R"(^\s*(h|x|y|z|s|sdg|t|tdg|sx|sxdg)\s+.+?\s*;?\s*$)", std::regex::icase);
            return std::regex_match(s, re);
        }

        // True if looks like a custom gate call (with or without param list)
        static bool is_custom_gate_call(const string& s) {
            static regex re_param(R"(^\s*[A-Za-z_]\w*\s*\(.*\)\s+.+?\s*;?\s*$)");
            static regex re_simple(R"(^\s*[A-Za-z_]\w*\s+.+?\s*;?\s*$)");
            // Guard out known primitives so they don't get misclassified
            // (match order in expander: rx/ry/rz/phase/u/u1/u2/u3/h/x/.../cx/cz/swap/ccx/measure/reset/barrier)
            if (is_measure_stmt(s) || is_reset_or_barrier(s) || is_oneq_simple(s) || is_multiq_primitive(s))
                return false;

            // 1q param primitives
            OneQParam op;
            if (parse_oneq_param(s, op)) {
                const string& name = op.op;
                if (name == "rx" || name == "ry" || name == "rz" || name == "phase" ||
                    name == "u" || name == "u1" || name == "u2" || name == "u3")
                    return false;
            }

            // It's a gate-ish statement and not a known primitive → custom gate call.
            return std::regex_match(s, re_param) || std::regex_match(s, re_simple);
        }

        // Core pass: collapse canonical primitives back to higher-level u-family where possible.
        static vector<string> lift_to_u_family(const vector<string>& canonical, bool verbose) {
            vector<string> out;
            out.reserve(canonical.size());

            for (size_t i = 0; i < canonical.size(); ) {
                string lifted;
                if (try_lift_sequence_to_u(canonical, i, lifted, verbose)) {
                    out.push_back(lifted);
                    // We consumed either 3 (u3/u2 case) or 1 (u1 case) statements.
                    if (lifted.rfind("u1", 0) == 0) {
                        ++i;
                    }
                    else {
                        i += 3;
                    }
                    continue;
                }

                // Otherwise keep as-is
                out.push_back(ensure_semi(trim(canonical[i])));
                ++i;
            }
            return out;
        }

        // Public: main entry for decompilation from canonical IR-like text back to QASM-ish text.
        // This function assumes the input is the *expanded* canonical sequence (as produced by the expander).
        // It reconstructs higher-level constructs where safe and leaves custom gate syntax intact.
        static vector<string> decompile_lines(const vector<string>& expanded_lines, bool verbose) {
            vector<string> result;
            result.reserve(expanded_lines.size());

            // Pass 1: Preserve block structures and obvious statements while normalizing semicolons/whitespace.
            // We also gather lines to try the "u-family" lift across boundaries.
            vector<string> normalized;
            normalized.reserve(expanded_lines.size());

            for (const auto& raw : expanded_lines) {
                string s = trim(raw);
                if (s.empty()) continue;

                // Keep gate definition blocks exactly as they are (including braces).
                if (is_custom_gate_header(s) || is_closing_brace(s)) {
                    normalized.push_back(s);
                    continue;
                }

                // Keep declarations & measurements & control flow as-is.
                if (is_reg_decl(s) || is_measure_stmt(s) || is_reset_or_barrier(s) ||
                    is_multiq_primitive(s) || is_oneq_simple(s) || is_custom_gate_call(s)) {
                    normalized.push_back(ensure_semi(s));
                    continue;
                }

                // If it's a 1q param op (rx/ry/rz/phase), keep for later u-family lifting.
                OneQParam op;
                if (parse_oneq_param(s, op)) {
                    normalized.push_back(ensure_semi(s));
                    continue;
                }

                // Fallback: keep as-is
                normalized.push_back(ensure_semi(s));
            }

            // Pass 2: Attempt to lift rz/ry/rz triples, etc. into u-family gates.
            result = lift_to_u_family(normalized, verbose);

            return result;
        }

        // ========================= PUBLIC API =========================
        // If your decompiler.hpp declares a different signature, adjust just this glue.

        vector<string> Decompiler::run(const vector<string>& expanded_canonical_lines, bool verbose) {
            return decompile_lines(expanded_canonical_lines, verbose);
        }

    } // namespace frontend
} // namespace qbin_compiler
