#include "load_trace.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>

namespace smartnic_fpga {
namespace {

constexpr const char* kExpectedHeader =
    "arrival_cycle,token_id,batch_id,layer_id,src_rank,dst_rank,expert_id,payload_bytes";

std::string trim(const std::string& value) {
    std::size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
        ++begin;
    }
    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }
    return value.substr(begin, end - begin);
}

std::uint64_t parse_u64(const std::string& key, const std::string& value) {
    std::size_t parsed = 0;
    unsigned long long result = 0;
    try {
        result = std::stoull(value, &parsed, 10);
    } catch (const std::exception&) {
        throw std::runtime_error("Invalid integer for '" + key + "': " + value);
    }
    if (parsed != value.size()) {
        throw std::runtime_error("Invalid trailing characters for '" + key + "': " + value);
    }
    return static_cast<std::uint64_t>(result);
}

std::uint32_t parse_u32(const std::string& key, const std::string& value) {
    const std::uint64_t parsed = parse_u64(key, value);
    if (parsed > UINT32_MAX) {
        throw std::runtime_error("Value for '" + key + "' exceeds uint32 range");
    }
    return static_cast<std::uint32_t>(parsed);
}

std::uint16_t parse_u16(const std::string& key, const std::string& value) {
    const std::uint64_t parsed = parse_u64(key, value);
    if (parsed > UINT16_MAX) {
        throw std::runtime_error("Value for '" + key + "' exceeds uint16 range");
    }
    return static_cast<std::uint16_t>(parsed);
}

std::uint32_t parse_bool(const std::string& key, const std::string& value) {
    if (value == "true" || value == "1") {
        return 1;
    }
    if (value == "false" || value == "0") {
        return 0;
    }
    throw std::runtime_error("Invalid boolean for '" + key + "': " + value);
}

std::uint32_t parse_policy(const std::string& value) {
    if (value == "round_robin") {
        return kRoundRobin;
    }
    if (value == "oldest_first") {
        return kOldestFirst;
    }
    if (value == "largest_queue") {
        return kLargestQueue;
    }
    if (value == "credit_aware") {
        return kCreditAware;
    }
    throw std::runtime_error("Unsupported scheduling_policy: " + value);
}

std::vector<std::string> split_csv_line(const std::string& line) {
    std::vector<std::string> fields;
    std::stringstream stream(line);
    std::string field;
    while (std::getline(stream, field, ',')) {
        fields.push_back(field);
    }
    if (!line.empty() && line.back() == ',') {
        fields.emplace_back();
    }
    return fields;
}

std::uint64_t percentile_nearest_rank(std::vector<std::uint64_t> values, double percentile) {
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
    if (index > values.size()) {
        index = values.size();
    }
    return values[index - 1U];
}

} // namespace

