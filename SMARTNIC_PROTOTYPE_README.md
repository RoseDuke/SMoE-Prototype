# SmartNIC Prototype Implementation README

This document is the implementation checklist for the first SmartNIC prototype.
The intended workflow is:

```text
CloudLab oct-build server: build HLS / xclbin artifacts
U280 node: run functional and timing validation
```

The prototype should start as a trace-driven SmartNIC dispatch engine that
matches the C++ simulator, then evolve toward a GPU-connected primitive. The
first goal is not to run a full Mixtral model on FPGA. The first goal is to
prove that a SmartNIC can implement the MoE dispatch optimizations:

- asynchronous SmartNIC-side token admission;
- destination-aware batching;
- expert-aware admission counters;
- blocked-token reorder;
- credit-based receiver backpressure;
- later, credit-controlled batch overlap.

## Baseline System Before Optimization

The baseline MoE path is logically:

```text
GPU router / top-k
  -> synchronous dispatch or all-to-all-style exchange
  -> receiver-side expert input formation
  -> batched expert compute on GPU
  -> combine / finalize
```

The important limitations are:

- dispatch is exposed as a batch/layer-level synchronization point;
- the sender does not know receiver-side expert pressure;
- hot experts can create stragglers after traffic has already arrived;
- small routed token movements can pay repeated fixed communication overhead;
- consecutive decode iterations are mostly serialized;
- there is no explicit destination credit model for safe overlap.

The SmartNIC prototype should not replace GPU expert kernels. It should reshape
the dispatch path before tokens reach receiver-side batched expert execution.

## Prototype Target

### V0 Target

V0 is a hardware version of the current scheduling-level simulator.

Required behavior:

```text
trace / GPU-produced token descriptors
  -> SmartNIC descriptor ingress
  -> per-destination queues
  -> batching, counter checks, reorder, credit checks
  -> packet scheduler
  -> packet output stream
  -> completion path that returns credits and expert counters
```

V0 can be trace-driven. A host program may feed token descriptors from CSV or
binary files into the FPGA through PCIe/XRT. The receiver may also be emulated
by a completion generator. This is acceptable for the first U280 validation
because it isolates the SmartNIC scheduling primitive.

### V1 Target

V1 adds explicit batch overlap:

```text
batch i expert compute / non-MoE work
    overlaps with
batch i+1 GPU-to-SmartNIC handoff and NIC-side scheduling
```

V1 requires additional fields and counters:

- `gpu_ready_cycle`;
- `nic_visible_cycle`;
- `batch_id`;
- active in-flight batch depth;
- hidden handoff cycles;
- exposed handoff cycles;
- per-batch completion time.

## Proposed Repository Layout

Use this layout when adding the FPGA implementation:

```text
fpga/
  include/
    smartnic_abi.hpp
    smartnic_config.hpp
  hls/
    descriptor_ingress.cpp
    metadata_parser.cpp
    queue_manager.cpp
    aggregation.cpp
    expert_counter.cpp
    reorder.cpp
    credit_manager.cpp
    scheduler.cpp
    packet_tx.cpp
    completion.cpp
    metrics.cpp
    top.cpp
  host/
    run_trace.cpp
    load_trace.cpp
    verify_against_sim.cpp
  scripts/
    build_hw_emu.sh
    build_u280.sh
    run_u280_trace.sh
  tests/
    tiny_uniform.bin
    tiny_skewed.bin
    hot_expert.bin
```

Keep the existing C++ simulator as the golden model. The FPGA output should be
compared against simulator output for the same trace and config.

## Token Descriptor ABI

The first prototype should use fixed-width descriptors. Do not start with a
variable-length metadata format.

Required fields:

```text
uint64_t token_id
uint32_t batch_id
uint16_t layer_id
uint16_t src_rank
uint16_t dst_rank
uint16_t expert_id
uint32_t payload_bytes
uint64_t arrival_cycle
uint64_t gpu_ready_cycle      // V1, can equal arrival_cycle in V0
uint64_t nic_visible_cycle    // V1, can equal arrival_cycle in V0
```

