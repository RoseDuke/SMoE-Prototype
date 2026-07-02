#include "config.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>
#include <string>
#include <unordered_set>

namespace {

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
        throw std::runtime_error("Invalid integer for config key '" + key + "': " + value);
    }
    if (parsed != value.size()) {
        throw std::runtime_error("Invalid trailing characters for config key '" + key + "': " + value);
    }
    return static_cast<std::uint64_t>(result);
}

std::uint32_t parse_u32(const std::string& key, const std::string& value) {
    const std::uint64_t parsed = parse_u64(key, value);
    if (parsed > UINT32_MAX) {
        throw std::runtime_error("Config key '" + key + "' exceeds uint32 range");
    }
    return static_cast<std::uint32_t>(parsed);
}

bool parse_bool(const std::string& key, const std::string& value) {
    if (value == "true" || value == "1") {
        return true;
    }
    if (value == "false" || value == "0") {
        return false;
    }
    throw std::runtime_error("Invalid boolean for config key '" + key + "': " + value);
}

void validate_config(const SimulatorConfig& config) {
    if (config.num_destinations == 0) {
        throw std::runtime_error("num_destinations must be greater than zero");
    }
    if (config.link_bytes_per_cycle == 0) {
        throw std::runtime_error("link_bytes_per_cycle must be greater than zero");
    }
    if (config.aggregation_threshold == 0) {
        throw std::runtime_error("aggregation_threshold must be greater than zero");
    }
    if (config.enable_credit_control && config.initial_credits_per_destination == 0) {
        throw std::runtime_error("initial_credits_per_destination must be greater than zero when credit control is enabled");
    }
    if (config.enable_expert_counters && config.expert_counter_limit == 0) {
        throw std::runtime_error("expert_counter_limit must be greater than zero when expert counters are enabled");
    }
}

} // namespace

SchedulingPolicy parse_scheduling_policy(const std::string& value) {
    if (value == "round_robin") {
        return SchedulingPolicy::RoundRobin;
    }
    if (value == "oldest_first") {
        return SchedulingPolicy::OldestFirst;
    }
    if (value == "largest_queue") {
        return SchedulingPolicy::LargestQueue;
    }
    if (value == "credit_aware") {
        return SchedulingPolicy::CreditAware;
    }
    throw std::runtime_error("Unsupported scheduling_policy: " + value);
}

std::string scheduling_policy_name(SchedulingPolicy policy) {
    switch (policy) {
    case SchedulingPolicy::RoundRobin:
        return "round_robin";
    case SchedulingPolicy::OldestFirst:
        return "oldest_first";
    case SchedulingPolicy::LargestQueue:
        return "largest_queue";
    case SchedulingPolicy::CreditAware:
        return "credit_aware";
    }
    return "unknown";
}

SimulatorConfig read_config_file(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("Failed to open config file: " + path);
    }

    SimulatorConfig config;
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
            throw std::runtime_error("Malformed config line " + std::to_string(line_number) + ": missing '='");
        }

        const std::string key = trim(line.substr(0, equals));
        const std::string value = trim(line.substr(equals + 1));
        if (key.empty() || value.empty()) {
            throw std::runtime_error("Malformed config line " + std::to_string(line_number) + ": empty key or value");
        }
        if (!seen_keys.insert(key).second) {
            throw std::runtime_error("Duplicate config key: " + key);
        }

        if (key == "num_destinations") {
            config.num_destinations = parse_u32(key, value);
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
            config.scheduling_policy = parse_scheduling_policy(value);
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
        } else {
            throw std::runtime_error("Unknown config key: " + key);
        }
    }

    validate_config(config);
    return config;
}
