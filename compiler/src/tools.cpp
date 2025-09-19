#include "qbin_compiler/tools.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace qbin_compiler {
    namespace util {

        std::string to_lower_ascii(std::string s) {
            for (auto& ch : s) ch = char(std::tolower(static_cast<unsigned char>(ch)));
            return s;
        }

        std::string trim(std::string_view sv) {
            std::size_t i = 0, j = sv.size();
            while (i < j && std::isspace(static_cast<unsigned char>(sv[i]))) ++i;
            while (j > i && std::isspace(static_cast<unsigned char>(sv[j - 1]))) --j;
            return std::string(sv.substr(i, j - i));
        }

        std::vector<std::string> split_commas(const std::string& s, bool respect_parens) {
            std::vector<std::string> out;
            std::size_t last = 0;
            int depth = 0;
            for (std::size_t i = 0; i <= s.size(); ++i) {
                bool at_end = (i == s.size());
                char ch = at_end ? '\0' : s[i];
                if (respect_parens) {
                    if (!at_end && ch == '(') { ++depth; continue; }
                    if (!at_end && ch == ')') { --depth; continue; }
                }
                if (at_end || (ch == ',' && depth == 0)) {
                    std::string t = trim(std::string_view(s).substr(last, i - last));
                    if (!t.empty()) out.push_back(t);
                    last = i + 1;
                }
            }
            return out;
        }

        std::size_t find_matching_paren(const std::string& s, std::size_t open_pos) {
            if (open_pos >= s.size() || s[open_pos] != '(') return std::string::npos;
            int depth = 1;
            for (std::size_t i = open_pos + 1; i < s.size(); ++i) {
                if (s[i] == '(') ++depth;
                else if (s[i] == ')') {
                    --depth;
                    if (depth == 0) return i;
                }
            }
            return std::string::npos;
        }

        // ----- simple expression parser for angles -----
        struct ExprEval {
            std::string s;
            std::size_t i = 0;

            static double run(const std::string& expr) {
                ExprEval ev; ev.s = expr; ev.i = 0;
                return ev.parse_expr();
            }

            void skip() { while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i; }

            double parse_expr() {
                double v = parse_term();
                for (;;) {
                    skip();
                    if (i >= s.size()) return v;
                    char c = s[i];
                    if (c == '+' || c == '-') {
                        ++i;
                        double t = parse_term();
                        v = (c == '+') ? v + t : v - t;
                    }
                    else return v;
                }
            }

            double parse_term() {
                double v = parse_factor();
                for (;;) {
                    skip();
                    if (i >= s.size()) return v;
                    char c = s[i];
                    if (c == '*' || c == '/') {
                        ++i;
                        double t = parse_factor();
                        v = (c == '*') ? v * t : v / t;
                    }
                    else return v;
                }
            }

            double parse_factor() {
                skip();
                if (i >= s.size()) return 0.0;
                if (s[i] == '(') {
                    ++i;
                    double v = parse_expr();
                    skip();
                    if (i < s.size() && s[i] == ')') ++i;
                    return v;
                }
                if (s[i] == '+') { ++i; return parse_factor(); }
                if (s[i] == '-') { ++i; return -parse_factor(); }
                if (match_pi()) return M_PI;
                return parse_number();
            }

            bool match_pi() {
                skip();
                if (i + 1 < s.size()) {
                    char c1 = char(std::tolower(static_cast<unsigned char>(s[i])));
                    char c2 = char(std::tolower(static_cast<unsigned char>(s[i + 1])));
                    if (c1 == 'p' && c2 == 'i') { i += 2; return true; }
                }
                return false;
            }

            double parse_number() {
                skip();
                std::size_t j = i;
                while (i < s.size()) {
                    unsigned char ch = static_cast<unsigned char>(s[i]);
                    if (std::isdigit(ch) || s[i] == '.' || s[i] == 'e' || s[i] == 'E' || s[i] == '+' || s[i] == '-') {
                        ++i;
                    }
                    else break;
                }
                std::string t = s.substr(j, i - j);
                if (t.empty()) return 0.0;
                return std::strtod(t.c_str(), nullptr);
            }
        };

        double eval_expr(const std::string& expr) { return ExprEval::run(expr); }

        void vlog(bool enabled, const std::string& msg) {
            if (enabled) std::cerr << "[qbin] " << msg << "\n";
        }

    }
} // namespace
