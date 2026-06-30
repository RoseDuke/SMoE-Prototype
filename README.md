# Nexus-MoE-SmartNIC-Prototype

This repository contains a deterministic C++17 software reference model for
Nexus-MoE SmartNIC token dispatch. It is intended as a correctness oracle before
building FPGA/HLS implementations.

## Build Instructions

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

The build creates:

```text
build/smartnic_ref
build/test_reference_model
```

## Run Instructions

```bash
./build/smartnic_ref \
    --trace tests/traces/tiny_uniform.csv \
    --config configs/baseline.cfg \
    --output results/uniform.csv \
    --summary results/uniform_summary.txt
```

Or run the full reference script:

```bash
bash scripts/run_reference.sh
```

## Configuration Documentation

Configuration files use `key=value` lines. Supported keys:

```text
num_destinations
initial_credits_per_destination
aggregation_threshold
aggregation_timeout_cycles
link_bytes_per_cycle
packet_fixed_overhead_cycles
receiver_processing_cycles
scheduling_policy
enable_credit_control
enable_aggregation
```

Supported scheduling policies are `round_robin`, `oldest_first`,
`largest_queue`, and `credit_aware`.

## Trace Format Documentation

Input traces are CSV files with this exact header:

```csv
arrival_cycle,token_id,batch_id,layer_id,src_rank,dst_rank,expert_id,payload_bytes
```

The parser validates the header, field count, integer parsing, unique
`token_id`, non-decreasing `arrival_cycle`, destination range, and positive
payload size.

## Output Format Documentation

Per-token output is sorted by `token_id` and uses:

```csv
token_id,dst_rank,arrival_cycle,enqueue_cycle,dispatch_cycle,completion_cycle,queue_delay,total_latency,queue_depth_at_enqueue,aggregation_size,credit_stalled
```

The summary file reports total tokens, total packets, final cycle, throughput,
average latency, p50/p95/p99 latency, queue delay, maximum queue depth, total
credit stall cycles, and per-destination counters.

## Architecture Overview

The model is a discrete-event simulator. CSV rows become token arrival events.
Tokens enter per-destination FIFO queues. A scheduler selects one eligible
destination for the single shared transmit link. Optional credit-based flow
control consumes one credit per packet and returns it after fixed receiver
processing delay. Optional aggregation can send multiple queued tokens to the
same destination in one packet, using threshold, timeout, and final drain logic.

The simulator uses a deterministic priority queue ordered by cycle, event type,
and sequence number. It does not use a large per-cycle simulation loop.

## Known Limitations

No real PCIe model.
No CMAC or UDP model.
No packet loss.
No network switch model.
Single shared transmit link.
Receiver processing modeled as fixed delay.
No GPU execution model.
No FPGA timing model.
