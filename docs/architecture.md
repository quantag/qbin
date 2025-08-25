# QBIN Architecture

Status: DRAFT  
Audience: developers of the spec, compiler, decompiler, libraries, and tools

This document explains the architecture of the QBIN project and how the
components fit together. It complements the normative format definition
in `spec/qbin-spec.md` and the developer cheat sheet in `spec/qbin-quickref.md`.

---

## 1) Goals and Non-Goals

Goals:
- Compact, stream-decodable representation of OpenQASM programs.
- Stable containers with clear versioning and forward compatibility.
- Clean separation between specification, libraries, and tools.
- Easy round-trip: QASM -> QBIN -> QASM with predictable fidelity.
- Vendor extension points that do not break core compatibility.

Non-goals (v1):
- Full general-purpose bytecode semantics (loops, arbitrary classical compute).
- VM execution environment. QBIN is a container + instruction stream, not a VM.

---

## 2) Project Layout (high-level)

```
qbin/
  spec/                -> Format specification and quick reference
  lib/                 -> Reference libraries (C++, Python, Rust)
  compiler/            -> QASM -> QBIN CLI and front-ends
  decompiler/          -> QBIN -> QASM CLI
  tools/               -> validators, inspectors, scripts
  examples/            -> QASM and QBIN examples and round-trip
  tests/               -> unit, conformance, fuzz
  docs/                -> architecture, roadmap, design decisions
```

---

## 3) Format Layers (data model)

QBIN is a layered container:

```
+----------------------+
|  Header (fixed)      |
+----------------------+
|  Section Table       |
+----------------------+
|  Sections...         |
|  - INST (required)   |
|  - STRS, META, ...   |
|  - EXTS (optional)   |
+----------------------+
```

- Header: magic, version, counts, offsets, CRC.
- Section Table: descriptors (id, offset, size, flags).
- Sections: independent typed payloads, 8-byte aligned.
- INST: instruction stream (opcodes + operand mask + operands).
- Optional sections add metadata, strings, gates, debug, signatures.

---

## 4) Component Overview

### 4.1 libqbin (reference library)
- Parse/validate: header, table, section ranges, checksums.
- Section readers/writers: STRS, META, QUBS, BITS, PARS, GATE, INST, DEBG, SIGN.
- Error taxonomy aligned with spec (ERR_*).
- Streaming decode of INST (zero-copy where possible).
- ABI-stable C API (thin) over C++ classes for easier bindings.
- Python and Rust bindings reuse the C API surface.

### 4.2 Compiler (qbin-compile)
Pipeline:
```
QASM text --> Front-end (lexer+parser) --> IR (normalized ops)
         --> Lowering to QBIN opcodes --> Section assembly
         --> Header+Table+INST (+STRS/META/GATE/DEBG as requested)
```
Key choices:
- Normalize gate set to v1 core opcodes; custom gates go to GATE+CALLG.
- Parameters become PARS entries; angles either literal or param_ref.
- Qubit and bit indices normalized to zero-based ints.
- Optional: emit STRS/META for better decompilation fidelity.

### 4.3 Decompiler (qbin-decompile)
Pipeline:
```
QBIN --> Reader --> Validate --> Recreate textual QASM
      --> Reconstruct declarations, names (from STRS/QUBS/BITS)
      --> Emit QASM 2.x or 3.x dialects as requested
```
Notes:
- If names are missing, synthesize like q[0], c[1].
- CALLG resolved against GATE; opaque gates emitted as calls.

### 4.4 Tools
- qbin-validate: syntax + structural validation, checksums, alignment.
- qbin-inspect: header/table dump, section hexdumps, INST decode.
- Fuzz harness: libFuzzer/AFL entry points for `reader` functions.
- Corpus management scripts, conformance runner.

---

## 5) Data Flow and APIs

### 5.1 Read Path (pseudocode)
```
read_all(fd):
  hdr = read_header(fd)
  check_crc(hdr)
  table = read_section_table(fd, hdr.section_count, hdr.section_table_*)
  for entry in table:
    ensure_aligned(entry.offset)
    ensure_inbounds(entry)
  sections = { id: map_entry(fd, entry) }
  inst = parse_INST(sections["INST"])
  return QBIN(hdr, table, sections, inst)
```

### 5.2 Write Path (pseudocode)
```
encode(qbin):
  layout = plan_layout(qbin.sections)           # compute sizes/offsets
  hdr = build_header(layout)
  hdr.crc = crc32c(hdr[0:0x14])                 # CRC over 0x00..0x13
  buf = [hdr, build_table(layout)]
  for sec in layout.sections_in_order:
    payload = encode_section(sec)
    if sec.flags.compressed:
      payload = compress(payload, alg)
    if sec.flags.checksummed:
      payload += trailer_crc(payload)
    buf.append(align8(payload))
  return concat(buf)
```

