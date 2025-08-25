# Contributing to QBIN

Thank you for your interest in contributing to QBIN!
We welcome contributions from the community to improve the specification,
compiler, decompiler, libraries, and tools.

---

## How to Contribute

### 1. Reporting Issues
- Use the [GitHub Issues](../../issues) page.
- Provide clear reproduction steps, expected behavior, and actual behavior.
- Attach example QASM or QBIN files if possible.

### 2. Proposing Enhancements
- Before large changes, open an issue to discuss your proposal.
- Describe the motivation (why it is needed) and potential design options.

### 3. Code Contributions
- Fork the repository and create a feature branch (`git checkout -b feature/my-change`).
- Follow existing code style (C++: clang-format, Python: PEP8).
- Add tests for new functionality (unit, round-trip, conformance).
- Run the full test suite before submitting a pull request.

### 4. Specification Changes
- Spec files are under `spec/`.
- Propose spec changes via pull requests with a clear rationale.
- Minor clarifications are welcome; major format changes require discussion in issues.

### 5. Documentation
- Docs are under `docs/`.
- Improvements to readability, diagrams, and quick references are welcome.

---

## Development Setup

Typical workflow:
```bash
git clone https://github.com/<your-org>/qbin.git
cd qbin
mkdir build && cd build
cmake ..
make -j
ctest
```

For Python bindings:
```bash
cd lib/python
pip install -e .
pytest
```

---

## Pull Request Process

1. Ensure all tests pass (`ctest`, `pytest`).
2. Update `CHANGELOG.md` if applicable.
3. One reviewer approval is required for merge.
4. Large or breaking changes may require multiple reviewers.

---

## Code of Conduct

By participating, you agree to follow the [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md).

---

## License

By contributing, you agree that your contributions will be licensed under
the [MIT License](LICENSE).
