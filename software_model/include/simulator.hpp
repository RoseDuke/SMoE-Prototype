#pragma once

#include <cstdint>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "config.hpp"
#include "scheduler.hpp"
#include "types.hpp"

class Simulator {
public:
    explicit Simulator(SimulatorConfig config);

    std::vector<DispatchRecord> run(const std::vector<TokenDescriptor>& trace);

    const std::vector<DestinationState>& destination_states() const;

    std::uint64_t final_cycle() const;

private:
    void process_token_arrival(const Event& event);
    void process_transmission_complete(const Event& event);
    void process_credit_return(const Event& event);
    void process_expert_counter_return(const Event& event);

    bool try_dispatch();
    bool can_dispatch_destination(std::uint32_t destination) const;
    std::size_t find_dispatchable_token_index(std::uint32_t destination) const;
    bool token_has_expert_counter_slot(
        const DestinationState& destination,
        const TokenDescriptor& token) const;
    bool token_has_expert_counter_slot(
        const DestinationState& destination,
        const TokenDescriptor& token,
        const std::unordered_map<std::uint32_t, std::uint32_t>& local_counts) const;

    std::uint64_t compute_service_cycles(std::uint64_t payload_bytes) const;
    std::uint64_t token_enqueue_cycle(std::uint32_t token_id) const;
    std::uint64_t next_timeout_cycle() const;
    bool all_queues_empty() const;
    void account_credit_stalls_until(std::uint64_t next_cycle);
    void validate_config() const;
    void validate_end_state(const std::vector<TokenDescriptor>& trace) const;

    SimulatorConfig config_;
    Scheduler scheduler_;

    std::vector<TokenDescriptor> trace_;
    std::vector<DestinationState> destinations_;
    std::vector<DispatchRecord> records_;

    std::unordered_map<std::uint32_t, std::size_t> token_index_by_id_;
    std::unordered_map<std::uint32_t, std::uint32_t> queue_depth_at_enqueue_by_id_;
    std::unordered_map<std::uint32_t, std::uint64_t> enqueue_cycle_by_id_;
    std::unordered_set<std::uint32_t> completed_token_ids_;
    std::unordered_set<std::uint32_t> credit_stalled_token_ids_;
    std::unordered_set<std::uint32_t> counter_stalled_token_ids_;

    std::uint64_t current_cycle_ = 0;
    std::uint64_t final_cycle_ = 0;
    std::uint64_t next_sequence_number_ = 0;
    std::uint64_t link_busy_until_ = 0;

    bool link_busy_ = false;
    bool drain_mode_ = false;

    std::priority_queue<Event, std::vector<Event>, EventCompare> events_;
};
