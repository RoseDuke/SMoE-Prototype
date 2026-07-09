# Next Steps

Date: 2026-07-09

## Current Status

The U280 runtime artifacts are now tracked in git:

```text
build/fpga_run_trace_xrt
fpga/build/smartnic_moe_dispatch_v0.xclbin
```

A fresh clone on a U280 FPGA node should be able to run the smoke test without
rebuilding the host executable or the xclbin.

The U280 node still needs the XRT runtime, driver, and matching shell:

```text
/opt/xilinx/xrt/lib/libxrt_coreutil.so.2
xilinx_u280_gen3x16_xdma_1_202211_1
```

## What To Do After Opening a U280 Node

Clone the repository:

```bash
git clone git@github.com:RoseDuke/SMoE-Prototype.git
cd SMoE-Prototype
```

If the repository already exists on the U280 node, update it:

```bash
cd SMoE-Prototype
git pull
```

Check that the committed runtime artifacts are present:

```bash
test -x build/fpga_run_trace_xrt
test -s fpga/build/smartnic_moe_dispatch_v0.xclbin
```

Check the FPGA/XRT runtime:

```bash
xbutil examine
ldd build/fpga_run_trace_xrt | grep xrt_coreutil
xclbinutil --info --input fpga/build/smartnic_moe_dispatch_v0.xclbin
```

Run the U280 smoke test:

```bash
bash fpga/scripts/run_u280_trace.sh \
  --out results/fpga_u280_smoke_run001
```

Expected outputs:

```text
results/fpga_u280_smoke_run001/tokens.csv
results/fpga_u280_smoke_run001/packets.csv
results/fpga_u280_smoke_run001/summary.txt
results/fpga_u280_smoke_run001/metrics.json
```

## Validate Against the Simulator

If the U280 node also has the already-built simulator tools, compare hardware
tokens against the simulator:

```bash
mkdir -p results/fpga_u280_smoke_run001_sim
./build/smartnic_ref \
  --trace tests/traces/tiny_skewed.csv \
  --config configs/full_smartnic.cfg \
  --output results/fpga_u280_smoke_run001_sim/tokens.csv \
  --summary results/fpga_u280_smoke_run001_sim/summary.txt
./build/fpga_verify_against_sim \
  --sim results/fpga_u280_smoke_run001_sim/tokens.csv \
  --hw results/fpga_u280_smoke_run001/tokens.csv
```

Expected validation message:

```text
FPGA V0 token records match simulator output: 10 rows
```

If those simulator binaries are not present on the U280 runtime node, copy the
hardware output files back to a build-capable machine and run the comparison
there.
