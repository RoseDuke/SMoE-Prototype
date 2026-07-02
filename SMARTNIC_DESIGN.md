# SmartNIC Design README

This document describes the proposed SmartNIC prototype for MoE token dispatch.
Here, "kernel" means an FPGA/HLS or NIC-side processing block, not an OS kernel.

## Goal

The SmartNIC sits between GPU-produced MoE routing metadata and remote expert
receivers. Its job is not to run expert compute. Its job is to make MoE dispatch
less synchronous and less bursty:

```text
GPU router output
-> SmartNIC token queues
-> batching / admission control / scheduling
-> packetized dispatch to destination GPU/expert
```

The current software simulator models the scheduling-level behavior of this
design. It does not yet model PCIe/NVLink/NIC DMA timing or real inter-node
network protocol timing.

## Prototype Stages

### V0: SmartNIC Dispatch Primitive

Implemented in the current C++ reference model:

- basic synchronous MoE baseline;
- SmartNIC-visible token queues;
- per-destination batching / aggregation;
- expert-aware counter admission;
- blocked-token reorder;
- credit primitive;
- deterministic packet scheduling.

This stage evaluates how much benefit comes from replacing a synchronous MoE
dispatch barrier with SmartNIC-side queued and batched dispatch.

### V1: Batch Overlap Extension

Next stage:

- explicit GPU-to-SmartNIC handoff latency;
- multiple batches in flight;
- hidden vs exposed communication latency;
- overlap-depth sweep;
- credit/counter pressure under overlap.

This stage evaluates whether SmartNIC dispatch work can be hidden under
non-MoE windows or inter-layer slack.

## Data Model

Each routed token or token chunk carries:

```text
token_id
batch_id / iteration_id
layer_id
src_rank
dst_rank
expert_id
payload_bytes
ready_cycle
```

For V1 overlap, the model should split `ready_cycle` into:

```text
gpu_ready_cycle
nic_visible_cycle
```

where:

```text
nic_visible_cycle = gpu_ready_cycle + exposed_gpu_to_nic_handoff
```

If handoff is fully hidden, exposed handoff can be zero. If not, it becomes part
of the critical path.

## SmartNIC Kernels

### 1. GPU Handoff RX Kernel

Receives token descriptors and payload pointers from GPU memory.

Supported functions:

- consume GPU-produced routing metadata;
- validate destination rank and payload size;
- optionally DMA token payload or payload chunk descriptors;
- timestamp `nic_visible_cycle`;
- push descriptors into the metadata parser.

Current simulator status:

- not separately modeled;
- folded into `arrival_cycle`.

V1 additions:

- fixed handoff latency;
- bandwidth-based handoff latency;
- maximum in-flight handoff requests;
- overlap with non-MoE compute windows.

### 2. Metadata Parser Kernel

Normalizes token routing metadata into the SmartNIC internal format.

Supported functions:

- extract `batch_id`, `layer_id`, `dst_rank`, `expert_id`;
- compute queue index;
- attach sequence number for deterministic ordering;
- reject malformed descriptors;
- forward valid token descriptors to queue manager.

Current simulator status:

- modeled by CSV trace parsing and token arrival events.

### 3. Destination Queue Manager Kernel

Maintains per-destination token queues.

Supported functions:

- enqueue tokens by destination rank;
- track queue depth;
- expose queue state to scheduler;
- preserve deterministic ordering within each destination;
- support skipped blocked tokens when reorder is enabled.

Current simulator status:

- implemented as per-destination queues.

### 4. Aggregation Kernel

Coalesces tokens targeting the same destination into larger packets.

Supported functions:

- aggregate up to `aggregation_threshold` tokens;
- flush partial packets after `aggregation_timeout_cycles`;
- drain remaining partial packets at the end of a run;
- report actual aggregation size;
- reduce packet fixed overhead.

Current simulator status:

- implemented.

Design intent:

```text
communication can be asynchronous,
but GPU-side execution should still receive batched work.
```

This avoids a naive token-by-token execution model that would hurt fused MoE
kernel efficiency.

### 5. Expert Counter Admission Kernel

Tracks per-destination/per-expert in-flight pressure.

Supported functions:

- maintain `expert_tokens_in_flight[dst_rank][expert_id]`;
- block tokens when an expert reaches `expert_counter_limit`;
- release counters after expert/receiver completion delay;
- expose blocked status to scheduler and metrics.

Current simulator status:

- implemented.

Design intent:

- prevent hot experts from accumulating unbounded work;
- reduce straggler pressure;
- enable safer batch overlap in V1.

### 6. Blocked-Token Reorder Kernel

Allows the SmartNIC to skip a blocked hot-expert token and send another eligible
token for the same destination.

Supported functions:

- scan destination queue for first dispatchable token;
- preserve deterministic behavior;
- avoid head-of-line blocking from saturated experts;
- cooperate with aggregation by filling packets only with eligible tokens.

Current simulator status:

- implemented.

Tradeoff:

- improves throughput and median latency;
- can increase latency for tokens intentionally held behind a counter gate.

### 7. Credit Manager Kernel

Tracks destination-level receiver capacity.

Supported functions:

- consume one credit per dispatched packet;
- block destination dispatch when credits are exhausted;
- return credits after receiver processing delay;
- expose credit stalls to scheduler and metrics.

Current simulator status:

- primitive implemented;
- current run did not trigger credit stalls.

