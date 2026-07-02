# Project Checkpoint

Date: 2026-07-02

This checkpoint captures the current state before shifting focus to the
SmartNIC primitive design.

## Repository State

Primary repo:

```text
/projectnb/caad/shiningy/SMoE-Prototype
```

Remote:

```text
git@github.com:RoseDuke/SMoE-Prototype.git
```

Current pushed branches before this checkpoint:

```text
main
2026-07-02
```

Important committed work already in the repo:

- event-driven C++17 SmartNIC reference simulator;
- staged optimization configs;
- optimization sweep runner;
- plotting script;
- SmartNIC optimization figures.

## Current Simulator Scope

The simulator is a deterministic scheduling-level model. It models how routed
MoE tokens become SmartNIC-visible, enter per-destination queues, and are
dispatched under different SmartNIC policies.

Currently modeled:

- basic synchronous MoE baseline;
- asynchronous SmartNIC-visible token enqueue;
- per-destination packet aggregation;
- expert counter admission control;
- blocked-token reorder;
- credit-control primitive;
- deterministic scheduling policies;
- per-token CSV output and summary metrics.

Not yet modeled:

- explicit GPU-to-SmartNIC handoff latency;
- PCIe/NVLink/RDMA timing;
- real inter-node switch contention;
- receiver GPU kernel occupancy;
- fused MoE microbatch efficiency;
- multiple batches in flight;
- hidden vs exposed overlap latency.

The current `arrival_cycle` should be interpreted as:

```text
token is already visible to the SmartNIC
```

It is not yet:

```text
token is produced on GPU before GPU-to-SmartNIC handoff
```

## Current Result Snapshot

Run used for the main figures:

```text
results/optimization_sweep_run001
```

Main comparison against the basic synchronous MoE baseline:

```text
SmartNIC-side batching average latency reduction: 13.3%
Full SmartNIC average latency reduction:          17.1%
Full SmartNIC packet reduction:                   74.9%
Full SmartNIC final-cycle reduction:               1.7%
Modeled counter-stall side effect:            82,500 cycles
```

Interpretation:

- SmartNIC-side batching is the more defensible version of asynchronous
  dispatch, because GPU execution should remain batched.
- Naive token-by-token async dispatch is not the target design.
- Expert counters and reorder help average/P50 latency but can intentionally
  delay some tokens.
- Credit-based dispatching is implemented as a primitive, but the current trace
  does not stress it enough to show direct benefit.

## Figures

Committed figure outputs:

```text
results/optimization_sweep_run001/figures/
```

Most important figure:

```text
07_basic_moe_comparison.svg
```

Plotting script:

```text
scripts/plot_optimization_sweep.py
```

The raw sweep CSV/summary files remain local experiment outputs unless
explicitly committed later.

## SmartNIC Design Document

The SmartNIC primitive design is captured in:

```text
SMARTNIC_DESIGN.md
```

It defines the proposed NIC-side kernels:

- GPU Handoff RX;
- Metadata Parser;
- Destination Queue Manager;
- Aggregation;
- Expert Counter Admission;
- Blocked-Token Reorder;
- Credit Manager;
- Scheduler / Arbiter;
- Packet Builder / TX;
- Completion / Return Event;
- Batch Overlap Controller;
- Metrics / Debug.

It also separates:

```text
V0: SmartNIC dispatch primitive
V1: batch overlap extension
```

## Batch Overlap Notes

Current profiling suggests there is room to hide communication/handoff work,
but the present simulator does not yet model this explicitly.

Rough advanced-profiling estimate for steady decode:

```text
MoE span per decode batch:             about 68 ms
expert compute critical time:          about 10-11 ms
prepare/finalize visible control time: about 6 ms
inter-MoE-layer slack:                 about 45 ms
batch-to-batch gap:                    about 4-5 ms
```

This slack should not be called pure GPU idle time. It is better described as:

```text
inter-MoE-layer / non-MoE window
```

Future overlap model should add:

```text
gpu_ready_cycle
nic_visible_cycle
gpu_to_nic_handoff_latency
max_inflight_batches
hidden_handoff_cycles
exposed_handoff_cycles
credit pressure under overlap
expert-counter pressure under overlap
```

## Local Workspace Notes

Large redundant Mixtral original checkpoint shards were removed:

```text
models/Mixtral-8x7B-Instruct-v0.1/consolidated.*.pt
```

The HuggingFace/vLLM safetensors shards remain:

```text
model-00001-of-00019.safetensors
...
model-00019-of-00019.safetensors
model.safetensors.index.json
```

This freed roughly 91 GB. Current scripts use HuggingFace/vLLM
`from_pretrained` or vLLM `load_format=auto`, and logs show safetensors loading.

## Next Focus: SmartNIC Primitive

Immediate next steps:

1. Refine the SmartNIC primitive interface:
   - token descriptor format;
   - queue metadata;
   - credit/counter update messages;
   - packet metadata format.

2. Decide counter semantics:
   - token count;
   - byte count;
   - estimated expert service time;
   - per-destination vs per-destination/per-expert.

3. Define receiver-side contract:
   - how received packets form expert microbatches;
   - when credits return;
   - when expert counters return;
   - what completion event the NIC observes.

4. Add V1 overlap model:
   - handoff latency;
   - overlap depth;
   - exposed vs hidden latency;
   - credit pressure under multiple batches in flight.

5. Re-run evaluation with staged claims:
   - Stage 1: basic synchronous MoE vs SmartNIC-side batching/full SmartNIC;
   - Stage 2: overlap-depth and handoff-latency sweep;
   - Stage 3: sensitivity to inter-node bandwidth and packet overhead.

## Do Not Forget

- Do not claim real hardware speedup yet.
- Current results are simulator-level scheduling benefits.
- Be explicit that GPU-to-SmartNIC handoff is not separately modeled in V0.
- The strongest current claim is:

```text
SmartNIC-side batching and admission control can reduce synchronous MoE
dispatch overhead in a deterministic scheduling model.
```

- The next claim to validate is:

```text
Credit/counter-controlled batch overlap can hide GPU-to-SmartNIC handoff and
network dispatch without flooding hot experts or destination queues.
```
