# TODO: U280 Minimal Evidence Package

Date: 2026-08-15

This TODO defines the minimum CloudLab/OCT-testbed U280 evidence needed to
support the current paper positioning:

```text
profiling + trace-driven simulator + early FPGA/HLS SmartNIC prototype
```

The goal is not to prove end-to-end Mixtral inference speedup. The goal is to
demonstrate that the SmartNIC dispatch primitive is implemented as a functional
FPGA kernel, runs on U280, and produces outputs consistent with the C++
simulator.

## Minimum Claim To Support

The paper should be able to claim:

```text
We implemented the SmartNIC dispatch primitive as a U280-targeted FPGA/HLS
functional kernel. On a trace-driven smoke test, the FPGA prototype executes
successfully and produces token-level outputs matching the C++ simulator,
demonstrating implementability of the proposed dispatch dataflow.
```

The paper should not claim:

```text
end-to-end MoE inference speedup
real GPU-to-SmartNIC handoff performance
real NCCL utilization improvement
real inter-node RDMA/SmartNIC speedup
implemented batch-overlap speedup
```

## 1. Confirm Repository And Runtime Artifacts

On the U280 node:

```bash
git clone git@github.com:RoseDuke/SMoE-Prototype.git
cd SMoE-Prototype
```

If the repository already exists:

```bash
cd SMoE-Prototype
git pull
```

Confirm commit and required runtime artifacts:

```bash
git rev-parse HEAD
test -x build/fpga_run_trace_xrt
test -s fpga/build/smartnic_moe_dispatch_v0.xclbin
ls -lh build/fpga_run_trace_xrt fpga/build/smartnic_moe_dispatch_v0.xclbin
```

## 2. Capture Environment Evidence

Create a result directory:

```bash
OUT=results/fpga_u280_minimal_2026_08_15
mkdir -p "${OUT}/environment"
```

Save reproducibility metadata:

```bash
git rev-parse HEAD > "${OUT}/environment/git_commit.txt"
git status --short > "${OUT}/environment/git_status.txt"
uname -a > "${OUT}/environment/uname.txt"

xbutil examine > "${OUT}/environment/xbutil_examine.txt"
xrt-smi examine > "${OUT}/environment/xrt_smi_examine.txt" 2>&1 || true

xclbinutil --info \
  --input fpga/build/smartnic_moe_dispatch_v0.xclbin \
  > "${OUT}/environment/xclbin_info.txt"

sha256sum fpga/build/smartnic_moe_dispatch_v0.xclbin \
  > "${OUT}/environment/xclbin.sha256"

ldd build/fpga_run_trace_xrt > "${OUT}/environment/host_ldd.txt"
```

The important platform string should match the U280 shell used to build the
artifact:

```text
xilinx_u280_gen3x16_xdma_1_202211_1
```

## 3. Run The Minimal U280 Functional Smoke Test

Run the FPGA prototype on the default trace and full SmartNIC configuration:

```bash
OUT=results/fpga_u280_minimal_2026_08_15
mkdir -p "${OUT}/run_full_smartnic"

/usr/bin/time -v \
  bash fpga/scripts/run_u280_trace.sh \
    --trace tests/traces/tiny_skewed.csv \
    --config configs/full_smartnic.cfg \
    --out "${OUT}/run_full_smartnic" \
  > "${OUT}/run_full_smartnic/run.log" 2>&1
```

Required outputs:

```text
run_full_smartnic/tokens.csv
run_full_smartnic/packets.csv
run_full_smartnic/summary.txt
run_full_smartnic/metrics.json
run_full_smartnic/run.log
```

Required metric checks:

```text
status_code = 0
queue_overflow = 0
malformed_descriptor = 0
event_overflow = 0
deadlock = 0
```

For the current tiny skewed smoke trace, expected high-level output is:

```text
total tokens = 10
total packets = 4
```

## 4. Validate U280 Output Against C++ Simulator

Run the simulator with the same trace and config:

