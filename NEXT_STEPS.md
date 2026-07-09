# Next Steps

Date: 2026-07-08

## P0 Blocker: Rebuild and Preserve the Hardware Artifact

> [!CAUTION]
> The previous U280 build completed, but its xclbin was left only in the build
> node's git-ignored `fpga/build/` directory. It was not pushed to GitHub and is
> not available in a fresh clone. A new hardware build is required.

The highest-priority task is to rebuild the U280 xclbin and complete artifact
handoff before releasing or reimaging the build node:

1. Rebuild `fpga/build/smartnic_moe_dispatch_v0.xclbin` on the build node.
2. Confirm that the file exists and is non-empty.
3. Generate its `xclbinutil` metadata and SHA-256 checksum.
4. Transfer the xclbin, metadata, and checksum to the FPGA node or durable
   storage.
5. Verify the checksum and platform compatibility on the FPGA node.
6. Run the U280 smoke test.

The exact transfer and verification commands are documented in
[`fpga/README.md`](fpga/README.md#p0-artifact-handoff-from-build-node-to-fpga-node).
The build node must remain available until the checksum reports `OK` on the
destination.

## Previous Build Result (Artifact Not Preserved)

The previous build generated this local-only artifact:

```text
fpga/build/smartnic_moe_dispatch_v0.xclbin
```

Build target:

```text
--target hw
```

Platform:

```text
xilinx_u280_gen3x16_xdma_1_202211_1
```

Build output summary:

```text
Created fpga/build/smartnic_moe_dispatch_v0.xclbin
Total elapsed time: 3h 40m 24s
```

Important timing note:

```text
DATA_CLK requested: 300 MHz
DATA_CLK achieved:  281.4 MHz
```

Vitis auto-scaled the data clock down because one or more timing paths missed
the original 300 MHz target. Treat the bitstream as a valid first hardware
bring-up artifact, but record the achieved clock when reporting results.

The build directory is intentionally ignored by git. Consequently, this
artifact was not included in the `bulid finished` commit or pushed to GitHub.

## Immediate Next Step After Rebuild and Handoff

Run the U280 smoke test:

```bash
cd /users/RoseDuke/SMoE-Prototype/fpga
bash scripts/run_u280_trace.sh \
  --xclbin build/smartnic_moe_dispatch_v0.xclbin \
  --trace ../tests/traces/tiny_skewed.csv \
  --config ../configs/full_smartnic.cfg \
  --out ../results/fpga_u280_smoke_run001
```

Expected behavior:

- XRT should program the U280 with `smartnic_moe_dispatch_v0.xclbin`.
- The host runner should emit `tokens.csv`, `packets.csv`, and `summary.txt`
  under `results/fpga_u280_smoke_run001`.
- The run should complete without XRT programming or kernel execution errors.

## Validate Against Simulator

After the U280 run, regenerate the simulator output and compare token records:

```bash
cd /users/RoseDuke/SMoE-Prototype
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

The expected validation message is:

```text
FPGA V0 token records match simulator output: 10 rows
```

## If The U280 Run Fails

First check that the card is visible:

```bash
xbutil examine
```

Then check whether the xclbin platform matches the installed shell:

```bash
xclbinutil --info --input fpga/build/smartnic_moe_dispatch_v0.xclbin
```

The xclbin should report:

```text
Platform VBNV: xilinx_u280_gen3x16_xdma_1_202211_1
Content: Bitstream
Kernels: smartnic_moe_dispatch_v0
```

## After Smoke Passes

1. Save the U280 smoke outputs that matter: `tokens.csv`, `packets.csv`, and
   `summary.txt`.
2. Run a slightly larger trace/config matrix on hardware and compare each run
   against the simulator.
3. Decide whether the 281.4 MHz achieved data clock is acceptable for the first
   prototype report, or whether to reduce timing pressure and rebuild for a
   cleaner 300 MHz target.
4. If the result should be preserved outside this machine, upload the xclbin as
   a release artifact rather than committing it into git.
