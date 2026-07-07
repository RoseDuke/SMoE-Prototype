# FPGA Resume Notes

Date: 2026-07-07

This file records the FPGA bring-up state before the current CloudLab/server
workspace is discarded. Read this first when resuming the project on a new
machine.

## Current State

The repository has a working V0 SmartNIC FPGA prototype scaffold:

- HLS/C++ top function: `fpga/hls/top.cpp`
- XRT host runner: `fpga/host/run_trace_xrt.cpp`
- build scripts: `fpga/scripts/build_hw_emu.sh`, `fpga/scripts/build_u280.sh`
- run scripts: `fpga/scripts/run_hw_emu_trace.sh`, `fpga/scripts/run_u280_trace.sh`

Software/HLS functional validation passed before hardware build:

```text
FPGA V0 token records match simulator output: 10 rows
```

The 4 trace x 5 config host-side matrix also matched the C++ simulator during
this session. A manual credit-pressure check also matched simulator output and
produced nonzero credit stalls.

## Committed HW Emulation Result

The following small hardware-emulation smoke result is committed:

```text
results/fpga_hw_emu_smoke_run001/
```

It was generated with:

```bash
cd /users/RoseDuke/SMoE-Prototype/fpga
emconfigutil --platform xilinx_u280_gen3x16_xdma_1_202211_1 --od build
bash scripts/run_hw_emu_trace.sh \
  --xclbin build/smartnic_moe_dispatch_v0.xclbin \
  --trace ../tests/traces/tiny_skewed.csv \
  --config ../configs/full_smartnic.cfg \
  --out ../results/fpga_hw_emu_smoke_run001
```

Important result values:

```text
Total tokens: 10
Total packets: 4
Final cycle: 346
Average latency cycles: 79.8
Maximum queue depth: 4
Total credit stall cycles: 0
Total counter stall cycles: 0
```

The hw_emu output was compared against simulator golden output. On a fresh
checkout, regenerate the simulator tokens first:

```bash
mkdir -p results/fpga_hw_emu_smoke_run001_sim
./build/smartnic_ref \
  --trace tests/traces/tiny_skewed.csv \
  --config configs/full_smartnic.cfg \
  --output results/fpga_hw_emu_smoke_run001_sim/tokens.csv \
  --summary results/fpga_hw_emu_smoke_run001_sim/summary.txt
./build/fpga_verify_against_sim \
  --sim results/fpga_hw_emu_smoke_run001_sim/tokens.csv \
  --hw results/fpga_hw_emu_smoke_run001/tokens.csv
```

and matched:

```text
FPGA V0 token records match simulator output: 10 rows
```

## Build Script Fix

`fpga/scripts/build_hw_emu.sh` was updated so repeated builds do not fail when
`smartnic_moe_dispatch_v0.xclbin.info` already exists. The script now calls:

```bash
xclbinutil --quiet --force ...
```

## Hardware Build Status

`TARGET=hw` was attempted for:

```text
xilinx_u280_gen3x16_xdma_1_202211_1
```

The build reached Vivado/VPL synthesis but failed while synthesizing the HLS
kernel IP:

```text
ulp_smartnic_moe_dispatch_v0_1_0_synth_1
```

The important failure symptom was not a Verilog/C++ syntax error. Vivado was
killed by the system while memory was low. The kernel synth log ended with:

```text
Netlist sorting complete.
/fpga/Xilinx/Vivado/2023.2/bin/rdiArgs.sh: line 369: <pid> Killed "$RDI_PROG" "$@"
Parent process (...) has died.
```

The first attempt reached about:

```text
free physical = 171 MB
```

The resume attempt from `vpl.synth` also killed the kernel synth process after
RTL optimization/netlist sorting, although the wrapper process continued
running platform IP synth jobs for a while.

Conclusion:

```text
hw_emu passes.
TARGET=hw has not produced a valid hardware xclbin yet.
The blocker is likely build memory pressure, not functional mismatch.
```

Do not assume any local `fpga/build/smartnic_moe_dispatch_v0.xclbin` from this
machine is a valid hardware bitstream. Before the failed `TARGET=hw` run, that
file was the previous `hw_emu` xclbin.

## Recommended Resume Path

On the next server, first re-run host-side and hw_emu validation if desired:

```bash
cd /users/RoseDuke/SMoE-Prototype
bash fpga/scripts/run_smoke.sh
```

Then build hw_emu if the build artifacts are absent:

```bash
cd /users/RoseDuke/SMoE-Prototype/fpga
export PLATFORM=xilinx_u280_gen3x16_xdma_1_202211_1
export TARGET=hw_emu
bash scripts/build_hw_emu.sh
```

Run hw_emu:

```bash
bash scripts/run_hw_emu_trace.sh \
  --xclbin build/smartnic_moe_dispatch_v0.xclbin \
  --trace ../tests/traces/tiny_skewed.csv \
  --config ../configs/full_smartnic.cfg \
  --out ../results/fpga_hw_emu_smoke_run002
```

For real U280 hardware build, prefer a build node with at least 32 GB RAM;
64 GB is safer:

```bash
cd /users/RoseDuke/SMoE-Prototype/fpga
export PLATFORM=xilinx_u280_gen3x16_xdma_1_202211_1
export TARGET=hw
bash scripts/build_u280.sh
```

If a previous failed VPL project exists and the machine has enough memory, a
resume build can be attempted with:

```bash
cd /users/RoseDuke/SMoE-Prototype/fpga
export PLATFORM=xilinx_u280_gen3x16_xdma_1_202211_1
v++ -l -t hw \
  --platform "$PLATFORM" \
  --from_step vpl.synth \
  --vivado.synth.jobs 1 \
  --vivado.impl.jobs 1 \
  build/smartnic_moe_dispatch_v0.xo \
  -o build/smartnic_moe_dispatch_v0.xclbin
```

## If Hardware Build Still Runs Out Of Memory

Use one of these paths:

1. Move to a larger build node.
2. Add swap before running Vivado/Vitis.
3. Create a smaller FPGA smoke profile for first U280 bring-up by reducing
   constants in `fpga/include/smartnic_config.hpp`, especially:

```text
kMaxTokens
kMaxQueueDepth
kMaxDestinations
kMaxExpertsPerDestination
kMaxEvents
```

Then rerun host smoke, hw_emu, and `TARGET=hw` build. This smaller profile would
be a bring-up artifact, not the final V0 capacity target.

## Useful Validation Commands

Compare any hardware/hw_emu token output to simulator output:

```bash
./build/fpga_verify_against_sim \
  --sim <sim_tokens.csv> \
  --hw <hw_tokens.csv>
```

Check whether a U280 is visible on a run node:

```bash
xbutil examine
```

Run U280 smoke after a valid `TARGET=hw` xclbin exists:

```bash
cd /users/RoseDuke/SMoE-Prototype/fpga
bash scripts/run_u280_trace.sh \
  --xclbin build/smartnic_moe_dispatch_v0.xclbin \
  --trace ../tests/traces/tiny_skewed.csv \
  --config ../configs/full_smartnic.cfg \
  --out ../results/fpga_u280_smoke_run001
```
