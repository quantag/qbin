# qbin — a compact binary format for OpenQASM 3

`qbin` is a minimal, lossless binary encoding for a **useful subset** of OpenQASM 3.
It focuses on code you actually run on hardware (single‑/two‑qubit gates, rotations,
measurement, and simple conditionals), so **state‑init circuits and large templated programs
shrink dramatically** versus plain text QASM.

This repository contains:
- **spec/** — the QBIN file format and quick reference
- **compiler/** — `qbin-compile` (OpenQASM → QBIN)
- **decompiler/** — `qbin-decompile` (QBIN → OpenQASM)
- **tests/** — round‑trip tests (QASM → QBIN → QASM) wired into CTest

---

## Why qbin?
- **Size matters:** Initialization / state‑prep circuits and large parametrized templates can be huge in text.
  `qbin` removes whitespace/identifiers and uses compact encodings (ULEB128, f32 angles), so artifacts are **much smaller**.
- **Streaming‑friendly:** One pass, sectioned layout (`INST`, more later).
- **Round‑trip exactness:** For the supported OpenQASM subset, decompilation reproduces the **same** source (incl. EOF newline).

See the full format in **[`spec/qbin-spec.md`](spec/qbin-spec.md)** and the quick sheet in **[`spec/qbin-quickref.md`](spec/qbin-quickref.md)**.

---

## Repository layout
```
qbin/
├─ spec/                          # spec and quickref
├─ compiler/                      # qbin-compile (QASM -> QBIN)
├─ decompiler/                    # qbin-decompile (QBIN -> QASM)
├─ tests/                         # CTest harness + data/*.qasm
├─ scripts/                       # helper scripts (bootstrap.sh)
├─ .github/workflows/ci.yml       # GitHub Actions CI
├─ README.md  LICENSE  CONTRIBUTING.md  CODE_OF_CONDUCT.md  SECURITY.md
```

---

## Quick start (Linux/macOS)
**Requirements:** CMake ≥ 3.16, a C++17 compiler, and Python 3.

### One command (build + test)
```bash
# If git reports "password auth not supported", see Troubleshooting below.
./scripts/bootstrap.sh        # Release build
# or
./scripts/bootstrap.sh Debug  # Debug build
```

### Manual CMake (alternative)
```bash
cmake --preset dev            # or: cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build --preset dev    # or: cmake --build build --parallel
ctest --preset dev --output-on-failure
```

Artifacts (after a preset build):
```
build/compiler/qbin-compile
build/decompiler/qbin-decompile
```

---

## CLI usage
### Compile OpenQASM → QBIN
```bash
build/compiler/qbin-compile path/to/input.qasm -o out.qbin
```

### Decompile QBIN → OpenQASM
```bash
build/decompiler/qbin-decompile out.qbin -o roundtrip.qasm
```

The decompiler preserves canonical formatting for the supported subset and
ends with a **blank line** to match our tests’ byte‑for‑byte comparison.

---

## Round‑trip tests
Tests live in `tests/` and are run by CTest.

- Add your `.qasm` vectors to `tests/data/`.
- Each file becomes a test: QASM → QBIN → QASM, then exact compare.

Run tests manually:
```bash
ctest --test-dir build --output-on-failure
```

---

## CI
A GitHub Actions workflow builds the compiler & decompiler and runs the
round‑trip tests on Ubuntu for pushes and PRs. See **[.github/workflows/ci.yml](.github/workflows/ci.yml)**.

---

## Troubleshooting

### “password authentication is not supported on git operations”
GitHub disabled password over HTTPS. Use **SSH** or a **Personal Access Token** (HTTPS):

**SSH (recommended):**
```bash
ssh-keygen -t ed25519 -C "you@example.com"
eval "$(ssh-agent -s)"
ssh-add ~/.ssh/id_ed25519
cat ~/.ssh/id_ed25519.pub  # add to GitHub → Settings → SSH keys
git remote set-url origin git@github.com:OWNER/REPO.git
```

**HTTPS + PAT:**
Create a token (Developer settings → Personal access tokens), then when prompted:
- Username: your GitHub username
- Password: the **token**

### `bootstrap.sh` not executable after clone
Either run with bash:
```bash
bash scripts/bootstrap.sh
```
or fix the bit once (and commit it so it stays executable):
```bash
chmod +x scripts/bootstrap.sh
git update-index --chmod=+x scripts/bootstrap.sh
git commit -m "Make bootstrap executable"; git push
```

### Decompiler reads fail with “bad b” / “bad c”
Ensure you rebuilt the **latest** decompiler: it must read operands in this order:
`a` → `b` → `c` → `angle(tag+payload)` → `aux_u32` → `imm8 (IF only)`.
Do a clean build of `decompiler/`.

---

## Supported OpenQASM subset (MVP)
- Single‑qubit: `x,y,z,h,s,sdg,t,tdg,sx,sxdg,rx(θ),ry(θ),rz(θ),phase(θ)`
- Two‑qubit: `cx,cz,swap` (+ some controlled/XX/YY/ZZ rotations reserved)
- Classical I/O: `c[i] = measure q[j];`
- Control flow: single‑line `if (c[k] ==/!= v) { <stmt>; }`
- Declarations (`qubit[N] q;`, `bit[M] c;`) are inferred on decompile.

See **spec** for opcodes, masks, and extensibility.

---

## License
Copyright (c) Quantag IT Solutions GmbH  
Licensed under the terms in **[LICENSE](LICENSE)**.

## Contributing & Security
- Please read **[CONTRIBUTING.md](CONTRIBUTING.md)** before sending PRs.
- Report vulnerabilities via **[SECURITY.md](SECURITY.md)**.

---

## Acknowledgements
Thanks to the OpenQASM community and the broader quantum‑stack ecosystem for prior art and inspiration.