For V0, `arrival_cycle` means the descriptor is already visible to the
SmartNIC. For V1, split it into GPU-ready time and NIC-visible time so the
prototype can evaluate handoff overlap.

Minimum correctness requirements:

- `token_id` is globally unique within a run;
- `dst_rank < num_destinations`;
- `payload_bytes > 0`;
- `arrival_cycle` is non-decreasing in trace-driven input;
- `batch_id` and `layer_id` are preserved in output metadata;
- output can be reordered internally, but `token_id` must allow exact combine.

## Packet Output ABI

The packet output does not need to be a real Ethernet/RDMA packet in V0. It
should be a hardware-visible packet descriptor:

```text
uint64_t packet_id
uint16_t dst_rank
uint16_t aggregation_size
uint32_t total_payload_bytes
uint64_t first_token_id
uint64_t dispatch_cycle
uint64_t completion_cycle
token_id list or pointer to token list
```

For hardware simplicity, start with a fixed maximum aggregation size:

```text
MAX_AGG_TOKENS = 16 or 32
```

The host verifier can expand each packet back into per-token records.

## Control Registers

Expose a small AXI-Lite control plane. Suggested registers:

```text
0x000 control
      bit 0: start
      bit 1: reset
      bit 2: enable_async_sending
      bit 3: enable_aggregation
      bit 4: enable_expert_counters
      bit 5: enable_blocked_token_reorder
      bit 6: enable_credit_control

0x008 status
      bit 0: idle
      bit 1: running
      bit 2: done
      bit 3: error

0x010 num_destinations
0x018 num_experts_per_destination
0x020 aggregation_threshold
0x028 aggregation_timeout_cycles
0x030 initial_credits_per_destination
0x038 expert_counter_limit
0x040 packet_fixed_overhead_cycles
0x048 link_bytes_per_cycle
0x050 receiver_processing_cycles
0x058 expert_counter_return_cycles
0x060 scheduling_policy
0x068 max_inflight_batches          // V1
0x070 handoff_latency_cycles        // V1
```

Keep all config fields runtime-programmable. Do not bake policy constants into
the bitstream unless timing forces it.

## Required HLS Blocks

### 1. Descriptor Ingress

Purpose:

- receive token descriptors from host memory or a future GPU handoff path;
- apply `arrival_cycle` ordering in trace mode;
- forward descriptors to metadata parsing.

Inputs:

- descriptor stream from PCIe/XRT host buffer;
- control registers.

Outputs:

- normalized descriptor stream.

V0 implementation:

- host feeds descriptors from a binary trace;
- ingress preserves order and timestamps;
- no real GPU DMA requirement.

Future GPU-connected implementation:

- replace host feeder with GPU-produced descriptor ring;
- add DMA read or peer-to-peer handoff support;
- explicitly measure GPU-to-NIC handoff latency.

### 2. Metadata Parser

Purpose:

- validate descriptor fields;
- compute queue indices;
- attach deterministic sequence numbers.

State:

- `next_sequence_number`;
- malformed descriptor counter.

Output:

- valid descriptors to the destination queue manager;
- error counter for invalid descriptors.

### 3. Destination Queue Manager

Purpose:

- maintain one logical queue per destination rank;
- expose queue depth and front-token metadata;
- support bounded lookahead for reorder.

Required state:

```text
queue[dst_rank]
queue_depth[dst_rank]
oldest_arrival_cycle[dst_rank]
```

Implementation notes:

- use BRAM/URAM-backed circular buffers;
- set a fixed maximum queue depth for V0;
- expose overflow as a hard error counter.

Correctness:

- if reorder is disabled, queue behavior must be FIFO;
- if reorder is enabled, token order may change only within the destination
  queue and only when the skipped token is blocked by counter admission.

