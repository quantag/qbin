# CLI usage

This project provides two command line tools:

- **qbin-compile**: convert OpenQASM source into QBIN format
- **qbin-decompile**: convert QBIN back into OpenQASM

---

## Compile OpenQASM to QBIN

    build/compiler/qbin-compile path/to/input.qasm -o out.qbin

- `input.qasm`: OpenQASM source file
- `-o out.qbin`: output file in QBIN format

---

## Decompile QBIN to OpenQASM

    build/decompiler/qbin-decompile out.qbin -o roundtrip.qasm

- `out.qbin`: QBIN file produced earlier
- `-o roundtrip.qasm`: reconstructed OpenQASM source

The decompiler preserves canonical formatting for the supported subset and always ends the file with a blank line. This guarantees exact round-trip comparisons in the test suite.

---

## Notes

- Both tools are generated after building with CMake or running `scripts/bootstrap.sh`.
- Executables live in `build/compiler/` and `build/decompiler/`.
