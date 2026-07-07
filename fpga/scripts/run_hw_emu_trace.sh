#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FPGA_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
REPO_ROOT="$(cd "${FPGA_ROOT}/.." && pwd)"

XCLBIN="${FPGA_ROOT}/build/smartnic_moe_dispatch_v0.xclbin"
TRACE="${REPO_ROOT}/tests/traces/tiny_skewed.csv"
CONFIG="${REPO_ROOT}/configs/full_smartnic.cfg"
OUT="${REPO_ROOT}/results/fpga_hw_emu_smoke"
DEVICE="0"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --xclbin)
      XCLBIN="$2"
      shift 2
      ;;
    --trace)
      TRACE="$2"
      shift 2
      ;;
    --config)
      CONFIG="$2"
      shift 2
      ;;
    --out)
      OUT="$2"
      shift 2
      ;;
    --device)
      DEVICE="$2"
      shift 2
      ;;
    *)
      echo "Unknown argument: $1" >&2
      exit 1
      ;;
  esac
done

if [[ ! -f "${FPGA_ROOT}/build/emconfig.json" ]]; then
  echo "Missing ${FPGA_ROOT}/build/emconfig.json; run build_hw_emu.sh first." >&2
  exit 1
fi

if [[ ! -x "${REPO_ROOT}/build/fpga_run_trace_xrt" ]]; then
  "${SCRIPT_DIR}/build_xrt_host.sh"
fi

XCLBIN="$(realpath "${XCLBIN}")"
TRACE="$(realpath "${TRACE}")"
CONFIG="$(realpath "${CONFIG}")"
OUT="$(realpath -m "${OUT}")"

mkdir -p "${OUT}"

(
  cd "${FPGA_ROOT}/build"
  XCL_EMULATION_MODE=hw_emu "${REPO_ROOT}/build/fpga_run_trace_xrt" \
    --xclbin "${XCLBIN}" \
    --trace "${TRACE}" \
    --config "${CONFIG}" \
    --out "${OUT}" \
    --device "${DEVICE}"
)

echo "HW emulation trace results: ${OUT}"