### 4. Aggregation Block

Purpose:

- coalesce multiple tokens for the same destination into one packet;
- reduce fixed packet overhead;
- preserve receiver-side batched execution.

Inputs:

- eligible descriptors from queue manager;
- aggregation config.

Outputs:

- packet candidate with up to `aggregation_threshold` tokens.

Flush rules:

- dispatch when `aggregation_threshold` is reached;
- dispatch partial packet when `aggregation_timeout_cycles` expires;
- drain all partial packets at end-of-run.

Metrics:

- total packets;
- total tokens aggregated;
- histogram of aggregation sizes;
- timeout flush count.

Important constraint:

- this optimization is not token-by-token execution. The SmartNIC may admit
  tokens asynchronously, but it must still build batched packet units.

### 5. Expert Counter Admission

Purpose:

- avoid overloading a hot expert at a destination;
- bound in-flight expert pressure before receiver-side stragglers form.

State:

```text
expert_inflight[dst_rank][expert_id]
```

Dispatch rule:

```text
expert_inflight[dst][expert] + packet_tokens_for_expert < expert_counter_limit
```

Completion rule:

- return counter slots after `expert_counter_return_cycles`, or after a
  receiver completion event in future versions.

Metrics:

- counter stall cycles;
- per-expert blocked tokens;
- max expert in-flight count.

Side effect to preserve:

- hot-expert tokens may become slower. This is expected and should be reported,
  not hidden.

### 6. Blocked-Token Reorder

Purpose:

- prevent head-of-line blocking introduced by expert counters.

Behavior:

```text
if front token is expert-blocked:
    scan up to reorder_window later tokens
    select first counter-eligible token
else:
    select front token
```

Required config:

```text
reorder_window
max_token_stall_cycles
```

V0 simplification:

- `reorder_window` may equal the queue depth if resources allow;
- otherwise use a small fixed window such as 8 or 16 descriptors.

Fairness:

- add aging so a blocked token cannot be bypassed forever;
- record max per-token stall.

### 7. Credit Manager

Purpose:

- represent destination-level receiver capacity;
- prevent early traffic from flooding a busy receiver;
- make future batch overlap controlled.

State:

```text
credits[dst_rank]
```

Dispatch rule:

```text
dispatch_allowed(dst) = queue_nonempty(dst)
                     && credits[dst] > 0
                     && exists expert-eligible token
```

Completion rule:

- packet dispatch consumes one destination credit;
- receiver completion returns one destination credit.

Metrics:

- credit stall cycles;
- per-destination credit minimum;
- credit return count.

Expected V0 behavior:

- single-batch traces may not trigger credit stalls.

Expected V1 behavior:

- credits become important when multiple batches overlap.

### 8. Scheduler / Arbiter

Purpose:

- choose one destination packet candidate for the transmit path.

Supported policies:

```text
0: round_robin
1: oldest_first
2: largest_queue
3: credit_aware
```

Eligibility:

- queue is non-empty;
- destination has credit if credit control is enabled;
- at least one token passes expert admission;
- aggregation block can form a packet or timeout requires flush.

Correctness:

- tie-breaking must be deterministic;
- the same trace and config must produce the same output every run.

### 9. Packet Builder / TX Model

Purpose:

- convert selected tokens into a packet descriptor;
- model service time;
- emit completion events.

Service-time model:

```text
packet_cycles =
    packet_fixed_overhead_cycles
  + ceil(total_payload_bytes / link_bytes_per_cycle)
```

V0 output:

- packet descriptors to host memory;
- per-token completion metadata for verification.

Future U280 network extension:

- replace logical TX with CMAC/RDMA/Ethernet path if available;
- keep packet builder interface unchanged so scheduling logic is reusable.

### 10. Completion Path

Purpose:

- return credits and expert counter capacity;
- mark token completion time;
- update metrics.

Inputs:

