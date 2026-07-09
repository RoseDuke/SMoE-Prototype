# FPGA SmartNIC Prototype

> [!CAUTION]
> **Runtime artifact policy.** Most generated FPGA build files are ignored by
> git, but the U280 smoke-test runtime artifacts are intentionally tracked:
> `build/fpga_run_trace_xrt` and
> `fpga/build/smartnic_moe_dispatch_v0.xclbin`. A fresh clone can run the U280
> smoke test without rebuilding, provided the FPGA node has the matching U280
> shell and XRT runtime installed.

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

## Run on a U280 FPGA Node from a Fresh Clone

Use this path when the U280 node is only a runtime machine and should not build
the host executable or the xclbin.

On the **U280 FPGA node**, clone or update the repository:

```bash
git clone git@github.com:RoseDuke/SMoE-Prototype.git
cd SMoE-Prototype
```

If the repository already exists on the U280 node, update it instead:

```bash
cd SMoE-Prototype
git pull
```

Confirm the required tracked runtime files are present:

```bash
test -x build/fpga_run_trace_xrt
test -s fpga/build/smartnic_moe_dispatch_v0.xclbin
```

Confirm the node can see the FPGA and has XRT available:

```bash
xbutil examine
ldd build/fpga_run_trace_xrt | grep xrt_coreutil
```

Run the default U280 smoke test:

```bash
bash fpga/scripts/run_u280_trace.sh
```

The default run uses:

```text
xclbin: fpga/build/smartnic_moe_dispatch_v0.xclbin
trace:  tests/traces/tiny_skewed.csv
config: configs/full_smartnic.cfg
out:    results/fpga_u280_smoke
```

To choose a different output directory, run:

```bash
bash fpga/scripts/run_u280_trace.sh \
  --out results/fpga_u280_smoke_run001
```

Expected outputs:

```text
results/fpga_u280_smoke*/tokens.csv
results/fpga_u280_smoke*/packets.csv
results/fpga_u280_smoke*/summary.txt
results/fpga_u280_smoke*/metrics.json
```

If the run fails before launching the kernel, first verify that the installed
shell matches the xclbin:

```bash
xclbinutil --info --input fpga/build/smartnic_moe_dispatch_v0.xclbin
```

The xclbin should report:

```text
Platform VBNV: xilinx_u280_gen3x16_xdma_1_202211_1
Kernels: smartnic_moe_dispatch_v0
Content: Bitstream
```

## Artifact Handoff from Build Node to FPGA Node

The hardware build writes the final artifact to:

```text
<repo>/fpga/build/smartnic_moe_dispatch_v0.xclbin
```

Most files in this directory are intentionally ignored by git. The final U280
smoke-test xclbin is the one exception currently tracked in this repository.
For future rebuilds, preserve the new artifact before releasing or reimaging
the build node.

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
complete.

Run hardware emulation from the generated `emconfig.json` directory:

```bash
bash scripts/run_hw_emu_trace.sh \
  --xclbin build/smartnic_moe_dispatch_v0.xclbin \
  --trace ../tests/traces/tiny_skewed.csv \
  --config ../configs/full_smartnic.cfg \
  --out ../results/fpga_hw_emu_smoke
```

On a U280 node with the tracked runtime artifacts, run:

```bash
bash fpga/scripts/run_u280_trace.sh
```
