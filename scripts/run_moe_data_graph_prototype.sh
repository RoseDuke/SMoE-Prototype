#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${repo_root}"

graph_dir="${1:-../smartnic_input_constructor/gpu_runs/run001_mixtral_decode_bs64_out16/data_graph}"
out_dir="${2:-results/moe_data_graph_run001}"
cycles_per_us="${CYCLES_PER_US:-1000}"

mkdir -p "${out_dir}"

convert_args=(
  --graph "${graph_dir}"
  --out "${out_dir}/smartnic_trace.csv"
  --cycles-per-us "${cycles_per_us}"
)

if [[ "${PRESERVE_DECODE_BATCHES:-0}" == "1" ]]; then
  convert_args+=(--preserve-decode-call-batches)
fi

if [[ "${EXPAND_TOKEN_COUNT:-0}" == "1" ]]; then
  convert_args+=(--expand-token-count)
fi

python3 scripts/convert_moe_flows_to_trace.py "${convert_args[@]}"

cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure

configs=(
  synchronous_baseline
  async_only
  async_aggregation
  async_expert_counter
  full_smartnic
)

for name in "${configs[@]}"; do
  ./build/smartnic_ref \
    --trace "${out_dir}/smartnic_trace.csv" \
    --config "configs/${name}.cfg" \
    --output "${out_dir}/${name}.csv" \
    --summary "${out_dir}/${name}_summary.txt"
done

echo "MoE data-graph SmartNIC prototype run complete:"
echo "  trace: ${out_dir}/smartnic_trace.csv"
for name in "${configs[@]}"; do
  echo "  ${name}: ${out_dir}/${name}_summary.txt"
done
