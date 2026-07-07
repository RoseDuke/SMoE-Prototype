#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
BUILD_DIR="${REPO_ROOT}/build"
RESULT_DIR="${REPO_ROOT}/results/fpga_v0_smoke"

"${SCRIPT_DIR}/build_host.sh"

mkdir -p "${RESULT_DIR}/sim" "${RESULT_DIR}/fpga"

"${BUILD_DIR}/smartnic_ref" \
  --trace "${REPO_ROOT}/tests/traces/tiny_skewed.csv" \
  --config "${REPO_ROOT}/configs/full_smartnic.cfg" \
  --output "${RESULT_DIR}/sim/tokens.csv" \
  --summary "${RESULT_DIR}/sim/summary.txt"

"${BUILD_DIR}/fpga_run_trace" \
  --trace "${REPO_ROOT}/tests/traces/tiny_skewed.csv" \
  --config "${REPO_ROOT}/configs/full_smartnic.cfg" \
  --out "${RESULT_DIR}/fpga"

"${BUILD_DIR}/fpga_verify_against_sim" \
  --sim "${RESULT_DIR}/sim/tokens.csv" \
  --hw "${RESULT_DIR}/fpga/tokens.csv"

echo "FPGA V0 smoke results: ${RESULT_DIR}"
