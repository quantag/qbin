# qbin - a compact binary format for OpenQASM 3

`qbin` is a minimal, lossless binary encoding for a useful subset of OpenQASM 3.
It focuses on code you actually run on hardware (single-/two-qubit gates, rotations,
measurement, and simple conditionals), so state-init circuits and large templated programs shrink dramatically versus plain text QASM.

## Why qbin?
- Size matters: init/state-prep circuits and large parametrized templates shrink a lot.
- Streaming-friendly: one-pass, sectioned layout.
- Round-trip exactness: supported subset decompiles to identical QASM.

See [Quick start](quickstart.md) and [CLI usage](cli.md) to begin.

Check [architecture](architecture.md) for more info about format.