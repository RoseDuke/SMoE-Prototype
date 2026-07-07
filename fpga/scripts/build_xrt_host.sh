#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
BUILD_DIR="${REPO_ROOT}/build"
XRT_ROOT="${XILINX_XRT:-/opt/xilinx/xrt}"

if [[ ! -d "${XRT_ROOT}/include" || ! -d "${XRT_ROOT}/lib" ]]; then
  echo "XRT include/lib directories were not found under ${XRT_ROOT}" >&2
  exit 1
fi

mkdir -p "${BUILD_DIR}"

g++ -std=c++17 -Wall -Wextra -Wpedantic -Werror \
  -I"${REPO_ROOT}/fpga/include" \
  -I"${REPO_ROOT}/fpga/host" \
  -I"${XRT_ROOT}/include" \
  "${REPO_ROOT}/fpga/host/load_trace.cpp" \
  "${REPO_ROOT}/fpga/host/run_trace_xrt.cpp" \
  -L"${XRT_ROOT}/lib" \
  -lxrt_coreutil \
  -pthread \
  -Wl,-rpath,"${XRT_ROOT}/lib" \
  -o "${BUILD_DIR}/fpga_run_trace_xrt"

echo "Built XRT host runner: ${BUILD_DIR}/fpga_run_trace_xrt"
