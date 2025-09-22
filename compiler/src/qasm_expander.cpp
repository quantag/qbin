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

                // custom gate calls
                {
                    static std::regex rcall(R"(^\s*([A-Za-z_]\w*)\s+(.+?)\s*;?$)");
                    std::smatch m;
                    if (std::regex_match(s, m, rcall)) {
                        std::string name = to_lower_ascii(trim(m[1].str()));
                        std::string rest = trim(m[2].str());
                        if (gates.hasGate(name)) {
                            auto args = util::split_commas(rest, true);
                            auto expanded = gates.getGate(name).expand(args);
                            for (auto& e : expanded) {
                                self(self, e, out);
                            }
                            return;
                        }
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
