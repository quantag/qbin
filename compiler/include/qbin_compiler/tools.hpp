#pragma once
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace qbin_compiler { namespace util {

// String helpers
std::string to_lower_ascii(std::string s);
std::string trim(std::string_view sv);

// Split a comma-separated list, respecting parentheses depth if requested.
std::vector<std::string> split_commas(const std::string& s, bool respect_parens = true);

// Find the index of the matching ')' given an index of '(' in s.
// Returns std::string::npos if not found.
std::size_t find_matching_paren(const std::string& s, std::size_t open_pos);

// Evaluate a numeric expression with: numbers, pi, + - * /, parentheses.
// Uses double precision; caller can cast to float if needed.
double eval_expr(const std::string& expr);

// Very small logger: prints to stderr if enabled == true.
void vlog(bool enabled, const std::string& msg);

}} // namespace
