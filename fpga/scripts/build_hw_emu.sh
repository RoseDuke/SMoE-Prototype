#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FPGA_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
REPO_ROOT="$(cd "${FPGA_ROOT}/.." && pwd)"
TARGET="${TARGET:-hw_emu}"
VIVADO_SYNTH_JOBS="${VIVADO_SYNTH_JOBS:-1}"
VIVADO_IMPL_JOBS="${VIVADO_IMPL_JOBS:-1}"

if [[ -z "${PLATFORM:-}" ]]; then
  echo "Set PLATFORM to the installed U280 platform name before running this script." >&2
  exit 1
fi

mkdir -p "${FPGA_ROOT}/build"

"${SCRIPT_DIR}/build_xrt_host.sh"

v++ -c \
  -t "${TARGET}" \
  --platform "${PLATFORM}" \
  -k smartnic_moe_dispatch_v0 \
  -I "${FPGA_ROOT}/include" \
  "${FPGA_ROOT}/hls/top.cpp" \
  -o "${FPGA_ROOT}/build/smartnic_moe_dispatch_v0.xo"

v++ -l \
  -t "${TARGET}" \
  --platform "${PLATFORM}" \
  --vivado.synth.jobs "${VIVADO_SYNTH_JOBS}" \
  --vivado.impl.jobs "${VIVADO_IMPL_JOBS}" \
  "${FPGA_ROOT}/build/smartnic_moe_dispatch_v0.xo" \
  -o "${FPGA_ROOT}/build/smartnic_moe_dispatch_v0.xclbin"

if [[ ! -s "${FPGA_ROOT}/build/smartnic_moe_dispatch_v0.xclbin" ]]; then
  echo "v++ link did not produce a non-empty xclbin." >&2
  exit 1
fi

xclbinutil --quiet --force \
  --info "${FPGA_ROOT}/build/smartnic_moe_dispatch_v0.xclbin.info" \
  --input "${FPGA_ROOT}/build/smartnic_moe_dispatch_v0.xclbin"

if [[ "${TARGET}" == "hw_emu" || "${TARGET}" == "sw_emu" ]]; then
  emconfigutil --platform "${PLATFORM}" --od "${FPGA_ROOT}/build"
fi

echo "Built ${TARGET} xclbin: ${FPGA_ROOT}/build/smartnic_moe_dispatch_v0.xclbin"
echo "Built XRT host runner: ${REPO_ROOT}/build/fpga_run_trace_xrt"
