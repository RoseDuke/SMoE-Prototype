#include "metrics.hpp"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <stdexcept>

namespace {

std::uint64_t nearest_rank_percentile(std::vector<std::uint64_t> values, double percentile) {
    if (values.empty()) {
        return 0;
    }
    std::sort(values.begin(), values.end());
    const double rank = percentile * static_cast<double>(values.size());
    std::size_t index = static_cast<std::size_t>(rank);
    if (rank > static_cast<double>(index)) {
        ++index;
    }
    if (index == 0) {
        index = 1;
    }
    index = std::min(index, values.size());
    return values[index - 1];
}

} // namespace

SummaryMetrics compute_summary_metrics(
    const std::vector<DispatchRecord>& records,
    const std::vector<DestinationState>& destinations,
    std::uint64_t final_cycle) {
    SummaryMetrics metrics;
    metrics.total_tokens = records.size();
    metrics.final_cycle = final_cycle;
    metrics.throughput_tokens_per_cycle =
        final_cycle == 0 ? 0.0 : static_cast<double>(records.size()) / static_cast<double>(final_cycle);

    std::vector<std::uint64_t> latencies;
    latencies.reserve(records.size());
    std::uint64_t latency_sum = 0;
    std::uint64_t queue_delay_sum = 0;
    for (const DispatchRecord& record : records) {
        const std::uint64_t latency = record.completion_cycle - record.arrival_cycle;
        const std::uint64_t queue_delay = record.dispatch_cycle - record.arrival_cycle;
        latencies.push_back(latency);
        latency_sum += latency;
        queue_delay_sum += queue_delay;
    }

    if (!records.empty()) {
        metrics.average_latency_cycles =
            static_cast<double>(latency_sum) / static_cast<double>(records.size());
        metrics.average_queue_delay_cycles =
            static_cast<double>(queue_delay_sum) / static_cast<double>(records.size());
    }

    // Deterministic nearest-rank percentile: ceil(p * N), using 1-based rank.
    metrics.p50_latency_cycles = nearest_rank_percentile(latencies, 0.50);
    metrics.p95_latency_cycles = nearest_rank_percentile(latencies, 0.95);
    metrics.p99_latency_cycles = nearest_rank_percentile(latencies, 0.99);

    for (const DestinationState& destination : destinations) {
        metrics.total_packets += destination.total_packets_sent;
        metrics.maximum_queue_depth = std::max(metrics.maximum_queue_depth, destination.max_queue_depth);
        metrics.total_credit_stall_cycles += destination.credit_stall_cycles;
    }

    return metrics;
}

void write_records_csv(
    const std::string& path,
    const std::vector<DispatchRecord>& records) {
    std::vector<DispatchRecord> sorted = records;
    std::sort(sorted.begin(), sorted.end(), [](const DispatchRecord& lhs, const DispatchRecord& rhs) {
        return lhs.token_id < rhs.token_id;
    });

    std::ofstream output(path);
    if (!output) {
        throw std::runtime_error("Failed to open output CSV: " + path);
    }

    output << "token_id,dst_rank,arrival_cycle,enqueue_cycle,dispatch_cycle,completion_cycle,"
           << "queue_delay,total_latency,queue_depth_at_enqueue,aggregation_size,credit_stalled\n";
    for (const DispatchRecord& record : sorted) {
        output << record.token_id << ','
               << record.dst_rank << ','
               << record.arrival_cycle << ','
               << record.enqueue_cycle << ','
               << record.dispatch_cycle << ','
               << record.completion_cycle << ','
               << (record.dispatch_cycle - record.arrival_cycle) << ','
               << (record.completion_cycle - record.arrival_cycle) << ','
               << record.queue_depth_at_enqueue << ','
               << record.aggregation_size << ','
               << (record.credit_stalled ? "true" : "false") << '\n';
    }
}

void write_summary_file(
    const std::string& path,
    const SummaryMetrics& metrics,
    const std::vector<DestinationState>& destinations) {
    std::ofstream output(path);
    if (!output) {
        throw std::runtime_error("Failed to open summary file: " + path);
    }

    output << std::fixed << std::setprecision(6);
    output << "Total tokens: " << metrics.total_tokens << '\n';
    output << "Total packets: " << metrics.total_packets << '\n';
    output << "Final cycle: " << metrics.final_cycle << '\n';
    output << "Throughput tokens/cycle: " << metrics.throughput_tokens_per_cycle << '\n';
    output << "Average latency cycles: " << metrics.average_latency_cycles << '\n';
    output << "P50 latency cycles: " << metrics.p50_latency_cycles << '\n';
    output << "P95 latency cycles: " << metrics.p95_latency_cycles << '\n';
    output << "P99 latency cycles: " << metrics.p99_latency_cycles << '\n';
    output << "Average queue delay cycles: " << metrics.average_queue_delay_cycles << '\n';
    output << "Maximum queue depth: " << metrics.maximum_queue_depth << '\n';
    output << "Total credit stall cycles: " << metrics.total_credit_stall_cycles << '\n';
    output << '\n';

    for (std::size_t index = 0; index < destinations.size(); ++index) {
        const DestinationState& destination = destinations[index];
        output << "Destination " << index << ":\n";
        output << "  Tokens sent: " << destination.total_tokens_sent << '\n';
        output << "  Packets sent: " << destination.total_packets_sent << '\n';
        output << "  Maximum queue depth: " << destination.max_queue_depth << '\n';
        output << "  Credit stall cycles: " << destination.credit_stall_cycles << '\n';
    }
}
