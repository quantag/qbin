#include "qbin_compiler/qasm_expander.hpp"
#include "qbin_compiler/tools.hpp"

#include <regex>

using qbin_compiler::util::to_lower_ascii;
using qbin_compiler::util::trim;
using qbin_compiler::util::vlog;

namespace qbin_compiler {
    namespace frontend {

        std::vector<std::string> StatementExpander::expand(
            const std::vector<std::string>& nondef_lines,
            const GateRegistry& gates,
            bool verbose)
        {
            std::vector<std::string> canonical;

            auto ensure_semi = [](const std::string& t) {
                if (!t.empty() && t.back() == ';') return t;
                std::string r = t;
                r.push_back(';');
                return r;
                };

            auto expand_recursive = [&](auto&& self,
                const std::string& stmt,
                std::vector<std::string>& out) -> void {
                    std::string s = trim(stmt);
                    if (s.empty()) return;
                    std::string sl = to_lower_ascii(s);

                    // skip barrier/reset
                    if (sl.rfind("barrier", 0) == 0) { vlog(verbose, "skip barrier"); return; }
                    if (sl.rfind("reset", 0) == 0) { vlog(verbose, "skip reset"); return; }

                    // measure
                    {
                        static std::regex rem1(R"(^\s*measure\s+(.+?)\s*->\s*(.+?)\s*;?$)", std::regex::icase);
                        static std::regex rem2(R"(^\s*(.+?)\s*=\s*measure\s+(.+?)\s*;?$)", std::regex::icase);
                        std::smatch m;
                        if (std::regex_match(s, m, rem1)) {
                            std::string q = trim(m[1].str());
                            std::string c = trim(m[2].str());
                            out.push_back(ensure_semi(c + " = measure " + q));
                            return;
                        }
                        if (std::regex_match(s, m, rem2)) {
                            out.push_back(ensure_semi(trim(m[1].str()) + " = measure " + trim(m[2].str())));
                            return;
                        }
                    }

                    // two-qubit gates
                    {
                        static std::regex r2(R"(^\s*(cx|cz|swap)\s+(.+?)\s*,\s*(.+?)\s*;?$)", std::regex::icase);
                        std::smatch m;
                        if (std::regex_match(s, m, r2)) {
                            std::string op = to_lower_ascii(m[1].str());
                            std::string a = trim(m[2].str());
                            std::string b = trim(m[3].str());
                            out.push_back(ensure_semi(op + " " + a + ", " + b));
                            return;
                        }
                    }

                    // one-qubit gates
                    {
                        static std::regex r1(R"(^\s*(h|x|y|z|s|sdg|t|tdg|sx|sxdg)\s+(.+?)\s*;?$)", std::regex::icase);
                        std::smatch m;
                        if (std::regex_match(s, m, r1)) {
                            std::string op = to_lower_ascii(m[1].str());
                            std::string a = trim(m[2].str());
                            out.push_back(ensure_semi(op + " " + a));
                            return;
                        }
                    }

                    // three-qubit gates
                    {
                        static std::regex r3(R"(^\s*(ccx)\s+(.+?)\s*,\s*(.+?)\s*,\s*(.+?)\s*;?$)", std::regex::icase);
                        std::smatch m;
                        if (std::regex_match(s, m, r3)) {
                            std::string a = trim(m[2].str());
                            std::string b = trim(m[3].str());
                            std::string c = trim(m[4].str());
                            out.push_back(ensure_semi("ccx " + a + ", " + b + ", " + c));
                            return;
                        }
                    }

                    // parametric gates
                    {
                        static std::regex rp(R"(^\s*(rx|ry|rz|phase)\s*\(\s*(.+?)\s*\)\s+(.+?)\s*;?$)", std::regex::icase);
                        std::smatch m;
                        if (std::regex_match(s, m, rp)) {
                            std::string op = to_lower_ascii(m[1].str());
                            std::string expr = trim(m[2].str());
                            std::string a = trim(m[3].str());
                            out.push_back(ensure_semi(op + "(" + expr + ") " + a));
                            return;
                        }
                    }

                    // u-family gates
                    {
                        static std::regex ru3(R"(^\s*(u|u3)\s*\(\s*([^,]+)\s*,\s*([^,]+)\s*,\s*([^)]+)\)\s+(.+?)\s*;?$)", std::regex::icase);
                        static std::regex ru2(R"(^\s*u2\s*\(\s*([^,]+)\s*,\s*([^)]+)\)\s+(.+?)\s*;?$)", std::regex::icase);
                        static std::regex ru1(R"(^\s*u1\s*\(\s*([^)]+)\)\s+(.+?)\s*;?$)", std::regex::icase);
                        std::smatch m;
                        if (std::regex_match(s, m, ru3)) {
                            std::string theta = trim(m[2].str());
                            std::string phi = trim(m[3].str());
                            std::string lambda = trim(m[4].str());
                            std::string q = trim(m[5].str());
                            out.push_back(ensure_semi("rz(" + phi + ") " + q));
                            out.push_back(ensure_semi("ry(" + theta + ") " + q));
                            out.push_back(ensure_semi("rz(" + lambda + ") " + q));
                            vlog(verbose, "expanded u3/u gate -> rz/ry/rz");
                            return;
                        }
                        if (std::regex_match(s, m, ru2)) {
                            std::string phi = trim(m[1].str());
                            std::string lambda = trim(m[2].str());
                            std::string q = trim(m[3].str());
                            out.push_back(ensure_semi("rz(" + phi + ") " + q));
                            out.push_back(ensure_semi("ry(pi/2) " + q));
                            out.push_back(ensure_semi("rz(" + lambda + ") " + q));
                            vlog(verbose, "expanded u2 -> rz/ry(pi/2)/rz");
                            return;
                        }
                        if (std::regex_match(s, m, ru1)) {
                            std::string lambda = trim(m[1].str());
                            std::string q = trim(m[2].str());
                            out.push_back(ensure_semi("rz(" + lambda + ") " + q));
                            vlog(verbose, "expanded u1 -> rz");
                            return;
                        }
                    }

                    // custom gate calls (with or without parameters)
                    {
                        static std::regex rcall_param(
                            R"(^\s*([A-Za-z_]\w*)\s*\(\s*([^)]+)?\s*\)\s+(.+?)\s*;?$)");
                        static std::regex rcall_simple(
                            R"(^\s*([A-Za-z_]\w*)\s+(.+?)\s*;?$)");
                        std::smatch m;

                        std::string name, paramlist, arglist;
                        std::vector<std::string> params, args;

                        if (std::regex_match(s, m, rcall_param)) {
                            name = to_lower_ascii(trim(m[1].str()));
                            paramlist = trim(m[2].str());
                            arglist = trim(m[3].str());
                            if (!paramlist.empty()) params = qbin_compiler::util::split_commas(paramlist, true);
                            args = qbin_compiler::util::split_commas(arglist, true);
                        }
                        else if (std::regex_match(s, m, rcall_simple)) {
                            name = to_lower_ascii(trim(m[1].str()));
                            arglist = trim(m[2].str());
                            args = qbin_compiler::util::split_commas(arglist, true);
                        }

                        if (!name.empty() && gates.hasGate(name)) {
                            // Flatten: params first, then qubit args — matches how formals were stored.
                            std::vector<std::string> call_args;
                            call_args.reserve(params.size() + args.size());
                            call_args.insert(call_args.end(), params.begin(), params.end());
                            call_args.insert(call_args.end(), args.begin(), args.end());

                            auto expanded = gates.getGate(name).expand(call_args);
                            for (auto& e : expanded) {
                                self(self, e, out);
                            }
                            vlog(verbose, "expanded custom gate: " + name);
                            return;
                        }
                    }

                    // fallback: keep as-is
                    out.push_back(ensure_semi(s));
                };

            // process all lines
            for (auto& s : nondef_lines) {
                std::vector<std::string> tmp;
                expand_recursive(expand_recursive, s, tmp);
                canonical.insert(canonical.end(), tmp.begin(), tmp.end());
            }

            return canonical;
        }

    } // namespace frontend
} // namespace qbin_compiler
