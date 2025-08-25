// main.cpp - CLI driver that uses qbin_compiler::compile_qasm_to_qbin_min

#include "qbin_compiler/compiler.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

static void print_usage(const char* argv0) {
    std::cerr
        << "Usage:\n"
        << "  " << argv0 << " <input.qasm> -o <output.qbin> [--verbose]\n"
        << "\n"
        << "Description:\n"
        << "  Minimal compiler from a small subset of OpenQASM to QBIN.\n"
        << "  Unsupported statements are skipped with a warning (if --verbose).\n";
}

int main(int argc, char** argv) {
    if (argc < 2) { print_usage(argv[0]); return 1; }

    std::string in_path;
    std::string out_path;
    bool verbose = false;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-o" && i + 1 < argc) {
            out_path = argv[++i];
        }
        else if (a == "--verbose" || a == "-v") {
            verbose = true;
        }
        else if (!a.empty() && a[0] == '-') {
            std::cerr << "Unknown option: " << a << "\n";
            print_usage(argv[0]);
            return 1;
        }
        else if (in_path.empty()) {
            in_path = a;
        }
        else {
            std::cerr << "Unexpected argument: " << a << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    if (in_path.empty() || out_path.empty()) {
        print_usage(argv[0]);
        return 1;
    }

    // Read input QASM
    std::ifstream ifs(in_path, std::ios::binary);
    if (!ifs) {
        std::cerr << "Error: cannot open input file: " << in_path << "\n";
        return 1;
    }
    std::stringstream buffer;
    buffer << ifs.rdbuf();
    std::string qasm = buffer.str();
    if (qasm.empty()) {
        std::cerr << "Error: input file is empty.\n";
        return 1;
    }

    // Compile
    std::vector<uint8_t> blob = qbin_compiler::compile_qasm_to_qbin_min(qasm, verbose);

    // Write output
    std::ofstream ofs(out_path, std::ios::binary);
    if (!ofs) {
        std::cerr << "Error: cannot open output file: " << out_path << "\n";
        return 1;
    }
    ofs.write(reinterpret_cast<const char*>(blob.data()), static_cast<std::streamsize>(blob.size()));
    if (!ofs) {
        std::cerr << "Error: failed to write output file.\n";
        return 1;
    }
    if (verbose) {
        std::cerr << "Wrote " << blob.size() << " bytes to " << out_path << "\n";
    }
    return 0;
}
