#pragma once

#include <cstdint>

#include "smartnic_config.hpp"

namespace smartnic_fpga {

struct SmartnicTokenDescriptor {
    std::uint64_t token_id = 0;
    std::uint32_t batch_id = 0;
    std::uint16_t layer_id = 0;
    std::uint16_t src_rank = 0;
    std::uint16_t dst_rank = 0;
    std::uint16_t expert_id = 0;
    std::uint32_t payload_bytes = 0;
    std::uint64_t arrival_cycle = 0;
    std::uint64_t gpu_ready_cycle = 0;
    std::uint64_t nic_visible_cycle = 0;
};

struct SmartnicRuntimeConfig {
    std::uint32_t num_destinations = 4;
    std::uint32_t num_experts_per_destination = kMaxExpertsPerDestination;
    std::uint32_t initial_credits_per_destination = 16;
    std::uint32_t aggregation_threshold = 1;
    std::uint32_t link_bytes_per_cycle = 32;
    std::uint64_t aggregation_timeout_cycles = 0;
    std::uint64_t packet_fixed_overhead_cycles = 20;
    std::uint64_t receiver_processing_cycles = 100;
    std::uint64_t expert_counter_return_cycles = 100;
    std::uint32_t scheduling_policy = kRoundRobin;
    std::uint32_t enable_credit_control = 0;
    std::uint32_t enable_aggregation = 0;
    std::uint32_t enable_async_sending = 1;
    std::uint32_t enable_expert_counters = 0;
    std::uint32_t enable_blocked_token_reorder = 1;
    std::uint32_t expert_counter_limit = 0;
    std::uint32_t reorder_window = kMaxQueueDepth;
    std::uint32_t max_queue_depth = kMaxQueueDepth;
    std::uint32_t max_inflight_batches = 1;
    std::uint64_t handoff_latency_cycles = 0;
};

struct SmartnicTokenRecord {
    std::uint64_t token_id = 0;
    std::uint16_t dst_rank = 0;
    std::uint16_t expert_id = 0;
    std::uint32_t aggregation_size = 0;
    std::uint64_t arrival_cycle = 0;
    std::uint64_t enqueue_cycle = 0;
    std::uint64_t dispatch_cycle = 0;
    std::uint64_t completion_cycle = 0;
    std::uint32_t queue_depth_at_enqueue = 0;
    std::uint32_t credit_stalled = 0;
    std::uint32_t counter_stalled = 0;
};

struct SmartnicPacketRecord {
    std::uint64_t packet_id = 0;
    std::uint16_t dst_rank = 0;
    std::uint16_t aggregation_size = 0;
    std::uint32_t total_payload_bytes = 0;
    std::uint64_t first_token_id = 0;
    std::uint64_t dispatch_cycle = 0;
    std::uint64_t completion_cycle = 0;
    std::uint64_t token_ids[kMaxAggTokens] = {};
};

struct SmartnicMetrics {
    std::int32_t status_code = 0;
    std::uint64_t total_tokens = 0;
    std::uint64_t total_packets = 0;
    std::uint64_t final_cycle = 0;
    std::uint32_t max_queue_depth = 0;
    std::uint64_t aggregation_size_sum = 0;
    std::uint64_t aggregation_timeout_flushes = 0;
    std::uint64_t credit_stall_cycles = 0;
    std::uint64_t counter_stall_cycles = 0;
    std::uint64_t reorder_events = 0;
    std::uint64_t queue_overflow_errors = 0;
    std::uint64_t malformed_descriptor_errors = 0;
    std::uint64_t event_overflow_errors = 0;
    std::uint64_t deadlock_errors = 0;
    std::uint64_t per_destination_packets[kMaxDestinations] = {};
    std::uint64_t per_destination_tokens[kMaxDestinations] = {};
    std::uint32_t per_destination_max_queue_depth[kMaxDestinations] = {};
    std::uint64_t per_destination_credit_stall_cycles[kMaxDestinations] = {};
    std::uint64_t per_destination_counter_stall_cycles[kMaxDestinations] = {};
};

} // namespace smartnic_fpga
