#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <regex>

namespace qbin_compiler {

/**
 * Represents a user-defined gate from OpenQASM.
 * Example:
 *   gate majority a,b,c {
 *       cx a,b;
 *       cx a,c;
 *       ccx b,c,a;
 *   }
 */
class CustomGate {
public:
    CustomGate() = default;
    CustomGate(std::string name,
               std::vector<std::string> params,
               std::vector<std::string> body);

    /// Gate name (e.g. "majority")
    const std::string& getName() const { return name_; }

    /// Parameter list (e.g. ["a","b","c"])
    const std::vector<std::string>& getParams() const { return params_; }

    /// Body (raw QASM statements, not yet expanded)
    const std::vector<std::string>& getBody() const { return body_; }

    /**
     * Expand this gate with concrete arguments.
     * Example:
     *   expand(["q[0]","q[1]","q[2]"])
     * returns vector of QASM lines with params substituted.
     */
    std::vector<std::string> expand(const std::vector<std::string>& args) const;

private:
    std::string name_;
    std::vector<std::string> params_;
    std::vector<std::string> body_;
};

/**
 * Registry for all custom gates.
 * Provides storage and lookup.
 */
class GateRegistry {
public:
    /// Register a new gate definition
    void addGate(const CustomGate& gate);

    /// Check if a gate is defined
    bool hasGate(const std::string& name) const;

    /// Get a gate definition (throws if not found)
    const CustomGate& getGate(const std::string& name) const;

private:
    std::unordered_map<std::string, CustomGate> gates_;
};

} // namespace qbin_compiler
