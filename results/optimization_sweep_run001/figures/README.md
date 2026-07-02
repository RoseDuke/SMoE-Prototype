# SmartNIC Optimization Figures

Generated from the optimization sweep summaries and per-token CSV files.

## Main takeaways

- SmartNIC-side batching average latency reduction vs basic MoE baseline: 13.3%.
- Full SmartNIC average latency reduction vs basic MoE baseline: 17.1%.
- Full SmartNIC packet reduction vs basic MoE baseline: 74.9%.
- Full SmartNIC final-cycle reduction vs basic MoE baseline: 1.7%.
- Modeled counter-stall side effect in full SmartNIC: 82,500 cycles.

## Figures

- `01_average_latency.svg`: staged average-latency improvement.
- `02_tail_latency.svg`: P50/P95/P99 latency comparison.
- `03_packets_and_cycles.svg`: packet count and makespan comparison.
- `04_latency_cdf.svg`: per-token latency CDF.
- `05_destination_skew.svg`: destination skew and packet coalescing.
- `06_counter_stall.svg`: counter-stall side effect by destination.
- `07_basic_moe_comparison.svg`: basic synchronous MoE baseline vs SmartNIC optimizations.