- TX completion events;
- optional receiver completion stream.

Outputs:

- credit return events;
- expert counter return events;
- per-token completion records.

V0 simplification:

- completion time can be generated from packet service time and fixed receiver
  processing delay.

### 11. Metrics Block

Required counters:

```text
total_tokens
total_packets
final_cycle
max_queue_depth
aggregation_size_sum
aggregation_timeout_flushes
credit_stall_cycles
counter_stall_cycles
reorder_events
queue_overflow_errors
malformed_descriptor_errors
per_destination_packets
per_destination_tokens
per_destination_max_queue_depth
```

V1 counters:

```text
active_batch_depth
max_active_batch_depth
hidden_handoff_cycles
exposed_handoff_cycles
handoff_blocked_by_credit_cycles
handoff_blocked_by_counter_cycles
```

The host program should dump these counters to a summary file that mirrors the
current simulator summaries.

## Optimization Behavior to Match

### Synchronous Baseline Mode

Config:

```text
enable_async_sending = 0
enable_aggregation = 0
enable_expert_counters = 0
enable_blocked_token_reorder = 0
enable_credit_control = 0
```

Behavior:

- descriptors for the same `(batch_id, layer_id)` group are not enqueued until
  the whole group is ready;
- this models the baseline synchronous MoE dispatch barrier.

### Async Admission Only

Config:

```text
enable_async_sending = 1
enable_aggregation = 0
enable_expert_counters = 0
enable_blocked_token_reorder = 0
enable_credit_control = 0
```

Behavior:

- each token may enter the SmartNIC queue at its own arrival time;
- packetization remains minimal;
- this mode exposes the risk of naive asynchronous dispatch.

### SmartNIC-Side Batching

Config:

```text
enable_async_sending = 1
enable_aggregation = 1
```

Behavior:

- tokens are admitted asynchronously;
- tokens to the same destination are coalesced into packets;
- receiver still sees batched work units.

### Expert Counter + Reorder

Config:

```text
enable_expert_counters = 1
enable_blocked_token_reorder = 1
```

Behavior:

- hot experts are bounded by `expert_counter_limit`;
- if a hot-expert token blocks the queue front, the reorder block may dispatch
  a later eligible token;
- hot-expert tokens may have higher latency, but non-hot tokens should avoid
  unnecessary head-of-line blocking.

### Credit-Based Dispatch

Config:

```text
enable_credit_control = 1
initial_credits_per_destination > 0
```

Behavior:

- destination dispatch consumes credits;
- completion returns credits;
- credits may not help much in single-batch V0 traces;
- credits are required for safe multi-batch overlap.

## Build Flow on CloudLab oct-build

The exact platform name depends on the installed Xilinx shell. Keep the scripts
parameterized:

```bash
export PLATFORM=<u280_platform_name>
export TARGET=hw_emu   # sw_emu, hw_emu, or hw
```

Suggested flow:

```bash
cd SMoE-Prototype/fpga
bash scripts/build_hw_emu.sh
bash scripts/build_u280.sh
```

The build scripts should generate:

```text
build/smartnic_moe_dispatch.xo
build/smartnic_moe_dispatch.xclbin
build/run_trace_host
```

Minimum build gates before using a U280 node:

- HLS C simulation passes on tiny traces;
- hardware emulation passes at least one skewed trace;
- host verifier matches the C++ simulator for packet count and per-token
  completion order;
- all runtime config registers are readable and writable;
- metrics counters are nonzero on a nontrivial trace.

## Validation Flow on U280 Node

Before running:

```bash
xbutil examine
```

Then run a smoke trace:

```bash
cd SMoE-Prototype/fpga
bash scripts/run_u280_trace.sh \
  --xclbin build/smartnic_moe_dispatch.xclbin \
  --trace tests/tiny_uniform.bin \
  --config ../configs/full_smartnic.cfg \
  --out results/u280_tiny_uniform
```

