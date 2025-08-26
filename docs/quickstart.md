# Quick start (Linux/macOS)

Requirements:
- CMake >= 3.16
- C++17 compiler
- Python 3

---

## One command (build + test)

    ./scripts/bootstrap.sh        # Release build
    ./scripts/bootstrap.sh Debug  # Debug build

---

## Manual CMake (alternative)

    cmake --preset dev
    cmake --build --preset dev
    ctest --preset dev --output-on-failure

---

## Artifacts after build

- build/compiler/qbin-compile
- build/decompiler/qbin-decompile

