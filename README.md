# QBIN – A Binary Format for OpenQASM

**QBIN** is a compact, binary representation of [OpenQASM](https://openqasm.com/) circuits.  
It is designed to solve one of the biggest bottlenecks in quantum software workflows:  
**size and parsing overhead of large circuits**.

---

## Why QBIN?

When working with quantum programs, especially *state initialization circuits* or algorithms with long gate sequences (e.g. QFT, Grover, chemistry ansätze), the text-based OpenQASM representation can quickly become very large:

- A 10-qubit state initialization may require **thousands of gates**.
- As plain text, this can reach **hundreds of kilobytes or even megabytes**.
- Parsing OpenQASM is slow: the compiler must tokenize, parse, and build ASTs.

With QBIN:

- The same circuit is stored as a **compact binary stream of opcodes and indices**.
- Redundant text (keywords, variable names, whitespace) is eliminated.
- Typical size reduction: **5–20× smaller** than QASM text.
- Parsing is O(1) per instruction, directly reading bytes without a heavy parser.
- Easier to transmit over the network, cache, or embed in larger workflows.

In short:  
**QBIN makes large quantum circuits smaller, faster, and more portable.**

---

## Features

- **Compact:** optimized binary encoding, much smaller than text QASM.
- **Fast to parse:** direct binary reader, no text parsing overhead.
- **Round-trip safe:** QBIN → QASM → QBIN is guaranteed consistent.
- **Extensible:** supports metadata sections, string tables, and signatures.
- **Future-proof:** optional checksums and digital signatures.

---

## Repository Structure

- `spec/` – formal specification of QBIN format.
- `compiler/` – compiler: OpenQASM → QBIN.
- `decompiler/` – decompiler: QBIN → OpenQASM.
- `lib/` – reference implementations (C++, Python, Rust).
- `examples/` – sample QASM circuits and their QBIN equivalents.
- `tests/` – unit tests, conformance suites, fuzzing.
- `docs/` – design notes, roadmap, and API docs.

---

## Quick Start

### Build from Source
```bash
git clone https://github.com/quantag/qbin.git
cd qbin
mkdir build && cd build
cmake ..
make -j