Then run stress traces:

```text
tiny_uniform
tiny_skewed
hot_expert
credit_pressure
aggregation_timeout
```

Required U280 validation outputs:

```text
packets.csv
tokens.csv
summary.txt
metrics.json
```

Compare against the simulator:

```bash
./build/smartnic_ref --trace <same_trace.csv> --config <same_config> ...
fpga/host/verify_against_sim \
  --sim results/sim/tokens.csv \
  --hw results/u280/tokens.csv
```

For V0, the hardware result should match the simulator for:

- total token count;
- total packet count;
- per-token destination and expert;
- aggregation size;
- credit stall flag;
- counter stall flag;
- deterministic dispatch order under the same policy.

Cycle counts may differ once real hardware timing is used. If so, report both:

```text
logical simulator cycles
measured FPGA cycles
```

## Milestones

### M0: Binary Trace and Host Feeder

Deliverables:

- binary descriptor format;
- CSV-to-binary converter;
- host loader;
- host-side verifier.

Pass condition:

- host-only replay matches existing simulator traces.

### M1: HLS Functional Pipeline

Deliverables:

- descriptor ingress;
- metadata parser;
- queue manager;
- scheduler;
- packet builder;
- metrics.

Pass condition:

- HLS C simulation matches synchronous baseline and async-only modes.

### M2: Batching and Counter Admission

Deliverables:

- aggregation block;
- expert counter block;
- blocked-token reorder.

Pass condition:

- matches simulator for `async_aggregation`, `async_expert_counter`, and
  `full_smartnic` configs on tiny and skewed traces.

### M3: Credit Manager

Deliverables:

- destination credit state;
- credit return path;
- credit stall metrics.

Pass condition:

- synthetic credit-pressure trace triggers credit stalls and recovers.

### M4: U280 Bring-Up

Deliverables:

- compiled U280 xclbin;
- XRT host program;
- U280 smoke test results.

Pass condition:

- tiny traces run on U280 and produce metrics matching simulator semantics.

### M5: V1 Batch Overlap

Deliverables:

- `gpu_ready_cycle` and `nic_visible_cycle` support;
- `max_inflight_batches`;
- overlap metrics;
- handoff latency sweep.

Pass condition:

- reports hidden and exposed handoff cycles under configurable overlap windows.

## Design Choices to Keep Explicit

Do not hide these assumptions:

- V0 is trace-driven, not a real GPU-attached SmartNIC.
- The FPGA does not execute expert compute.
- Receiver processing is initially modeled by completion events.
- Packet service time is a configurable model before real networking is added.
- Credit stalls are not automatically bad; they are controlled backpressure.
- Expert-counter stalls are not automatically bad; they expose hot-expert
  admission control.
- Asynchronous token admission must be paired with batching to avoid tiny packet
  and tiny kernel inefficiency.

## Open Engineering Questions

These should be resolved during prototype bring-up:

- What queue depth fits comfortably in BRAM/URAM for U280?
- Should aggregation be destination-only or destination-plus-expert?
- Should expert counters count tokens, bytes, or estimated service time?
- What reorder window is affordable at target frequency?
- What is the measured descriptor ingress bandwidth through XRT/PCIe?
- Can the eventual GPU-to-FPGA handoff use peer-to-peer DMA on the target
  platform?
- How should credit return be tied to real receiver completion in a
  multi-node setup?
- What is the minimum metadata needed for combine-order correctness?

## First Implementation Recommendation

Start with this exact subset:

```text
descriptor_ingress
metadata_parser
destination_queue_manager
aggregation
scheduler
packet_tx_model
completion
metrics
```

Then add:

```text
expert_counter
blocked_token_reorder
credit_manager
```

Finally add:

```text
batch_overlap_controller
real GPU/FPGANIC handoff timing
real network transmit path
```

This sequence gives a working U280 prototype early while keeping every new
optimization measurable against the existing simulator.
