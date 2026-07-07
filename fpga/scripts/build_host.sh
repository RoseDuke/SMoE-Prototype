#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
BUILD_DIR="${REPO_ROOT}/build"
CMAKE_BIN="${CMAKE_BIN:-cmake}"

if command -v "${CMAKE_BIN}" >/dev/null 2>&1 &&
   "${CMAKE_BIN}" -S "${REPO_ROOT}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Debug; then
  "${CMAKE_BIN}" --build "${BUILD_DIR}" --target fpga_run_trace fpga_verify_against_sim smartnic_ref -j
else
  echo "CMake configure failed or is unavailable; falling back to direct g++ build." >&2
  mkdir -p "${BUILD_DIR}"
  g++ -std=c++17 -Wall -Wextra -Wpedantic -Werror \
    -I"${REPO_ROOT}/fpga/include" \
    -I"${REPO_ROOT}/fpga/host" \
    "${REPO_ROOT}/fpga/hls/top.cpp" \
    "${REPO_ROOT}/fpga/host/load_trace.cpp" \
    "${REPO_ROOT}/fpga/host/run_trace.cpp" \
    -o "${BUILD_DIR}/fpga_run_trace"

  g++ -std=c++17 -Wall -Wextra -Wpedantic -Werror \
    "${REPO_ROOT}/fpga/host/verify_against_sim.cpp" \
    -o "${BUILD_DIR}/fpga_verify_against_sim"

  g++ -std=c++17 -Wall -Wextra -Wpedantic -Werror \
    -I"${REPO_ROOT}/software_model/include" \
    "${REPO_ROOT}/software_model/src/config.cpp" \
    "${REPO_ROOT}/software_model/src/metrics.cpp" \
    "${REPO_ROOT}/software_model/src/scheduler.cpp" \
    "${REPO_ROOT}/software_model/src/simulator.cpp" \
    "${REPO_ROOT}/software_model/src/trace_reader.cpp" \
    "${REPO_ROOT}/software_model/src/main.cpp" \
    -o "${BUILD_DIR}/smartnic_ref"
fi

"${SCRIPT_DIR}/build_xrt_host.sh"

echo "Built FPGA V0 host targets in ${BUILD_DIR}"
