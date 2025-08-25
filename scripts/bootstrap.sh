#!/usr/bin/env bash
set -euo pipefail

# Usage: ./scripts/bootstrap.sh [Debug|Release] (default: Release)
type="${1:-Release}"
preset="dev"
bdir="build"
if [[ "$type" == "Debug" ]]; then
  preset="dev-debug"
  bdir="build-debug"
fi

echo "==> Configuring ($type)"
cmake --preset "$preset"

echo "==> Building"
cmake --build --preset "$preset" --parallel

echo "==> Testing"

# Prefer the CTest that CMake used (from the cache), fall back to PATH or /usr/bin/ctest.
CTEST_BIN=""
cache="${bdir}/CMakeCache.txt"
if [[ -f "$cache" ]]; then
  CTEST_BIN=$(awk -F= '/^CMAKE_CTEST_COMMAND:FILEPATH=/ {print $2}' "$cache" || true)
fi

# Fall back to PATH
if [[ -z "${CTEST_BIN:-}" ]]; then
  if command -v ctest >/dev/null 2>&1; then
    CTEST_BIN="$(command -v ctest)"
  fi
fi

# If PATH ctest exists but is broken (conda/pip installs can be mismatched), try /usr/bin/ctest
if [[ -z "${CTEST_BIN:-}" ]] || ! "$CTEST_BIN" --version >/dev/null 2>&1; then
  if [[ -x /usr/bin/ctest ]]; then
    CTEST_BIN="/usr/bin/ctest"
  fi
fi

# If still missing or not runnable, try to install via apt (Debian/Ubuntu).
if [[ -z "${CTEST_BIN:-}" ]] || ! "$CTEST_BIN" --version >/dev/null 2>&1; then
  if [[ -f /etc/debian_version ]]; then
    echo "==> Installing cmake (ctest) via apt... (sudo required)"
    sudo apt-get update
    sudo apt-get install -y cmake
    if command -v ctest >/dev/null 2>&1; then
      CTEST_BIN="$(command -v ctest)"
    elif [[ -x /usr/bin/ctest ]]; then
      CTEST_BIN="/usr/bin/ctest"
    fi
  fi
fi

# Final fallback: use the build system's 'test' target if we still couldn't find a working ctest
if [[ -z "${CTEST_BIN:-}" ]] || ! "$CTEST_BIN" --version >/dev/null 2>&1; then
  echo "!! ctest not available/runnable; using 'cmake --build --target test' as fallback"
  cmake --build --preset "$preset" --target test || {
    echo "Tests failed (no ctest)."; exit 1;
  }
else
  # Use --test-dir instead of --preset for compatibility with older CTest versions
  "$CTEST_BIN" --test-dir "$bdir" --output-on-failure
fi

echo "All good ✅"
