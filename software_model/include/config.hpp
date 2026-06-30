#pragma once

#include <cstdint>
#include <string>

enum class SchedulingPolicy {
    RoundRobin,
    OldestFirst,
    LargestQueue,
    CreditAware
};

struct SimulatorConfig {
    std::uint32_t num_destinations = 4;

    std::uint32_t initial_credits_per_destination = 16;

    std::uint32_t aggregation_threshold = 1;
    std::uint64_t aggregation_timeout_cycles = 0;

    std::uint32_t link_bytes_per_cycle = 32;
    std::uint64_t packet_fixed_overhead_cycles = 20;

    std::uint64_t receiver_processing_cycles = 100;

    SchedulingPolicy scheduling_policy = SchedulingPolicy::RoundRobin;

    bool enable_credit_control = false;
    bool enable_aggregation = false;
};

SimulatorConfig read_config_file(const std::string& path);
SchedulingPolicy parse_scheduling_policy(const std::string& value);
std::string scheduling_policy_name(SchedulingPolicy policy);