### 5.3 Public API Surfaces

C++:
```
class QbinReader {
 public:
  static QbinFile read(std::span<const uint8_t> bytes);
};

class QbinWriter {
 public:
  static std::vector<uint8_t> write(const QbinFile& f);
};
```

C ABI (for bindings):
```
int qbin_read(const uint8_t* data, size_t len, qbin_file_t** out);
int qbin_write(const qbin_file_t* in, uint8_t** out, size_t* out_len);
void qbin_free(void* p);
```

Python:
```
import qbin
f = qbin.read(b)
b = qbin.write(f)
```

---

## 6) Instruction Encoding (summary)

Instruction = `opcode (u8)` + `operand_mask (u8)` + operands.  
Operand mask bits:
```
0: a (varint)   1: b (varint)   2: c (varint)
3: angle_0      4: angle_1      5: angle_2
6: param_ref    7: aux_u32
```
Angle slot: `u8 tag (0=f32, 1=param_ref) + payload`.

Rationale:
- Constant-size opcode header; variable-size operands for compactness.
- Varint for indices; f32 for angles; param_ref enables symbolic reuse.

---

## 7) Names, Metadata, and Fidelity

- STRS: deduplication of names/keys; referenced via IDs.
- META: binary KV, avoids JSON parser; used for provenance and target info.
- QUBS/BITS: preserve register naming and sizes for round-trip readability.
- PARS: stable parameter IDs and kinds; literal vs ref recorded at use-sites.
- DEBG: optional file/line/column mapping for decompilers and IDEs.

Round-trip policies are documented in `spec/qbin-spec.md` section 13.

---

## 8) Security and Robustness

- All inputs untrusted: validate ranges, counts, overlaps, alignment.
- Compression bombs: cap `raw_size` and compression ratios.
- Checksums and signatures: optional integrity and authenticity.
- EXTS and GATE bodies: never execute; only parse/validate unless explicitly enabled.
- Bounded recursion for IF/ENDIF nesting when decoding.

---

## 9) Performance Considerations

- Streaming decode of INST to avoid large intermediate ASTs.
- Use contiguous buffers and span/slice views (no copies) in readers.
- Prefer varint for indices and sparse IDs; normalize index order.
- Optional zstd compression for large STRS/DEBG sections.
- Align sections to 8 bytes to help mmap and DMA-friendly IO.

Target outcomes (indicative):
- 5x–20x smaller than QASM for long gate streams.
- O(1) per-instruction decode; linear scan with no tokenization.

---

## 10) Testing Strategy

- Unit tests: per-section encode/decode.
- Round-trip tests: QASM -> QBIN -> QASM equivalence (semantic).
- Conformance suite: canonical `.qbin` fixtures with hashes.
- Fuzzing: structure-aware fuzzers for `reader` entry points.
- Cross-language parity: C++ vs Python decoders read same fixtures.

CI:
- Build on Linux/macOS/Windows.
- Run ctest + pytest + fuzz smoke.
- Artifacts: reference `.qbin` fixtures and coverage reports.

---

## 11) Extension Mechanism

EXTS section registers:
- New opcodes and operand schemas.
- Expression DAGs used by PARS (symbolic math).
- Vector operands (qubit lists), multi-control gates.

Rules:
- Unknown sections must be skippable.
- Unknown opcodes: report `ERR_UNSUPPORTED_OPCODE` unless EXTS map provided.
- Minor versions cannot break existing encodings.

---

## 12) Integration Examples

- CLI workflow:
```
qbin-compile program.qasm -o program.qbin --meta target=ibm_brisbane
qbin-inspect program.qbin --inst
qbin-decompile program.qbin -o program_roundtrip.qasm --qasm 3
```

- IDE integration (e.g., VS Code extension):
  - Use Python binding to decode `.qbin` for quick previews.
  - Map DEBG to source lines for inline annotations.
  - Send `.qbin` over IPC instead of large QASM text for speed.

---

## 13) Roadmap (excerpt)

- v1.0-draft freeze -> conformance fixtures -> reference reader/writer (C++)
- qbin-compile/decompile MVP -> Python binding -> Rust binding
- qbin-inspect GUI (optional) -> integration with IDEs
- v1.1: EXTS registry, vector operands, richer conditionals

Full roadmap in `docs/roadmap.md` (WIP).

---

## 14) Glossary

- INST: Instruction stream section.
- STRS: String table.
- META: Metadata section.
- QUBS/BITS: Qubit/bit tables.
- PARS: Parameter table.
- GATE: Custom gate table.
- DEBG: Debug mapping.
- SIGN: Detached signature.
- EXTS: Extension registry.