```bash
OUT=results/fpga_u280_minimal_2026_08_15
mkdir -p "${OUT}/sim_full_smartnic"

./build/smartnic_ref \
  --trace tests/traces/tiny_skewed.csv \
  --config configs/full_smartnic.cfg \
  --output "${OUT}/sim_full_smartnic/tokens.csv" \
  --summary "${OUT}/sim_full_smartnic/summary.txt"
```

Compare token records:

```bash
./build/fpga_verify_against_sim \
  --sim "${OUT}/sim_full_smartnic/tokens.csv" \
  --hw "${OUT}/run_full_smartnic/tokens.csv" \
  > "${OUT}/run_full_smartnic/verify_against_sim.txt"
```

The expected validation text is:

```text
FPGA V0 token records match simulator output: 10 rows
```

This is the most important correctness evidence.

## 5. Optional But Useful Ablation Smoke Tests

If time allows, run the same U280 smoke trace across the simulator configs:

```text
configs/synchronous_baseline.cfg
configs/async_only.cfg
configs/async_aggregation.cfg
configs/async_expert_counter.cfg
configs/full_smartnic.cfg
```

This is not required for the minimum paper evidence, but it shows that the FPGA
kernel supports the same optimization switches as the simulator.

Suggested loop:

```bash
OUT=results/fpga_u280_minimal_2026_08_15
for cfg in synchronous_baseline async_only async_aggregation async_expert_counter full_smartnic; do
  mkdir -p "${OUT}/ablations/${cfg}"
  /usr/bin/time -v \
    bash fpga/scripts/run_u280_trace.sh \
      --trace tests/traces/tiny_skewed.csv \
      --config "configs/${cfg}.cfg" \
      --out "${OUT}/ablations/${cfg}" \
    > "${OUT}/ablations/${cfg}/run.log" 2>&1
done
```

## 6. Hardware Design Information Needed For The Paper

Prepare or update the following design artifacts:

```text
1. System-level diagram
   GPU router -> SmartNIC dispatch primitive -> receiver GPU expert input.

2. FPGA pipeline diagram
   descriptor ingress -> metadata parser -> destination queues
   -> batching/counter/credit/reorder -> scheduler -> packet records
   -> completion/metrics.

3. Host/XRT interface diagram
   descriptor BO, config BO, token-record BO, packet-record BO, metrics BO.

4. Simulator-vs-FPGA validation diagram
   same trace + same config -> simulator and U280 -> compare tokens.csv.
```

The paper should also list the implemented ABI:

```text
SmartnicTokenDescriptor
SmartnicRuntimeConfig
SmartnicTokenRecord
SmartnicPacketRecord
SmartnicMetrics
```

These are defined in:

```text
fpga/include/smartnic_abi.hpp
fpga/include/smartnic_config.hpp
```

## 7. Final Minimum Evidence Directory

The final directory to bring back from OCT-testbed should look like:

```text
results/fpga_u280_minimal_2026_08_15/
  environment/
    git_commit.txt
    git_status.txt
    uname.txt
    xbutil_examine.txt
    xrt_smi_examine.txt
    xclbin_info.txt
    xclbin.sha256
    host_ldd.txt

  run_full_smartnic/
    tokens.csv
    packets.csv
    summary.txt
    metrics.json
    run.log
    verify_against_sim.txt

  sim_full_smartnic/
    tokens.csv
    summary.txt

  ablations/                 # optional
```

## Completion Criteria

The minimum evidence package is complete when:

```text
1. U280 run finishes successfully.
2. metrics.json reports status_code = 0.
3. no queue/malformed/event/deadlock errors are reported.
4. tokens.csv, packets.csv, summary.txt, and metrics.json are saved.
5. fpga_verify_against_sim reports exact token-record match.
6. environment and xclbin metadata are saved.
7. hardware design diagrams are available for the paper.
```

Once these criteria are met, the current paper can support an early FPGA
prototype claim.
