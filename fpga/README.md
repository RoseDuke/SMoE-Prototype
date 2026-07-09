# FPGA SmartNIC Prototype

> [!CAUTION]
> **P0 — A successful build is not safely preserved until artifact handoff is
> complete.** `fpga/build/` is ignored by git. Committing and pushing the
> repository will not upload the xclbin. Do not release or reimage the build
> node until the xclbin has been copied to the FPGA node or durable storage and
> its checksum has been verified.

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

## P0 Artifact Handoff from Build Node to FPGA Node

The hardware build writes the final artifact to:

```text
<repo>/fpga/build/smartnic_moe_dispatch_v0.xclbin
```

This directory is intentionally ignored by git. GitHub is not an artifact
transport, and `git commit` or `git push` will not preserve this file.

Immediately after a successful `TARGET=hw` build, run the following on the
**build node**:

```bash
cd /path/to/SMoE-Prototype/fpga/build
test -s smartnic_moe_dispatch_v0.xclbin
xclbinutil --info \
  --input smartnic_moe_dispatch_v0.xclbin \
  > smartnic_moe_dispatch_v0.xclbin.info
sha256sum smartnic_moe_dispatch_v0.xclbin \
  > smartnic_moe_dispatch_v0.xclbin.sha256
```

Copy the artifact, checksum, and metadata to the **FPGA node**. `rsync` is
preferred because a large interrupted transfer can be resumed:

```bash
ssh USER@FPGA_NODE 'mkdir -p /path/to/SMoE-Prototype/fpga/build'
rsync -avP \
  smartnic_moe_dispatch_v0.xclbin \
  smartnic_moe_dispatch_v0.xclbin.sha256 \
  smartnic_moe_dispatch_v0.xclbin.info \
  USER@FPGA_NODE:/path/to/SMoE-Prototype/fpga/build/
```

On the **FPGA node**, verify the copy before releasing the build node:

```bash
cd /path/to/SMoE-Prototype/fpga/build
sha256sum -c smartnic_moe_dispatch_v0.xclbin.sha256
xclbinutil --info --input smartnic_moe_dispatch_v0.xclbin
xbutil examine
```

The checksum command must report `OK`. The xclbin platform must match the shell
installed on the FPGA node. Only after these checks pass is artifact handoff
complete. If direct SSH transfer is unavailable, upload the same three files to
durable project/object storage or a GitHub Release, then download and verify
them on the FPGA node.

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