SmartnicRuntimeConfig read_runtime_config(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("Failed to open config file: " + path);
    }

    SmartnicRuntimeConfig config;
    std::unordered_set<std::string> seen_keys;
    std::string line;
    std::uint64_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        const std::size_t comment = line.find('#');
        if (comment != std::string::npos) {
            line = line.substr(0, comment);
        }
        line = trim(line);
        if (line.empty()) {
            continue;
        }

        const std::size_t equals = line.find('=');
        if (equals == std::string::npos) {
            throw std::runtime_error("Malformed config line " + std::to_string(line_number));
        }
        const std::string key = trim(line.substr(0, equals));
        const std::string value = trim(line.substr(equals + 1U));
        if (!seen_keys.insert(key).second) {
            throw std::runtime_error("Duplicate config key: " + key);
        }

        if (key == "num_destinations") {
            config.num_destinations = parse_u32(key, value);
        } else if (key == "num_experts_per_destination") {
            config.num_experts_per_destination = parse_u32(key, value);
        } else if (key == "initial_credits_per_destination") {
            config.initial_credits_per_destination = parse_u32(key, value);
        } else if (key == "aggregation_threshold") {
            config.aggregation_threshold = parse_u32(key, value);
        } else if (key == "aggregation_timeout_cycles") {
            config.aggregation_timeout_cycles = parse_u64(key, value);
        } else if (key == "link_bytes_per_cycle") {
            config.link_bytes_per_cycle = parse_u32(key, value);
        } else if (key == "packet_fixed_overhead_cycles") {
            config.packet_fixed_overhead_cycles = parse_u64(key, value);
        } else if (key == "receiver_processing_cycles") {
            config.receiver_processing_cycles = parse_u64(key, value);
        } else if (key == "expert_counter_return_cycles") {
            config.expert_counter_return_cycles = parse_u64(key, value);
        } else if (key == "scheduling_policy") {
            config.scheduling_policy = parse_policy(value);
        } else if (key == "enable_credit_control") {
            config.enable_credit_control = parse_bool(key, value);
        } else if (key == "enable_aggregation") {
            config.enable_aggregation = parse_bool(key, value);
        } else if (key == "enable_async_sending") {
            config.enable_async_sending = parse_bool(key, value);
        } else if (key == "enable_expert_counters") {
            config.enable_expert_counters = parse_bool(key, value);
        } else if (key == "enable_blocked_token_reorder") {
            config.enable_blocked_token_reorder = parse_bool(key, value);
        } else if (key == "expert_counter_limit") {
            config.expert_counter_limit = parse_u32(key, value);
        } else if (key == "reorder_window") {
            config.reorder_window = parse_u32(key, value);
        } else if (key == "max_queue_depth") {
            config.max_queue_depth = parse_u32(key, value);
        } else if (key == "max_inflight_batches") {
            config.max_inflight_batches = parse_u32(key, value);
        } else if (key == "handoff_latency_cycles") {
            config.handoff_latency_cycles = parse_u64(key, value);
        } else {
            throw std::runtime_error("Unknown config key: " + key);
        }
    }
    return config;
}

std::vector<SmartnicTokenDescriptor> read_trace_csv(
    const std::string& path,
    const SmartnicRuntimeConfig& config) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("Failed to open trace file: " + path);
    }

    std::string line;
    if (!std::getline(input, line)) {
        throw std::runtime_error("Trace file is empty: " + path);
    }
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    if (line != kExpectedHeader) {
        throw std::runtime_error("Trace CSV header mismatch in " + path);
    }

    std::vector<SmartnicTokenDescriptor> trace;
    std::unordered_set<std::uint64_t> token_ids;
    std::uint64_t previous_arrival = 0;
    bool have_previous = false;
    std::uint64_t line_number = 1;
    while (std::getline(input, line)) {
        ++line_number;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            continue;
        }
        const std::vector<std::string> fields = split_csv_line(line);
        if (fields.size() != 8) {
            throw std::runtime_error(
                "Trace line " + std::to_string(line_number) + " has invalid field count");
        }
        SmartnicTokenDescriptor token;
        token.arrival_cycle = parse_u64("arrival_cycle", fields[0]);
        token.token_id = parse_u64("token_id", fields[1]);
        token.batch_id = parse_u32("batch_id", fields[2]);
        token.layer_id = parse_u16("layer_id", fields[3]);
        token.src_rank = parse_u16("src_rank", fields[4]);
        token.dst_rank = parse_u16("dst_rank", fields[5]);
        token.expert_id = parse_u16("expert_id", fields[6]);
        token.payload_bytes = parse_u32("payload_bytes", fields[7]);
        token.gpu_ready_cycle = token.arrival_cycle;
        token.nic_visible_cycle = token.arrival_cycle;

        if (token.dst_rank >= config.num_destinations) {
            throw std::runtime_error("Trace line " + std::to_string(line_number) + " has invalid dst_rank");
        }
        if (token.payload_bytes == 0) {
            throw std::runtime_error("Trace line " + std::to_string(line_number) + " has zero payload");
        }
        if (!token_ids.insert(token.token_id).second) {
            throw std::runtime_error("Duplicate token_id on trace line " + std::to_string(line_number));
        }
        if (have_previous && token.arrival_cycle < previous_arrival) {
            throw std::runtime_error("Trace line " + std::to_string(line_number) + " has decreasing arrival_cycle");
        }
        previous_arrival = token.arrival_cycle;
        have_previous = true;
        trace.push_back(token);
    }
    return trace;
}

