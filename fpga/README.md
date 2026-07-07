# FPGA SmartNIC Prototype

This directory contains the FPGA/HLS implementation scaffold for the
trace-driven SmartNIC dispatch prototype.

The first hardware target is the V0 SmartNIC dispatch primitive described in
`../SMARTNIC_PROTOTYPE_README.md`:

```text
trace / GPU-produced token descriptors
  -> descriptor ingress
  -> metadata parser
  -> per-destination queues
  -> batching / counter / reorder / credit checks
  -> scheduler
  -> packet output descriptors
  -> completion path
  -> metrics
```

The existing C++ reference model remains the golden model. FPGA outputs should
be compared against `smartnic_ref` for the same trace and runtime config.

## Layout

```text
include/   Shared FPGA ABI and runtime configuration headers.
hls/       Vitis HLS kernels and top-level SmartNIC dispatch pipeline.
host/      XRT host code, trace loading, and simulator comparison tools.
scripts/   Build and run helpers for emulation and U280 validation.
tests/     Binary smoke/stress traces for FPGA validation.
```

## Initial Milestones

```text
M0: binary trace format, host loader, host-side verifier
M1: HLS baseline/async descriptor-to-packet pipeline
M2: aggregation, expert counters, and blocked-token reorder
M3: credit manager and completion return path
M4: U280 build and smoke validation
M5: V1 batch overlap extension
```

V0 is trace-driven and does not model a real GPU-to-SmartNIC link. V1 should
add `gpu_ready_cycle`, `nic_visible_cycle`, handoff latency, and overlap-depth
metrics once the base dispatch primitive is stable.

## V0 Host Smoke Test

Build and compare the V0 HLS functional model against the C++ simulator:

```bash
bash fpga/scripts/run_smoke.sh
```

Direct run:

```bash
./build/fpga_run_trace \
  --trace tests/traces/tiny_skewed.csv \
  --config configs/full_smartnic.cfg \
  --out results/fpga_v0_manual
```

The run writes `tokens.csv`, `packets.csv`, `summary.txt`, and `metrics.json`.

## HW Emulation / U280 Path

Build the XRT host runner and an emulation xclbin:

```bash
cd fpga
export PLATFORM=xilinx_u280_gen3x16_xdma_1_202211_1
export TARGET=hw_emu
bash scripts/build_hw_emu.sh
```

Run hardware emulation from the generated `emconfig.json` directory:

```bash
bash scripts/run_hw_emu_trace.sh \
  --xclbin build/smartnic_moe_dispatch_v0.xclbin \
  --trace ../tests/traces/tiny_skewed.csv \
  --config ../configs/full_smartnic.cfg \
  --out ../results/fpga_hw_emu_smoke
```

On a U280 node, build `TARGET=hw` and run:

```bash
bash scripts/run_u280_trace.sh \
  --xclbin build/smartnic_moe_dispatch_v0.xclbin \
  --trace ../tests/traces/tiny_skewed.csv \
  --config ../configs/full_smartnic.cfg \
  --out ../results/fpga_u280_smoke
```