Design intent for V1:

- bound receiver-side queue growth under batch overlap;
- let multiple batches be in flight without flooding a destination;
- make overlap controlled rather than unbounded.

### 8. Scheduler / Arbiter Kernel

Selects which destination sends the next packet.

Supported policies:

- round-robin;
- oldest-first;
- largest-queue;
- credit-aware.

Supported functions:

- ignore empty queues;
- ignore credit-blocked destinations;
- ignore destinations with no dispatchable expert;
- select one destination for the shared transmit path;
- preserve deterministic tie-breaking.

Current simulator status:

- implemented.

### 9. Packet Builder / TX Kernel

Builds the outgoing SmartNIC packet and accounts for transmit time.

Supported functions:

- combine metadata and payload descriptors;
- compute packet service cycles;
- account for fixed packet overhead;
- account for payload transfer time;
- emit completion events.

Current simulator status:

- modeled by:

```text
packet_fixed_overhead_cycles + ceil(payload_bytes / link_bytes_per_cycle)
```

Future hardware model additions:

- real PCIe/NVLink bandwidth;
- RDMA or Ethernet protocol overhead;
- switch contention;
- per-destination virtual channels;
- multi-link transmit arbitration.

### 10. Completion / Return Event Kernel

Processes receiver-side completion events.

Supported functions:

- mark packet transmission complete;
- return destination credit;
- return expert counter slots;
- update per-destination counters.

Current simulator status:

- implemented as `TransmissionComplete`, `CreditReturn`, and
  `ExpertCounterReturn` events.

### 11. Batch Overlap Controller

Controls how many batches can be in flight and how much handoff work can be
hidden.

Supported functions:

- track active batch IDs;
- enforce `max_inflight_batches`;
- estimate hidden vs exposed GPU-to-SmartNIC handoff latency;
- admit next batch when credits and expert counters allow;
- expose overlap metrics:
  - hidden handoff cycles;
  - exposed handoff cycles;
  - active batch depth;
  - receiver pressure;
  - credit stalls under overlap.

Current simulator status:

- not implemented.

Design intent:

```text
batch overlap should not mean unlimited early sending.
It should mean controlled early handoff plus NIC-side batching and admission.
```

### 12. Metrics / Debug Kernel

Collects observability counters.

Supported functions:

- total tokens;
- total packets;
- average and tail latency;
- queue depth;
- packet aggregation size;
- credit stall cycles;
- counter stall cycles;
- per-destination load;
- per-expert pressure;
- V1 overlap-specific hidden/exposed latency.

Current simulator status:

- V0 metrics implemented;
- V1 overlap metrics pending.

## End-to-End Flow

```text
GPU router / top-k
  |
  v
GPU Handoff RX Kernel
  |
  v
Metadata Parser Kernel
  |
  v
Destination Queue Manager
  |
  +--> Expert Counter Admission
  |
  +--> Credit Manager
  |
  v
Aggregation Kernel
  |
  v
Scheduler / Arbiter
  |
  v
Packet Builder / TX Kernel
  |
  v
Remote receiver / expert input buffer
  |
  v
Completion / credit / counter return
```

## GPU-Side Kernels Assumed by the Design

The SmartNIC design assumes the GPU still runs batched MoE compute. It does not
replace the fused MoE kernels.

### Router / Top-K Kernel

Supported functions:

- compute token-to-expert routing;
- produce expert IDs and destination ranks;
- generate token descriptors for SmartNIC handoff.

### Pack / Unpack Kernel

Supported functions:

- materialize token payloads or payload pointers;
- consume received expert input batches;
- restore output token order after combine.

### Expert Compute Kernel

Supported functions:

- run batched expert GEMM / fused MoE compute;
- process microbatches formed by receiver-side queues.

### Combine Kernel

Supported functions:

- combine expert outputs;
- return results to the transformer layer.

## Supported Optimizations

### SmartNIC-Side Batching

Implemented in V0.

Purpose:

- reduce packet fixed overhead;
- preserve GPU batch execution;
- avoid naive token-by-token dispatch.

### Expert-Aware Admission

Implemented in V0.

Purpose:

- avoid overloading a hot expert;
- reduce straggler amplification;
- support controlled overlap in V1.

### Credit-Based Dispatching

Primitive implemented in V0.

Purpose:

- represent receiver capacity;
- prevent unbounded queue growth;
- become more important when multiple batches are in flight.

### Batch Overlap

Planned for V1.

Purpose:

- hide GPU-to-SmartNIC handoff and dispatch work under non-MoE windows;
- overlap next-batch preparation with current-batch compute;
- evaluate exposed communication latency under realistic handoff/network costs.

## Current Assumptions

- `arrival_cycle` means the token is already visible to the SmartNIC.
- GPU-to-SmartNIC handoff time is not separately modeled.
- Inter-node network time is approximated by a logical packet service model.
- GPU expert compute is not cycle-accurately modeled.
- The current simulator is a scheduling-level model, not a full hardware timing
  simulator.

## Next Design Questions

- What is the measured GPU-to-SmartNIC handoff latency?
- How much of that handoff can overlap with non-MoE kernels?
- What is a realistic `max_inflight_batches` value?
- How large should destination credits be?
- Should expert counters count tokens, bytes, or estimated expert service time?
- Should packet aggregation be per destination only, or per destination/expert?
- How should receiver-side microbatch formation interact with fused MoE kernels?
