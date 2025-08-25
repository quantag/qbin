#!/usr/bin/env bash
set -euo pipefail

# Usage: ./scripts/bootstrap.sh [Debug|Release] (default: Release)
type="${1:-Release}"

echo "==> Configuring ($type)"
if [[ "$type" == "Debug" ]]; then
  cmake --preset dev-debug
else
  cmake --preset dev
fi

echo "==> Building"
if [[ "$type" == "Debug" ]]; then
  cmake --build --preset dev-debug --parallel
else
  cmake --build --preset dev --parallel
fi

echo "==> Testing"
if [[ "$type" == "Debug" ]]; then
  ctest --preset dev-debug --output-on-failure
else
  ctest --preset dev --output-on-failure
fi

echo "All good ✅"
