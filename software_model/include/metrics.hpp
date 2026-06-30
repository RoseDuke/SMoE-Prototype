#pragma once

#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

#include "types.hpp"

struct SummaryMetrics {
    std::uint64_t total_tokens = 0;
    std::uint64_t total_packets = 0;
    std::uint64_t final_cycle = 0;
    double throughput_tokens_per_cycle = 0.0;
    double average_latency_cycles = 0.0;
    std::uint64_t p50_latency_cycles = 0;
    std::uint64_t p95_latency_cycles = 0;
    std::uint64_t p99_latency_cycles = 0;
    double average_queue_delay_cycles = 0.0;
    std::uint32_t maximum_queue_depth = 0;
    std::uint64_t total_credit_stall_cycles = 0;
};

SummaryMetrics compute_summary_metrics(
    const std::vector<DispatchRecord>& records,
    const std::vector<DestinationState>& destinations,
    std::uint64_t final_cycle);

void write_records_csv(
    const std::string& path,
    const std::vector<DispatchRecord>& records);

void write_summary_file(
    const std::string& path,
    const SummaryMetrics& metrics,
    const std::vector<DestinationState>& destinations);
