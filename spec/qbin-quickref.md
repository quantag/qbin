
# QBIN Quick Reference (v1.0-draft)

This is a cheat sheet for developers working with QBIN.

---

## File Layout

```
Header (24 bytes)
Section Table (entries)
Sections... (payloads, 8-byte aligned)
```

---

## Header (v1)

| Offset | Size | Field |
|--------|------|-------|
| 0x00   | 4    | Magic = "QBIN" |
| 0x04   | 1    | Major version = 1 |
| 0x05   | 1    | Minor version = 0 |
| 0x06   | 1    | Flags (bit0=endianness, bit1=table hash) |
| 0x07   | 1    | Header size = 24 |
| 0x08   | 4    | Section count |
| 0x0C   | 4    | Section table offset |
| 0x10   | 4    | Section table size |
| 0x14   | 4    | Header CRC32 |

---

## Section IDs

| ID    | ASCII | Purpose |
|-------|-------|---------|
| META  | 0x4D455441 | Metadata |
| STRS  | 0x53545253 | String table |
| QUBS  | 0x51554253 | Qubit table |
| BITS  | 0x42495453 | Classical bits |
| PARS  | 0x50415253 | Parameters |
| GATE  | 0x47415445 | Gate table |
| INST  | 0x494E5354 | Instruction stream (**required**) |
| DEBG  | 0x44454247 | Debug info |
| SIGN  | 0x5349474E | Signature |
| CPRS  | 0x43505253 | Compression dict |
| EXTS  | 0x45585453 | Extensions |

---

## Operand Mask Bits

```
bit0: qubit_a (varint)
bit1: qubit_b (varint)
bit2: qubit_c (varint)
bit3: angle_0 (tagged f32/param)
bit4: angle_1
bit5: angle_2
bit6: param_ref (param_id or CALLG gate_id)
bit7: aux_u32 (duration, bit index, etc.)
```

Angle slot encoding:
```
u8 tag: 0 = f32, 1 = param_ref
if tag==0: f32
if tag==1: varint param_id
```

---

## Opcodes (v1 core)

### Single-qubit
```
0x01 X, 0x02 Y, 0x03 Z, 0x04 H
0x05 S, 0x06 SDG, 0x07 T, 0x08 TDG
0x09 SX, 0x0A SXDG
0x0B RX a, angle_0
0x0C RY a, angle_0
0x0D RZ a, angle_0
0x0E PHASE a, angle_0
0x0F U a, angle_0, angle_1, angle_2
```

### Two-qubit and multi
```
0x10 CX a,b
0x11 CZ a,b
0x12 ECR a,b
0x13 SWAP a,b
0x14 CSX a,b
0x15 CRX a,b, angle_0
0x16 CRY a,b, angle_0
0x17 CRZ a,b, angle_0
0x18 CU a,b, angle_0, angle_1, angle_2

0x20 RXX a,b, angle_0
0x21 RYY a,b, angle_0
0x22 RZZ a,b, angle_0
```

### IO and barriers
```
0x30 MEASURE a, aux(bit_index)
0x31 RESET a
0x32 BARRIER
```

### Timing and frame
```
0x38 DELAY a, aux(duration_ns)
0x39 FRAME a, angle_0
```

### Custom gates
```
0x40 CALLG gate_id, qubits...
```

### Classical conditions
```
0x81 IF_EQ  aux(bit_index), value
0x82 IF_NEQ aux(bit_index), value
0x8F ENDIF
```

---

## Reserved Ranges

- 0x80..0xBF: control flow (future)
- 0xC0..0xFF: vendor extensions

---

## Error Codes (summary)

```
0x01 ERR_MAGIC_OR_VERSION
0x02 ERR_HEADER_CRC
0x03 ERR_SECTION_TABLE_RANGE
0x04 ERR_MISSING_INST
0x05 ERR_MULTIPLE_INST
0x09 ERR_UNSUPPORTED_OPCODE
...
```

---

## Typical Sizes

- QASM text: 100–1000× larger than binary for long state init circuits.  
- QBIN: compact, round-trip safe, stream-decodable.

---

