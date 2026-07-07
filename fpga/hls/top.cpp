#include "smartnic_v0.hpp"

#include <cstdint>
#include <limits>

namespace smartnic_fpga {
namespace {

constexpr std::uint32_t kInvalidIndex = std::numeric_limits<std::uint32_t>::max();
constexpr std::uint64_t kMaxCycle = std::numeric_limits<std::uint64_t>::max();

enum EventType : std::uint32_t {
    kTokenArrival = 0,
    kTransmissionComplete = 1,
    kCreditReturn = 2,
    kExpertCounterReturn = 3
};

struct Event {
    std::uint64_t cycle = 0;
    std::uint32_t type = kTokenArrival;
    std::uint32_t destination = 0;
    std::uint32_t token_index = 0;
    std::uint64_t sequence_number = 0;
};

struct DispatchState {
    std::uint64_t current_cycle = 0;
    std::uint64_t final_cycle = 0;
    std::uint64_t next_sequence_number = 0;
    std::uint64_t link_busy_until = 0;
    std::uint32_t next_round_robin_destination = 0;
    std::uint32_t event_count = 0;
    std::uint32_t record_count = 0;
    std::uint32_t packet_count = 0;
    bool link_busy = false;
    bool drain_mode = false;

    Event events[kMaxEvents] = {};
    std::uint64_t enqueue_cycle[kMaxTokens] = {};
    std::uint64_t effective_enqueue_cycle[kMaxTokens] = {};
    std::uint32_t queue_depth_at_enqueue[kMaxTokens] = {};
    std::uint32_t queue_token_index[kMaxDestinations][kMaxQueueDepth] = {};
    std::uint32_t queue_depth[kMaxDestinations] = {};
    std::uint32_t available_credits[kMaxDestinations] = {};
    std::uint32_t max_credits[kMaxDestinations] = {};
    std::uint32_t max_queue_depth[kMaxDestinations] = {};
    std::uint64_t total_tokens_sent[kMaxDestinations] = {};
    std::uint64_t total_packets_sent[kMaxDestinations] = {};
    std::uint64_t credit_stall_cycles[kMaxDestinations] = {};
    std::uint64_t counter_stall_cycles[kMaxDestinations] = {};
    std::uint32_t expert_tokens_in_flight[kMaxDestinations][kMaxExpertsPerDestination] = {};
    bool completed[kMaxTokens] = {};
    bool credit_stalled[kMaxTokens] = {};
    bool counter_stalled[kMaxTokens] = {};
};

std::uint32_t effective_expert_count(const SmartnicRuntimeConfig& config) {
    return config.num_experts_per_destination == 0
               ? kMaxExpertsPerDestination
               : config.num_experts_per_destination;
}

std::uint32_t effective_reorder_window(const SmartnicRuntimeConfig& config) {
    if (config.reorder_window == 0 || config.reorder_window > kMaxQueueDepth) {
        return kMaxQueueDepth;
    }
    return config.reorder_window;
}

std::uint32_t effective_queue_depth(const SmartnicRuntimeConfig& config) {
    if (config.max_queue_depth == 0 || config.max_queue_depth > kMaxQueueDepth) {
        return kMaxQueueDepth;
    }
    return config.max_queue_depth;
}

int event_priority(std::uint32_t type) {
    if (type == kTransmissionComplete) {
        return 0;
    }
    if (type == kCreditReturn || type == kExpertCounterReturn) {
        return 1;
    }
    if (type == kTokenArrival) {
        return 2;
    }
    return 3;
}

bool event_less(const Event& lhs, const Event& rhs) {
    if (lhs.cycle != rhs.cycle) {
        return lhs.cycle < rhs.cycle;
    }
    const int lhs_priority = event_priority(lhs.type);
    const int rhs_priority = event_priority(rhs.type);
    if (lhs_priority != rhs_priority) {
        return lhs_priority < rhs_priority;
    }
    return lhs.sequence_number < rhs.sequence_number;
}

std::uint64_t batch_layer_key(const SmartnicTokenDescriptor& token) {
    return (static_cast<std::uint64_t>(token.batch_id) << 32U) |
           static_cast<std::uint64_t>(token.layer_id);
}

void reset_metrics(SmartnicMetrics& metrics) {
    metrics.status_code = kStatusOk;
    metrics.total_tokens = 0;
    metrics.total_packets = 0;
    metrics.final_cycle = 0;
    metrics.max_queue_depth = 0;
    metrics.aggregation_size_sum = 0;
    metrics.aggregation_timeout_flushes = 0;
    metrics.credit_stall_cycles = 0;
    metrics.counter_stall_cycles = 0;
    metrics.reorder_events = 0;
    metrics.queue_overflow_errors = 0;
    metrics.malformed_descriptor_errors = 0;
    metrics.event_overflow_errors = 0;
    metrics.deadlock_errors = 0;
    for (std::uint32_t destination = 0; destination < kMaxDestinations; ++destination) {
        metrics.per_destination_packets[destination] = 0;
        metrics.per_destination_tokens[destination] = 0;
        metrics.per_destination_max_queue_depth[destination] = 0;
        metrics.per_destination_credit_stall_cycles[destination] = 0;
        metrics.per_destination_counter_stall_cycles[destination] = 0;
    }
}

void reset_state(DispatchState& state, const SmartnicRuntimeConfig& config) {
    state.current_cycle = 0;
    state.final_cycle = 0;
    state.next_sequence_number = 0;
    state.link_busy_until = 0;
    state.next_round_robin_destination = 0;
    state.event_count = 0;
    state.record_count = 0;
    state.packet_count = 0;
    state.link_busy = false;
    state.drain_mode = false;

    for (std::uint32_t token = 0; token < kMaxTokens; ++token) {
        state.enqueue_cycle[token] = 0;
        state.effective_enqueue_cycle[token] = 0;
        state.queue_depth_at_enqueue[token] = 0;
        state.completed[token] = false;
        state.credit_stalled[token] = false;
        state.counter_stalled[token] = false;
    }
    for (std::uint32_t destination = 0; destination < kMaxDestinations; ++destination) {
        state.queue_depth[destination] = 0;
        state.available_credits[destination] = config.initial_credits_per_destination;
        state.max_credits[destination] = config.initial_credits_per_destination;
        state.max_queue_depth[destination] = 0;
        state.total_tokens_sent[destination] = 0;
        state.total_packets_sent[destination] = 0;
        state.credit_stall_cycles[destination] = 0;
        state.counter_stall_cycles[destination] = 0;
        for (std::uint32_t expert = 0; expert < kMaxExpertsPerDestination; ++expert) {
            state.expert_tokens_in_flight[destination][expert] = 0;
        }
    }
}

bool push_event(
    DispatchState& state,
    SmartnicMetrics& metrics,
    std::uint64_t cycle,
    std::uint32_t type,
    std::uint32_t destination,
    std::uint32_t token_index) {
    if (state.event_count >= kMaxEvents) {
        metrics.event_overflow_errors += 1;
        return false;
    }
    Event& event = state.events[state.event_count++];
    event.cycle = cycle;
    event.type = type;
    event.destination = destination;
    event.token_index = token_index;
    event.sequence_number = state.next_sequence_number++;
    return true;
}

bool pop_next_event(DispatchState& state, Event& event) {
    if (state.event_count == 0) {
        return false;
    }
    std::uint32_t best_index = 0;
    for (std::uint32_t index = 1; index < state.event_count; ++index) {
        if (event_less(state.events[index], state.events[best_index])) {
            best_index = index;
        }
    }
    event = state.events[best_index];
    state.events[best_index] = state.events[state.event_count - 1U];
    state.event_count -= 1;
    return true;
}

std::uint64_t next_event_cycle(const DispatchState& state) {
    if (state.event_count == 0) {
        return kMaxCycle;
    }
    std::uint32_t best_index = 0;
    for (std::uint32_t index = 1; index < state.event_count; ++index) {
        if (event_less(state.events[index], state.events[best_index])) {
            best_index = index;
        }
    }
    return state.events[best_index].cycle;
}

bool same_token_id_exists_before(
    const SmartnicTokenDescriptor* descriptors,
    std::uint32_t token_index) {
    for (std::uint32_t index = 0; index < token_index; ++index) {
        if (descriptors[index].token_id == descriptors[token_index].token_id) {
            return true;
        }
    }
    return false;
}

bool validate_config(const SmartnicRuntimeConfig& config, SmartnicMetrics& metrics) {
    if (config.num_destinations == 0 || config.num_destinations > kMaxDestinations) {
        metrics.malformed_descriptor_errors += 1;
        return false;
    }
    if (effective_expert_count(config) == 0 ||
        effective_expert_count(config) > kMaxExpertsPerDestination) {
        metrics.malformed_descriptor_errors += 1;
        return false;
    }
    if (config.link_bytes_per_cycle == 0) {
        metrics.malformed_descriptor_errors += 1;
        return false;
    }
    if (config.aggregation_threshold == 0 ||
        config.aggregation_threshold > kMaxAggTokens) {
        metrics.malformed_descriptor_errors += 1;
        return false;
    }
    if (config.enable_credit_control != 0 &&
        config.initial_credits_per_destination == 0) {
        metrics.malformed_descriptor_errors += 1;
        return false;
    }
    if (config.enable_expert_counters != 0 &&
        (config.expert_counter_limit == 0 || config.expert_counter_return_cycles == 0)) {
        metrics.malformed_descriptor_errors += 1;
        return false;
    }
    if (config.scheduling_policy > kCreditAware) {
        metrics.malformed_descriptor_errors += 1;
        return false;
    }
    return true;
}

bool validate_trace(
    const SmartnicTokenDescriptor* descriptors,
    std::uint32_t descriptor_count,
    const SmartnicRuntimeConfig& config,
    SmartnicMetrics& metrics) {
    if (descriptor_count > kMaxTokens) {
        metrics.malformed_descriptor_errors += 1;
        return false;
    }
    const std::uint32_t expert_count = effective_expert_count(config);
    std::uint64_t previous_arrival = 0;
    bool have_previous = false;
    for (std::uint32_t index = 0; index < descriptor_count; ++index) {
        const SmartnicTokenDescriptor& token = descriptors[index];
        if (same_token_id_exists_before(descriptors, index)) {
            metrics.malformed_descriptor_errors += 1;
            return false;
        }
        if (token.dst_rank >= config.num_destinations ||
            token.expert_id >= expert_count ||
            token.payload_bytes == 0) {
            metrics.malformed_descriptor_errors += 1;
            return false;
        }
        if (have_previous && token.arrival_cycle < previous_arrival) {
            metrics.malformed_descriptor_errors += 1;
            return false;
        }
        previous_arrival = token.arrival_cycle;
        have_previous = true;
    }
    return true;
}

void compute_effective_enqueue_cycles(
    DispatchState& state,
    const SmartnicTokenDescriptor* descriptors,
    std::uint32_t descriptor_count,
    const SmartnicRuntimeConfig& config) {
    for (std::uint32_t index = 0; index < descriptor_count; ++index) {
        if (config.enable_async_sending != 0) {
            state.effective_enqueue_cycle[index] = descriptors[index].arrival_cycle;
            continue;
        }
        std::uint64_t group_ready_cycle = descriptors[index].arrival_cycle;
        const std::uint64_t key = batch_layer_key(descriptors[index]);
        for (std::uint32_t other = 0; other < descriptor_count; ++other) {
            if (batch_layer_key(descriptors[other]) == key &&
                descriptors[other].arrival_cycle > group_ready_cycle) {
                group_ready_cycle = descriptors[other].arrival_cycle;
            }
        }
        state.effective_enqueue_cycle[index] = group_ready_cycle;
    }
}

bool seed_arrival_events(
    DispatchState& state,
    SmartnicMetrics& metrics,
    const SmartnicTokenDescriptor* descriptors,
    std::uint32_t descriptor_count) {
    for (std::uint32_t index = 0; index < descriptor_count; ++index) {
        if (!push_event(
                state,
                metrics,
                state.effective_enqueue_cycle[index],
                kTokenArrival,
                descriptors[index].dst_rank,
                index)) {
            return false;
        }
    }
    return true;
}

bool all_queues_empty(const DispatchState& state, const SmartnicRuntimeConfig& config) {
    for (std::uint32_t destination = 0; destination < config.num_destinations; ++destination) {
        if (state.queue_depth[destination] != 0) {
            return false;
        }
    }
    return true;
}

bool token_has_expert_counter_slot(
    const DispatchState& state,
    const SmartnicRuntimeConfig& config,
    const SmartnicTokenDescriptor& token,
    const std::uint32_t* local_expert_counts) {
    if (config.enable_expert_counters == 0) {
        return true;
    }
    const std::uint32_t global =
        state.expert_tokens_in_flight[token.dst_rank][token.expert_id];
    const std::uint32_t local =
        local_expert_counts == nullptr ? 0U : local_expert_counts[token.expert_id];
    return global + local < config.expert_counter_limit;
}

std::uint32_t find_dispatchable_token_index(
    const DispatchState& state,
    const SmartnicRuntimeConfig& config,
    const SmartnicTokenDescriptor* descriptors,
    std::uint32_t destination) {
    const std::uint32_t depth = state.queue_depth[destination];
    if (depth == 0) {
        return kInvalidIndex;
    }
    if (config.enable_expert_counters == 0) {
        return 0;
    }
    const std::uint32_t front_token_index = state.queue_token_index[destination][0];
    if (config.enable_blocked_token_reorder == 0) {
        return token_has_expert_counter_slot(
                   state,
                   config,
                   descriptors[front_token_index],
                   nullptr)
                   ? 0U
                   : kInvalidIndex;
    }
    const std::uint32_t scan_limit =
        depth < effective_reorder_window(config) ? depth : effective_reorder_window(config);
    for (std::uint32_t queue_index = 0; queue_index < scan_limit; ++queue_index) {
        const std::uint32_t token_index = state.queue_token_index[destination][queue_index];
        if (token_has_expert_counter_slot(state, config, descriptors[token_index], nullptr)) {
            return queue_index;
        }
    }
    return kInvalidIndex;
}

bool can_dispatch_destination(
    const DispatchState& state,
    const SmartnicRuntimeConfig& config,
    const SmartnicTokenDescriptor* descriptors,
    std::uint32_t destination) {
    if (destination >= config.num_destinations ||
        state.queue_depth[destination] == 0) {
        return false;
    }
    if (config.enable_credit_control != 0 && state.available_credits[destination] == 0) {
        return false;
    }
    if (find_dispatchable_token_index(state, config, descriptors, destination) == kInvalidIndex) {
        return false;
    }
    if (config.enable_aggregation == 0) {
        return true;
    }
    if (state.queue_depth[destination] >= config.aggregation_threshold) {
        return true;
    }
    if (state.drain_mode) {
        return true;
    }
    if (config.aggregation_timeout_cycles == 0) {
        return false;
    }
    const std::uint32_t front_index = state.queue_token_index[destination][0];
    const std::uint64_t oldest_arrival = descriptors[front_index].arrival_cycle;
    return state.current_cycle >= oldest_arrival &&
           state.current_cycle - oldest_arrival >= config.aggregation_timeout_cycles;
}

std::uint64_t next_timeout_cycle(
    const DispatchState& state,
    const SmartnicRuntimeConfig& config,
    const SmartnicTokenDescriptor* descriptors) {
    if (config.enable_aggregation == 0 || config.aggregation_timeout_cycles == 0) {
        return kMaxCycle;
    }
    std::uint64_t best = kMaxCycle;
    for (std::uint32_t destination = 0; destination < config.num_destinations; ++destination) {
        if (state.queue_depth[destination] == 0) {
            continue;
        }
        if (config.enable_credit_control != 0 && state.available_credits[destination] == 0) {
            continue;
        }
        if (find_dispatchable_token_index(state, config, descriptors, destination) == kInvalidIndex) {
            continue;
        }
        if (state.queue_depth[destination] >= config.aggregation_threshold) {
            return state.current_cycle;
        }
        const std::uint32_t front_index = state.queue_token_index[destination][0];
        const std::uint64_t wakeup =
            descriptors[front_index].arrival_cycle + config.aggregation_timeout_cycles;
        if (wakeup < best) {
            best = wakeup;
        }
    }
    return best;
}

void account_stalls_until(
    DispatchState& state,
    const SmartnicRuntimeConfig& config,
    const SmartnicTokenDescriptor* descriptors,
    std::uint64_t next_cycle) {
    if ((config.enable_credit_control == 0 && config.enable_expert_counters == 0) ||
        next_cycle <= state.current_cycle) {
        return;
    }
    const std::uint64_t delta = next_cycle - state.current_cycle;
    for (std::uint32_t destination = 0; destination < config.num_destinations; ++destination) {
        const std::uint32_t depth = state.queue_depth[destination];
        if (depth == 0) {
            continue;
        }
        if (state.available_credits[destination] == 0) {
            state.credit_stall_cycles[destination] += delta;
            for (std::uint32_t queue_index = 0; queue_index < depth; ++queue_index) {
                state.credit_stalled[state.queue_token_index[destination][queue_index]] = true;
            }
        }
        if (config.enable_expert_counters != 0) {
            bool any_counter_blocked = false;
            bool any_dispatchable = false;
            for (std::uint32_t queue_index = 0; queue_index < depth; ++queue_index) {
                const std::uint32_t token_index = state.queue_token_index[destination][queue_index];
                if (token_has_expert_counter_slot(state, config, descriptors[token_index], nullptr)) {
                    any_dispatchable = true;
                } else {
                    state.counter_stalled[token_index] = true;
                    any_counter_blocked = true;
                }
            }
            if (any_counter_blocked &&
                (!any_dispatchable || config.enable_blocked_token_reorder == 0)) {
                state.counter_stall_cycles[destination] += delta;
            }
        }
    }
}

std::uint64_t compute_service_cycles(
    const SmartnicRuntimeConfig& config,
    std::uint64_t payload_bytes) {
    const std::uint64_t transfer_cycles =
        (payload_bytes + config.link_bytes_per_cycle - 1U) /
        config.link_bytes_per_cycle;
    return config.packet_fixed_overhead_cycles + transfer_cycles;
}

std::uint32_t select_destination(
    DispatchState& state,
    const SmartnicRuntimeConfig& config,
    const SmartnicTokenDescriptor* descriptors) {
    switch (config.scheduling_policy) {
    case kRoundRobin:
        for (std::uint32_t offset = 0; offset < config.num_destinations; ++offset) {
            const std::uint32_t destination =
                (state.next_round_robin_destination + offset) % config.num_destinations;
            if (can_dispatch_destination(state, config, descriptors, destination)) {
                state.next_round_robin_destination =
                    (destination + 1U) % config.num_destinations;
                return destination;
            }
        }
        return kInvalidIndex;
    case kOldestFirst: {
        std::uint64_t best_arrival = kMaxCycle;
        std::uint32_t best_destination = kInvalidIndex;
        for (std::uint32_t destination = 0; destination < config.num_destinations; ++destination) {
            if (!can_dispatch_destination(state, config, descriptors, destination)) {
                continue;
            }
            const std::uint32_t front_index = state.queue_token_index[destination][0];
            const std::uint64_t arrival = descriptors[front_index].arrival_cycle;
            if (arrival < best_arrival) {
                best_arrival = arrival;
                best_destination = destination;
            }
        }
        return best_destination;
    }
    case kLargestQueue: {
        std::uint32_t best_size = 0;
        std::uint32_t best_destination = kInvalidIndex;
        for (std::uint32_t destination = 0; destination < config.num_destinations; ++destination) {
            if (!can_dispatch_destination(state, config, descriptors, destination)) {
                continue;
            }
            if (state.queue_depth[destination] > best_size) {
                best_size = state.queue_depth[destination];
                best_destination = destination;
            }
        }
        return best_destination;
    }
    case kCreditAware: {
        std::uint64_t best_wait = 0;
        std::uint32_t best_destination = kInvalidIndex;
        for (std::uint32_t destination = 0; destination < config.num_destinations; ++destination) {
            if (!can_dispatch_destination(state, config, descriptors, destination) ||
                state.available_credits[destination] == 0) {
                continue;
            }
            const std::uint32_t front_index = state.queue_token_index[destination][0];
            const std::uint64_t arrival = descriptors[front_index].arrival_cycle;
            const std::uint64_t wait =
                state.current_cycle >= arrival ? state.current_cycle - arrival : 0U;
            if (best_destination == kInvalidIndex || wait > best_wait) {
                best_wait = wait;
                best_destination = destination;
            }
        }
        return best_destination;
    }
    default:
        return kInvalidIndex;
    }
}

void remove_queue_entry(
    DispatchState& state,
    std::uint32_t destination,
    std::uint32_t queue_index) {
    const std::uint32_t depth = state.queue_depth[destination];
    for (std::uint32_t index = queue_index + 1U; index < depth; ++index) {
        state.queue_token_index[destination][index - 1U] =
            state.queue_token_index[destination][index];
    }
    state.queue_depth[destination] = depth - 1U;
}

bool emit_packet_and_records(
    DispatchState& state,
    const SmartnicRuntimeConfig& config,
    const SmartnicTokenDescriptor* descriptors,
    SmartnicTokenRecord* token_records,
    std::uint32_t token_record_capacity,
    SmartnicPacketRecord* packet_records,
    std::uint32_t packet_record_capacity,
    SmartnicMetrics& metrics,
    std::uint32_t destination,
    const std::uint32_t* packet_token_indices,
    std::uint32_t packet_token_count,
    std::uint64_t total_payload_bytes) {
    if (state.record_count + packet_token_count > token_record_capacity ||
        state.record_count + packet_token_count > kMaxTokens ||
        state.packet_count >= packet_record_capacity ||
        state.packet_count >= kMaxPackets) {
        return false;
    }

    const std::uint64_t dispatch_cycle = state.current_cycle;
    const std::uint64_t completion_cycle =
        dispatch_cycle + compute_service_cycles(config, total_payload_bytes);

    state.link_busy = true;
    state.link_busy_until = completion_cycle;
    state.total_packets_sent[destination] += 1;
    state.total_tokens_sent[destination] += packet_token_count;

    if (config.enable_expert_counters != 0) {
        for (std::uint32_t index = 0; index < packet_token_count; ++index) {
            const SmartnicTokenDescriptor& token = descriptors[packet_token_indices[index]];
            state.expert_tokens_in_flight[destination][token.expert_id] += 1;
        }
    }

    const std::uint32_t packet_index = state.packet_count;
    packet_records[packet_index].packet_id = packet_index;
    packet_records[packet_index].dst_rank = static_cast<std::uint16_t>(destination);
    packet_records[packet_index].aggregation_size =
        static_cast<std::uint16_t>(packet_token_count);
    packet_records[packet_index].total_payload_bytes =
        static_cast<std::uint32_t>(total_payload_bytes);
    packet_records[packet_index].first_token_id =
        descriptors[packet_token_indices[0]].token_id;
    packet_records[packet_index].dispatch_cycle = dispatch_cycle;
    packet_records[packet_index].completion_cycle = completion_cycle;
    for (std::uint32_t index = 0; index < packet_token_count; ++index) {
        packet_records[packet_index].token_ids[index] =
            descriptors[packet_token_indices[index]].token_id;
    }
    for (std::uint32_t index = packet_token_count; index < kMaxAggTokens; ++index) {
        packet_records[packet_index].token_ids[index] = 0;
    }
    state.packet_count += 1;

    for (std::uint32_t index = 0; index < packet_token_count; ++index) {
        const std::uint32_t token_index = packet_token_indices[index];
        const SmartnicTokenDescriptor& token = descriptors[token_index];
        const std::uint32_t record_index = state.record_count++;
        token_records[record_index].token_id = token.token_id;
        token_records[record_index].dst_rank = token.dst_rank;
        token_records[record_index].expert_id = token.expert_id;
        token_records[record_index].aggregation_size = packet_token_count;
        token_records[record_index].arrival_cycle = token.arrival_cycle;
        token_records[record_index].enqueue_cycle = state.enqueue_cycle[token_index];
        token_records[record_index].dispatch_cycle = dispatch_cycle;
        token_records[record_index].completion_cycle = completion_cycle;
        token_records[record_index].queue_depth_at_enqueue =
            state.queue_depth_at_enqueue[token_index];
        token_records[record_index].credit_stalled =
            state.credit_stalled[token_index] ? 1U : 0U;
        token_records[record_index].counter_stalled =
            state.counter_stalled[token_index] ? 1U : 0U;
        state.completed[token_index] = true;
    }

    metrics.aggregation_size_sum += packet_token_count;
    if (config.enable_aggregation != 0 &&
        packet_token_count < config.aggregation_threshold &&
        !state.drain_mode &&
        config.aggregation_timeout_cycles != 0) {
        metrics.aggregation_timeout_flushes += 1;
    }

    if (!push_event(
            state,
            metrics,
            completion_cycle,
            kTransmissionComplete,
            destination,
            packet_token_indices[0])) {
        return false;
    }
    if (config.enable_credit_control != 0) {
        if (!push_event(
                state,
                metrics,
                completion_cycle + config.receiver_processing_cycles,
                kCreditReturn,
                destination,
                packet_token_indices[0])) {
            return false;
        }
    }
    if (config.enable_expert_counters != 0) {
        for (std::uint32_t index = 0; index < packet_token_count; ++index) {
            if (!push_event(
                    state,
                    metrics,
                    completion_cycle + config.expert_counter_return_cycles,
                    kExpertCounterReturn,
                    destination,
                    packet_token_indices[index])) {
                return false;
            }
        }
    }
    return true;
}

bool try_dispatch(
    DispatchState& state,
    const SmartnicRuntimeConfig& config,
    const SmartnicTokenDescriptor* descriptors,
    SmartnicTokenRecord* token_records,
    std::uint32_t token_record_capacity,
    SmartnicPacketRecord* packet_records,
    std::uint32_t packet_record_capacity,
    SmartnicMetrics& metrics) {
    if (state.link_busy) {
        return false;
    }

    const std::uint32_t destination = select_destination(state, config, descriptors);
    if (destination == kInvalidIndex) {
        return false;
    }

    if (config.enable_credit_control != 0) {
        state.available_credits[destination] -= 1;
    }

    const std::uint32_t target_packet_tokens =
        config.enable_aggregation != 0 ? config.aggregation_threshold : 1U;
    std::uint32_t packet_token_indices[kMaxAggTokens] = {};
    std::uint32_t local_expert_counts[kMaxExpertsPerDestination] = {};
    std::uint32_t packet_token_count = 0;
    std::uint64_t total_payload_bytes = 0;

    while (packet_token_count < target_packet_tokens) {
        const std::uint32_t queue_index =
            find_dispatchable_token_index(state, config, descriptors, destination);
        if (queue_index == kInvalidIndex) {
            break;
        }
        const std::uint32_t token_index =
            state.queue_token_index[destination][queue_index];
        const SmartnicTokenDescriptor& token = descriptors[token_index];
        if (!token_has_expert_counter_slot(
                state,
                config,
                token,
                local_expert_counts)) {
            break;
        }
        if (queue_index > 0) {
            metrics.reorder_events += 1;
        }
        packet_token_indices[packet_token_count++] = token_index;
        total_payload_bytes += token.payload_bytes;
        local_expert_counts[token.expert_id] += 1;
        remove_queue_entry(state, destination, queue_index);
        if (config.enable_aggregation == 0) {
            break;
        }
    }

    if (packet_token_count == 0) {
        if (config.enable_credit_control != 0) {
            state.available_credits[destination] += 1;
        }
        return false;
    }

    return emit_packet_and_records(
        state,
        config,
        descriptors,
        token_records,
        token_record_capacity,
        packet_records,
        packet_record_capacity,
        metrics,
        destination,
        packet_token_indices,
        packet_token_count,
        total_payload_bytes);
}

bool process_token_arrival(
    DispatchState& state,
    const SmartnicRuntimeConfig& config,
    const SmartnicTokenDescriptor* descriptors,
    const Event& event,
    SmartnicMetrics& metrics) {
    const SmartnicTokenDescriptor& token = descriptors[event.token_index];
    const std::uint32_t destination = token.dst_rank;
    const std::uint32_t queue_limit = effective_queue_depth(config);
    if (state.queue_depth[destination] >= queue_limit) {
        metrics.queue_overflow_errors += 1;
        return false;
    }
    const std::uint32_t depth = state.queue_depth[destination];
    state.queue_token_index[destination][depth] = event.token_index;
    state.queue_depth[destination] = depth + 1U;
    state.queue_depth_at_enqueue[event.token_index] = depth + 1U;
    state.enqueue_cycle[event.token_index] = state.current_cycle;
    if (state.queue_depth[destination] > state.max_queue_depth[destination]) {
        state.max_queue_depth[destination] = state.queue_depth[destination];
    }
    return true;
}

bool process_credit_return(DispatchState& state, const Event& event) {
    if (state.available_credits[event.destination] >= state.max_credits[event.destination]) {
        return false;
    }
    state.available_credits[event.destination] += 1;
    return true;
}

bool process_expert_counter_return(
    DispatchState& state,
    const SmartnicTokenDescriptor* descriptors,
    const Event& event) {
    const SmartnicTokenDescriptor& token = descriptors[event.token_index];
    std::uint32_t& counter =
        state.expert_tokens_in_flight[event.destination][token.expert_id];
    if (counter == 0) {
        return false;
    }
    counter -= 1;
    return true;
}

bool process_events_at_current_cycle(
    DispatchState& state,
    const SmartnicRuntimeConfig& config,
    const SmartnicTokenDescriptor* descriptors,
    SmartnicMetrics& metrics) {
    while (state.event_count != 0 && next_event_cycle(state) == state.current_cycle) {
        Event event;
        if (!pop_next_event(state, event)) {
            return false;
        }
        if (event.type == kTokenArrival) {
            if (!process_token_arrival(state, config, descriptors, event, metrics)) {
                return false;
            }
        } else if (event.type == kTransmissionComplete) {
            state.link_busy = false;
        } else if (event.type == kCreditReturn) {
            if (!process_credit_return(state, event)) {
                return false;
            }
        } else if (event.type == kExpertCounterReturn) {
            if (!process_expert_counter_return(state, descriptors, event)) {
                return false;
            }
        } else {
            return false;
        }
    }
    return true;
}

void finalize_metrics(
    const DispatchState& state,
    const SmartnicRuntimeConfig& config,
    SmartnicMetrics& metrics) {
    metrics.total_tokens = state.record_count;
    metrics.total_packets = state.packet_count;
    metrics.final_cycle = state.final_cycle;
    metrics.max_queue_depth = 0;
    metrics.credit_stall_cycles = 0;
    metrics.counter_stall_cycles = 0;
    for (std::uint32_t destination = 0; destination < config.num_destinations; ++destination) {
        metrics.per_destination_packets[destination] = state.total_packets_sent[destination];
        metrics.per_destination_tokens[destination] = state.total_tokens_sent[destination];
        metrics.per_destination_max_queue_depth[destination] = state.max_queue_depth[destination];
        metrics.per_destination_credit_stall_cycles[destination] =
            state.credit_stall_cycles[destination];
        metrics.per_destination_counter_stall_cycles[destination] =
            state.counter_stall_cycles[destination];
        if (state.max_queue_depth[destination] > metrics.max_queue_depth) {
            metrics.max_queue_depth = state.max_queue_depth[destination];
        }
        metrics.credit_stall_cycles += state.credit_stall_cycles[destination];
        metrics.counter_stall_cycles += state.counter_stall_cycles[destination];
    }
}

int run_dispatch(
    DispatchState& state,
    const SmartnicTokenDescriptor* descriptors,
    std::uint32_t descriptor_count,
    const SmartnicRuntimeConfig& config,
    SmartnicTokenRecord* token_records,
    std::uint32_t token_record_capacity,
    SmartnicPacketRecord* packet_records,
    std::uint32_t packet_record_capacity,
    SmartnicMetrics& metrics) {
    while (state.event_count != 0 || !all_queues_empty(state, config) || state.link_busy) {
        if (!state.link_busy &&
            try_dispatch(
                state,
                config,
                descriptors,
                token_records,
                token_record_capacity,
                packet_records,
                packet_record_capacity,
                metrics)) {
            continue;
        }

        if (state.event_count == 0) {
            if (!state.link_busy && !all_queues_empty(state, config)) {
                const std::uint64_t timeout_cycle =
                    next_timeout_cycle(state, config, descriptors);
                if (timeout_cycle != kMaxCycle && timeout_cycle > state.current_cycle) {
                    account_stalls_until(state, config, descriptors, timeout_cycle);
                    state.current_cycle = timeout_cycle;
                    continue;
                }
                state.drain_mode = true;
                if (try_dispatch(
                        state,
                        config,
                        descriptors,
                        token_records,
                        token_record_capacity,
                        packet_records,
                        packet_record_capacity,
                        metrics)) {
                    state.drain_mode = false;
                    continue;
                }
                state.drain_mode = false;
            }
            if (!state.link_busy && !all_queues_empty(state, config)) {
                metrics.deadlock_errors += 1;
                return kStatusDeadlock;
            }
            continue;
        }

        const std::uint64_t event_cycle = next_event_cycle(state);
        std::uint64_t next_cycle = event_cycle;
        if (!state.link_busy) {
            const std::uint64_t timeout_cycle =
                next_timeout_cycle(state, config, descriptors);
            if (timeout_cycle < next_cycle) {
                next_cycle = timeout_cycle;
            }
        }

        if (next_cycle > state.current_cycle) {
            account_stalls_until(state, config, descriptors, next_cycle);
            state.current_cycle = next_cycle;
        }
        if (next_cycle < event_cycle) {
            continue;
        }
        if (!process_events_at_current_cycle(state, config, descriptors, metrics)) {
            return kStatusInvalidTrace;
        }
    }

    state.final_cycle = state.current_cycle;
    if (state.record_count != descriptor_count) {
        return kStatusInvalidTrace;
    }
    finalize_metrics(state, config, metrics);
    return kStatusOk;
}

} // namespace
} // namespace smartnic_fpga

