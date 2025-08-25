#include "qbin_decompiler/decompiler.hpp"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

static bool read_file(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    std::streamoff n = f.tellg();
    if (n < 0) return false;
    out.resize(static_cast<size_t>(n));
    f.seekg(0, std::ios::beg);
    f.read(reinterpret_cast<char*>(out.data()), n);
    return static_cast<size_t>(f.gcount()) == static_cast<size_t>(n);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " input.qbin [-o output.qasm] [--verbose]\n";
        return 1;
    }
    std::string in_path, out_path;
    bool verbose = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-o" && i + 1 < argc) out_path = argv[++i];
        else if (a == "--verbose" || a == "-v") verbose = true;
        else if (!a.empty() && a[0] != '-') in_path = a;
        else { std::cerr << "Unknown option: " << a << "\n"; return 1; }
    }
    if (in_path.empty()) { std::cerr << "No input file provided.\n"; return 1; }

    std::vector<uint8_t> buf;
    if (!read_file(in_path, buf)) {
        std::cerr << "Failed to read: " << in_path << "\n";
        return 1;
    }

    std::string qasm, err;
    if (!qbin_decompiler::decode_qbin_to_qasm(buf, qasm, err, verbose)) {
        std::cerr << "INST decode error: " << err << "\n";
        return 1;
    }

    // Ensure a trailing newline for byte-for-byte round-trip
    std::string out = qasm;
    if (!out.empty() && out.back() != '\n') out.push_back('\n');

    if (out_path.empty()) {
        std::cout << out;
    }
    else {
        std::ofstream ofs(out_path, std::ios::binary);
        if (!ofs) { std::cerr << "Cannot open output: " << out_path << "\n"; return 1; }
        ofs.write(out.data(), static_cast<std::streamsize>(out.size()));
        if (!ofs) { std::cerr << "Write failed\n"; return 1; }
    }
    return 0;
}