void write_token_records_csv(
    const std::string& path,
    const std::vector<SmartnicTokenRecord>& records) {
    std::vector<SmartnicTokenRecord> sorted = records;
    std::sort(sorted.begin(), sorted.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.token_id < rhs.token_id;
    });

    std::ofstream output(path);
    if (!output) {
        throw std::runtime_error("Failed to open token output CSV: " + path);
    }
    output << "token_id,dst_rank,arrival_cycle,enqueue_cycle,dispatch_cycle,completion_cycle,"
           << "queue_delay,total_latency,queue_depth_at_enqueue,aggregation_size,credit_stalled,counter_stalled\n";
    for (const SmartnicTokenRecord& record : sorted) {
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
               << (record.credit_stalled != 0 ? "true" : "false") << ','
               << (record.counter_stalled != 0 ? "true" : "false") << '\n';
    }
}

void write_packet_records_csv(
    const std::string& path,
    const std::vector<SmartnicPacketRecord>& records) {
    std::ofstream output(path);
    if (!output) {
        throw std::runtime_error("Failed to open packet output CSV: " + path);
    }
    output << "packet_id,dst_rank,aggregation_size,total_payload_bytes,first_token_id,"
           << "dispatch_cycle,completion_cycle,token_ids\n";
    for (const SmartnicPacketRecord& packet : records) {
        output << packet.packet_id << ','
               << packet.dst_rank << ','
               << packet.aggregation_size << ','
               << packet.total_payload_bytes << ','
               << packet.first_token_id << ','
               << packet.dispatch_cycle << ','
               << packet.completion_cycle << ',';
        for (std::uint32_t index = 0; index < packet.aggregation_size; ++index) {
            if (index != 0) {
                output << '|';
            }
            output << packet.token_ids[index];
        }
        output << '\n';
    }
}

void write_summary_file(
    const std::string& path,
    const SmartnicMetrics& metrics,
    const std::vector<SmartnicTokenRecord>& records,
    std::uint32_t num_destinations) {
    std::ofstream output(path);
    if (!output) {
        throw std::runtime_error("Failed to open summary file: " + path);
    }

    std::vector<std::uint64_t> latencies;
    latencies.reserve(records.size());
    std::uint64_t latency_sum = 0;
    std::uint64_t queue_delay_sum = 0;
    for (const SmartnicTokenRecord& record : records) {
        const std::uint64_t latency = record.completion_cycle - record.arrival_cycle;
        const std::uint64_t queue_delay = record.dispatch_cycle - record.arrival_cycle;
        latencies.push_back(latency);
        latency_sum += latency;
        queue_delay_sum += queue_delay;
    }

    const double average_latency =
        records.empty() ? 0.0 : static_cast<double>(latency_sum) / static_cast<double>(records.size());
    const double average_queue_delay =
        records.empty() ? 0.0 : static_cast<double>(queue_delay_sum) / static_cast<double>(records.size());
    const double throughput =
        metrics.final_cycle == 0 ? 0.0 : static_cast<double>(records.size()) / static_cast<double>(metrics.final_cycle);

    output << std::fixed << std::setprecision(6);
    output << "Total tokens: " << metrics.total_tokens << '\n';
    output << "Total packets: " << metrics.total_packets << '\n';
    output << "Final cycle: " << metrics.final_cycle << '\n';
    output << "Throughput tokens/cycle: " << throughput << '\n';
    output << "Average latency cycles: " << average_latency << '\n';
    output << "P50 latency cycles: " << percentile_nearest_rank(latencies, 0.50) << '\n';
    output << "P95 latency cycles: " << percentile_nearest_rank(latencies, 0.95) << '\n';
    output << "P99 latency cycles: " << percentile_nearest_rank(latencies, 0.99) << '\n';
    output << "Average queue delay cycles: " << average_queue_delay << '\n';
    output << "Maximum queue depth: " << metrics.max_queue_depth << '\n';
    output << "Total credit stall cycles: " << metrics.credit_stall_cycles << '\n';
    output << "Total counter stall cycles: " << metrics.counter_stall_cycles << '\n';
    output << '\n';
    for (std::uint32_t destination = 0; destination < num_destinations; ++destination) {
        output << "Destination " << destination << ":\n";
        output << "  Tokens sent: " << metrics.per_destination_tokens[destination] << '\n';
        output << "  Packets sent: " << metrics.per_destination_packets[destination] << '\n';
        output << "  Maximum queue depth: " << metrics.per_destination_max_queue_depth[destination] << '\n';
        output << "  Credit stall cycles: " << metrics.per_destination_credit_stall_cycles[destination] << '\n';
        output << "  Counter stall cycles: " << metrics.per_destination_counter_stall_cycles[destination] << '\n';
    }
}

