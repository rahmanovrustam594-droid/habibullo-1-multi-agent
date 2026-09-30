#!/usr/bin/env bash
set -euo pipefail

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

echo

echo "Build finished. Binary: ./build/habibullo"
