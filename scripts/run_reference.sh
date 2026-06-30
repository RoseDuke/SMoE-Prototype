#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${repo_root}"

cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure

mkdir -p results

./build/smartnic_ref \
    --trace tests/traces/tiny_uniform.csv \
    --config configs/baseline.cfg \
    --output results/uniform_baseline.csv \
    --summary results/uniform_baseline_summary.txt

./build/smartnic_ref \
    --trace tests/traces/tiny_skewed.csv \
    --config configs/optimized.cfg \
    --output results/skewed_optimized.csv \
    --summary results/skewed_optimized_summary.txt

echo "Generated:"
echo "  results/uniform_baseline.csv"
echo "  results/uniform_baseline_summary.txt"
echo "  results/skewed_optimized.csv"
echo "  results/skewed_optimized_summary.txt"