extern "C" int smartnic_moe_dispatch_v0(
    const smartnic_fpga::SmartnicTokenDescriptor* descriptors,
    std::uint32_t descriptor_count,
    const smartnic_fpga::SmartnicRuntimeConfig* config,
    smartnic_fpga::SmartnicTokenRecord* token_records,
    std::uint32_t token_record_capacity,
    smartnic_fpga::SmartnicPacketRecord* packet_records,
    std::uint32_t packet_record_capacity,
    smartnic_fpga::SmartnicMetrics* metrics) {
    using namespace smartnic_fpga;

#if defined(__SYNTHESIS__) || defined(__VITIS_HLS__)
#pragma HLS INTERFACE m_axi port=descriptors offset=slave bundle=gmem0 depth=4096
#pragma HLS INTERFACE m_axi port=config offset=slave bundle=gmem1 depth=1
#pragma HLS INTERFACE m_axi port=token_records offset=slave bundle=gmem2 depth=4096
#pragma HLS INTERFACE m_axi port=packet_records offset=slave bundle=gmem3 depth=4096
#pragma HLS INTERFACE m_axi port=metrics offset=slave bundle=gmem4 depth=1
#pragma HLS INTERFACE s_axilite port=descriptors bundle=control
#pragma HLS INTERFACE s_axilite port=descriptor_count bundle=control
#pragma HLS INTERFACE s_axilite port=config bundle=control
#pragma HLS INTERFACE s_axilite port=token_records bundle=control
#pragma HLS INTERFACE s_axilite port=token_record_capacity bundle=control
#pragma HLS INTERFACE s_axilite port=packet_records bundle=control
#pragma HLS INTERFACE s_axilite port=packet_record_capacity bundle=control
#pragma HLS INTERFACE s_axilite port=metrics bundle=control
#pragma HLS INTERFACE s_axilite port=return bundle=control
#endif

    if (descriptors == nullptr || config == nullptr || token_records == nullptr ||
        packet_records == nullptr || metrics == nullptr) {
        return kStatusInvalidConfig;
    }

    static DispatchState state;
    reset_metrics(*metrics);
    if (!validate_config(*config, *metrics)) {
        metrics->status_code = kStatusInvalidConfig;
        return kStatusInvalidConfig;
    }
    if (!validate_trace(descriptors, descriptor_count, *config, *metrics)) {
        metrics->status_code = kStatusInvalidTrace;
        return kStatusInvalidTrace;
    }
    if (token_record_capacity < descriptor_count || packet_record_capacity == 0) {
        metrics->status_code = kStatusOutputOverflow;
        return kStatusOutputOverflow;
    }

    reset_state(state, *config);
    compute_effective_enqueue_cycles(state, descriptors, descriptor_count, *config);
    if (!seed_arrival_events(state, *metrics, descriptors, descriptor_count)) {
        metrics->status_code = kStatusEventOverflow;
        return kStatusEventOverflow;
    }
    const int status = run_dispatch(
        state,
        descriptors,
        descriptor_count,
        *config,
        token_records,
        token_record_capacity,
        packet_records,
        packet_record_capacity,
        *metrics);
    if (metrics->queue_overflow_errors != 0) {
        metrics->status_code = kStatusQueueOverflow;
        return kStatusQueueOverflow;
    }
    if (metrics->event_overflow_errors != 0) {
        metrics->status_code = kStatusEventOverflow;
        return kStatusEventOverflow;
    }
    if (status != kStatusOk) {
        metrics->status_code = status;
        return status;
    }
    metrics->status_code = kStatusOk;
    return kStatusOk;
}
