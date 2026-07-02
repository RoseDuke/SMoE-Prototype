#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${repo_root}"

trace_path="${1:-tests/traces/tiny_skewed.csv}"
out_dir="${2:-results/optimization_sweep}"

cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure

mkdir -p "${out_dir}"

configs=(
  synchronous_baseline
  async_only
  async_aggregation
  async_expert_counter
  full_smartnic
)

for name in "${configs[@]}"; do
  ./build/smartnic_ref \
    --trace "${trace_path}" \
    --config "configs/${name}.cfg" \
    --output "${out_dir}/${name}.csv" \
    --summary "${out_dir}/${name}_summary.txt"
done

echo "Optimization sweep complete:"
for name in "${configs[@]}"; do
  echo "  ${out_dir}/${name}_summary.txt"
done
