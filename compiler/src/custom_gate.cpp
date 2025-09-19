#include "qbin_compiler/custom_gate.hpp"
#include <unordered_map>

namespace qbin_compiler {

// ---------------- CustomGate ----------------

CustomGate::CustomGate(std::string name,
                       std::vector<std::string> params,
                       std::vector<std::string> body)
    : name_(std::move(name)),
      params_(std::move(params)),
      body_(std::move(body)) {}

std::vector<std::string> CustomGate::expand(const std::vector<std::string>& args) const {
    std::vector<std::string> out;

    if (args.size() != params_.size()) {
        // argument count mismatch return empty for now
        return out;
    }

    // Build substitution map param -> arg
    std::unordered_map<std::string, std::string> subs;
    for (size_t i = 0; i < params_.size(); ++i) {
        subs[params_[i]] = args[i];
    }

    // Apply substitutions to each body line
    for (auto& line : body_) {
        std::string replaced = line;
        for (auto& kv : subs) {
            size_t pos = 0;
            while ((pos = replaced.find(kv.first, pos)) != std::string::npos) {
                replaced.replace(pos, kv.first.size(), kv.second);
                pos += kv.second.size();
            }
        }
        out.push_back(replaced);
    }

    return out;
}

// ---------------- GateRegistry ----------------

void GateRegistry::addGate(const CustomGate& gate) {
    gates_[gate.getName()] = gate;
}

bool GateRegistry::hasGate(const std::string& name) const {
    return gates_.find(name) != gates_.end();
}

const CustomGate& GateRegistry::getGate(const std::string& name) const {
    return gates_.at(name);
}

} // namespace qbin_compiler