void write_metrics_json(
    const std::string& path,
    const SmartnicMetrics& metrics,
    std::uint32_t num_destinations) {
    std::ofstream output(path);
    if (!output) {
        throw std::runtime_error("Failed to open metrics JSON: " + path);
    }
    output << "{\n";
    output << "  \"status_code\": " << metrics.status_code << ",\n";
    output << "  \"total_tokens\": " << metrics.total_tokens << ",\n";
    output << "  \"total_packets\": " << metrics.total_packets << ",\n";
    output << "  \"final_cycle\": " << metrics.final_cycle << ",\n";
    output << "  \"max_queue_depth\": " << metrics.max_queue_depth << ",\n";
    output << "  \"aggregation_size_sum\": " << metrics.aggregation_size_sum << ",\n";
    output << "  \"aggregation_timeout_flushes\": " << metrics.aggregation_timeout_flushes << ",\n";
    output << "  \"credit_stall_cycles\": " << metrics.credit_stall_cycles << ",\n";
    output << "  \"counter_stall_cycles\": " << metrics.counter_stall_cycles << ",\n";
    output << "  \"reorder_events\": " << metrics.reorder_events << ",\n";
    output << "  \"errors\": {\n";
    output << "    \"queue_overflow\": " << metrics.queue_overflow_errors << ",\n";
    output << "    \"malformed_descriptor\": " << metrics.malformed_descriptor_errors << ",\n";
    output << "    \"event_overflow\": " << metrics.event_overflow_errors << ",\n";
    output << "    \"deadlock\": " << metrics.deadlock_errors << "\n";
    output << "  },\n";
    output << "  \"destinations\": [\n";
    for (std::uint32_t destination = 0; destination < num_destinations; ++destination) {
        output << "    {\"destination\": " << destination
               << ", \"tokens\": " << metrics.per_destination_tokens[destination]
               << ", \"packets\": " << metrics.per_destination_packets[destination]
               << ", \"max_queue_depth\": " << metrics.per_destination_max_queue_depth[destination]
               << ", \"credit_stall_cycles\": " << metrics.per_destination_credit_stall_cycles[destination]
               << ", \"counter_stall_cycles\": " << metrics.per_destination_counter_stall_cycles[destination]
               << "}";
        if (destination + 1U != num_destinations) {
            output << ',';
        }
        output << '\n';
    }
    output << "  ]\n";
    output << "}\n";
}

} // namespace smartnic_fpga
