#pragma once

#include <cstdint>
#include <deque>

struct TokenDescriptor {
    std::uint64_t arrival_cycle = 0;
    std::uint32_t token_id = 0;
    std::uint32_t batch_id = 0;
    std::uint32_t layer_id = 0;
    std::uint32_t src_rank = 0;
    std::uint32_t dst_rank = 0;
    std::uint32_t expert_id = 0;
    std::uint32_t payload_bytes = 0;
};

struct DispatchRecord {
    std::uint32_t token_id = 0;
    std::uint32_t dst_rank = 0;

    std::uint64_t arrival_cycle = 0;
    std::uint64_t enqueue_cycle = 0;
    std::uint64_t dispatch_cycle = 0;
    std::uint64_t completion_cycle = 0;

    std::uint32_t queue_depth_at_enqueue = 0;
    std::uint32_t aggregation_size = 1;

    bool credit_stalled = false;
};

struct DestinationState {
    std::deque<TokenDescriptor> queue;

    std::uint32_t available_credits = 0;
    std::uint32_t max_credits = 0;

    std::uint64_t total_tokens_sent = 0;
    std::uint64_t total_packets_sent = 0;
    std::uint64_t credit_stall_cycles = 0;

    std::uint32_t max_queue_depth = 0;
};

enum class EventType {
    TokenArrival,
    TransmissionComplete,
    CreditReturn
};

struct Event {
    std::uint64_t cycle = 0;
    EventType type = EventType::TokenArrival;

    std::uint32_t destination = 0;
    std::uint32_t token_id = 0;

    std::uint64_t sequence_number = 0;
};

inline int event_type_priority(EventType type) {
    switch (type) {
    case EventType::TransmissionComplete:
        return 0;
    case EventType::CreditReturn:
        return 1;
    case EventType::TokenArrival:
        return 2;
    }
    return 3;
}

struct EventCompare {
    bool operator()(const Event& lhs, const Event& rhs) const {
        if (lhs.cycle != rhs.cycle) {
            return lhs.cycle > rhs.cycle;
        }
        const int lhs_priority = event_type_priority(lhs.type);
        const int rhs_priority = event_type_priority(rhs.type);
        if (lhs_priority != rhs_priority) {
            return lhs_priority > rhs_priority;
        }
        return lhs.sequence_number > rhs.sequence_number;
    }
};
