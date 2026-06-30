#include "simulator.hpp"

#include <algorithm>
#include <cassert>
#include <limits>
#include <stdexcept>

Simulator::Simulator(SimulatorConfig config)
    : config_(config),
      scheduler_(config.scheduling_policy) {
    validate_config();
    destinations_.resize(config_.num_destinations);
    for (DestinationState& destination : destinations_) {
        destination.available_credits = config_.initial_credits_per_destination;
        destination.max_credits = config_.initial_credits_per_destination;
    }
}

std::vector<DispatchRecord> Simulator::run(const std::vector<TokenDescriptor>& trace) {
    trace_ = trace;
    records_.clear();
    token_index_by_id_.clear();
    queue_depth_at_enqueue_by_id_.clear();
    completed_token_ids_.clear();
    credit_stalled_token_ids_.clear();
    current_cycle_ = 0;
    final_cycle_ = 0;
    next_sequence_number_ = 0;
    link_busy_until_ = 0;
    link_busy_ = false;
    drain_mode_ = false;
    events_ = std::priority_queue<Event, std::vector<Event>, EventCompare>();
    scheduler_ = Scheduler(config_.scheduling_policy);

    destinations_.assign(config_.num_destinations, DestinationState{});
    for (DestinationState& destination : destinations_) {
        destination.available_credits = config_.initial_credits_per_destination;
        destination.max_credits = config_.initial_credits_per_destination;
    }

    for (std::size_t index = 0; index < trace_.size(); ++index) {
        const TokenDescriptor& token = trace_[index];
        if (token.dst_rank >= config_.num_destinations) {
            throw std::runtime_error("Token has dst_rank outside configured num_destinations");
        }
        if (token.payload_bytes == 0) {
            throw std::runtime_error("Token has zero payload_bytes");
        }
        if (!token_index_by_id_.emplace(token.token_id, index).second) {
            throw std::runtime_error("Duplicate token_id in simulator input");
        }
        events_.push(Event{
            token.arrival_cycle,
            EventType::TokenArrival,
            token.dst_rank,
            token.token_id,
            next_sequence_number_++});
    }

    while (!events_.empty() || !all_queues_empty() || link_busy_) {
        if (!link_busy_ && try_dispatch()) {
            continue;
        }

        if (events_.empty()) {
            if (!link_busy_ && !all_queues_empty()) {
                const std::uint64_t timeout_cycle = next_timeout_cycle();
                if (timeout_cycle != std::numeric_limits<std::uint64_t>::max() &&
                    timeout_cycle > current_cycle_) {
                    account_credit_stalls_until(timeout_cycle);
                    current_cycle_ = timeout_cycle;
                    continue;
                }
                drain_mode_ = true;
                if (try_dispatch()) {
                    drain_mode_ = false;
                    continue;
                }
                drain_mode_ = false;
            }
            if (!link_busy_ && !all_queues_empty()) {
                throw std::runtime_error("Simulation deadlock: queues are non-empty but no destination can dispatch");
            }
            continue;
        }

        const std::uint64_t next_event_cycle = events_.top().cycle;
        std::uint64_t next_cycle = next_event_cycle;
        if (!link_busy_) {
            const std::uint64_t timeout_cycle = next_timeout_cycle();
            if (timeout_cycle < next_cycle) {
                next_cycle = timeout_cycle;
            }
        }

        if (next_cycle > current_cycle_) {
            account_credit_stalls_until(next_cycle);
            current_cycle_ = next_cycle;
        }

        if (next_cycle < next_event_cycle) {
            continue;
        }

        while (!events_.empty() && events_.top().cycle == current_cycle_) {
            const Event event = events_.top();
            events_.pop();
            switch (event.type) {
            case EventType::TokenArrival:
                process_token_arrival(event);
                break;
            case EventType::TransmissionComplete:
                process_transmission_complete(event);
                break;
            case EventType::CreditReturn:
                process_credit_return(event);
                break;
            }
        }
    }

    final_cycle_ = current_cycle_;
    validate_end_state(trace);
    return records_;
}

const std::vector<DestinationState>& Simulator::destination_states() const {
    return destinations_;
}

std::uint64_t Simulator::final_cycle() const {
    return final_cycle_;
}

void Simulator::process_token_arrival(const Event& event) {
    const auto iter = token_index_by_id_.find(event.token_id);
    if (iter == token_index_by_id_.end()) {
        throw std::runtime_error("TokenArrival event references unknown token_id");
    }
    const TokenDescriptor& token = trace_[iter->second];
    if (token.dst_rank >= destinations_.size()) {
        throw std::runtime_error("TokenArrival event has invalid destination");
    }
    DestinationState& destination = destinations_[token.dst_rank];
    queue_depth_at_enqueue_by_id_[token.token_id] =
        static_cast<std::uint32_t>(destination.queue.size() + 1U);
    destination.queue.push_back(token);
    destination.max_queue_depth = std::max(
        destination.max_queue_depth,
        static_cast<std::uint32_t>(destination.queue.size()));
}

void Simulator::process_transmission_complete(const Event& event) {
    (void)event;
    assert(link_busy_);
    assert(current_cycle_ >= link_busy_until_);
    link_busy_ = false;
}

void Simulator::process_credit_return(const Event& event) {
    if (event.destination >= destinations_.size()) {
        throw std::runtime_error("CreditReturn event has invalid destination");
    }
    DestinationState& destination = destinations_[event.destination];
    if (destination.available_credits >= destination.max_credits) {
        throw std::runtime_error("CreditReturn would exceed destination max credits");
    }
    destination.available_credits += 1;
    assert(destination.available_credits <= destination.max_credits);
}

bool Simulator::try_dispatch() {
    if (link_busy_) {
        return false;
    }

    std::vector<DestinationState> candidates = destinations_;
    for (std::size_t index = 0; index < candidates.size(); ++index) {
        if (!can_dispatch_destination(static_cast<std::uint32_t>(index))) {
            candidates[index].queue.clear();
        }
    }

    const int selected = scheduler_.select_destination(
        candidates,
        current_cycle_,
        config_.enable_credit_control || config_.scheduling_policy == SchedulingPolicy::CreditAware);
    if (selected < 0) {
        return false;
    }

    const std::uint32_t destination_index = static_cast<std::uint32_t>(selected);
    if (!can_dispatch_destination(destination_index)) {
        return false;
    }

    DestinationState& destination = destinations_[destination_index];
    assert(!destination.queue.empty());
    assert(!config_.enable_credit_control || destination.available_credits > 0);

    const std::uint32_t aggregation_size =
        config_.enable_aggregation
            ? std::min<std::uint32_t>(
                  config_.aggregation_threshold,
                  static_cast<std::uint32_t>(destination.queue.size()))
            : 1U;

    std::vector<TokenDescriptor> packet_tokens;
    packet_tokens.reserve(aggregation_size);
    std::uint64_t total_payload_bytes = 0;
    for (std::uint32_t count = 0; count < aggregation_size; ++count) {
        packet_tokens.push_back(destination.queue.front());
        total_payload_bytes += destination.queue.front().payload_bytes;
        destination.queue.pop_front();
    }

    if (config_.enable_credit_control) {
        destination.available_credits -= 1;
        assert(destination.available_credits <= destination.max_credits);
    }

    const std::uint64_t dispatch_cycle = current_cycle_;
    const std::uint64_t completion_cycle = dispatch_cycle + compute_service_cycles(total_payload_bytes);
    assert(completion_cycle >= dispatch_cycle);
    assert(!link_busy_);
    assert(dispatch_cycle >= link_busy_until_);

    link_busy_ = true;
    link_busy_until_ = completion_cycle;

    destination.total_packets_sent += 1;
    destination.total_tokens_sent += packet_tokens.size();

    for (const TokenDescriptor& token : packet_tokens) {
        assert(dispatch_cycle >= token.arrival_cycle);
        DispatchRecord record;
        record.token_id = token.token_id;
        record.dst_rank = token.dst_rank;
        record.arrival_cycle = token.arrival_cycle;
        record.enqueue_cycle = token.arrival_cycle;
        record.dispatch_cycle = dispatch_cycle;
        record.completion_cycle = completion_cycle;
        const auto depth_iter = queue_depth_at_enqueue_by_id_.find(token.token_id);
        if (depth_iter == queue_depth_at_enqueue_by_id_.end()) {
            throw std::runtime_error("Missing queue depth for token");
        }
        record.queue_depth_at_enqueue = depth_iter->second;
        record.aggregation_size = aggregation_size;
        record.credit_stalled =
            credit_stalled_token_ids_.find(token.token_id) != credit_stalled_token_ids_.end();
        records_.push_back(record);

        if (!completed_token_ids_.insert(token.token_id).second) {
            throw std::runtime_error("Token completed more than once");
        }
    }

    events_.push(Event{
        completion_cycle,
        EventType::TransmissionComplete,
        destination_index,
        packet_tokens.front().token_id,
        next_sequence_number_++});

    if (config_.enable_credit_control) {
        events_.push(Event{
            completion_cycle + config_.receiver_processing_cycles,
            EventType::CreditReturn,
            destination_index,
            packet_tokens.front().token_id,
            next_sequence_number_++});
    }

    return true;
}

bool Simulator::can_dispatch_destination(std::uint32_t destination_index) const {
    if (destination_index >= destinations_.size()) {
        return false;
    }
    const DestinationState& destination = destinations_[destination_index];
    if (destination.queue.empty()) {
        return false;
    }
    if (config_.enable_credit_control && destination.available_credits == 0) {
        return false;
    }
    if (!config_.enable_aggregation) {
        return true;
    }
    if (destination.queue.size() >= config_.aggregation_threshold) {
        return true;
    }
    if (drain_mode_) {
        return true;
    }
    if (config_.aggregation_timeout_cycles == 0) {
        return false;
    }
    const std::uint64_t oldest_arrival = destination.queue.front().arrival_cycle;
    return current_cycle_ >= oldest_arrival &&
           current_cycle_ - oldest_arrival >= config_.aggregation_timeout_cycles;
}

std::uint64_t Simulator::compute_service_cycles(std::uint64_t payload_bytes) const {
    assert(config_.link_bytes_per_cycle > 0);
    const std::uint64_t transfer_cycles =
        (payload_bytes + config_.link_bytes_per_cycle - 1U) / config_.link_bytes_per_cycle;
    return config_.packet_fixed_overhead_cycles + transfer_cycles;
}

std::uint64_t Simulator::next_timeout_cycle() const {
    if (!config_.enable_aggregation || config_.aggregation_timeout_cycles == 0) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    std::uint64_t best = std::numeric_limits<std::uint64_t>::max();
    for (const DestinationState& destination : destinations_) {
        if (destination.queue.empty()) {
            continue;
        }
        if (config_.enable_credit_control && destination.available_credits == 0) {
            continue;
        }
        if (destination.queue.size() >= config_.aggregation_threshold) {
            best = std::min(best, current_cycle_);
            continue;
        }
        const std::uint64_t wakeup = destination.queue.front().arrival_cycle + config_.aggregation_timeout_cycles;
        best = std::min(best, wakeup);
    }
    return best;
}

bool Simulator::all_queues_empty() const {
    for (const DestinationState& destination : destinations_) {
        if (!destination.queue.empty()) {
            return false;
        }
    }
    return true;
}

void Simulator::account_credit_stalls_until(std::uint64_t next_cycle) {
    if (!config_.enable_credit_control || next_cycle <= current_cycle_) {
        return;
    }
    const std::uint64_t delta = next_cycle - current_cycle_;
    for (DestinationState& destination : destinations_) {
        assert(destination.available_credits <= destination.max_credits);
        if (!destination.queue.empty() && destination.available_credits == 0) {
            destination.credit_stall_cycles += delta;
            for (const TokenDescriptor& token : destination.queue) {
                credit_stalled_token_ids_.insert(token.token_id);
            }
        }
    }
}

void Simulator::validate_config() const {
    if (config_.num_destinations == 0) {
        throw std::runtime_error("num_destinations must be greater than zero");
    }
    if (config_.link_bytes_per_cycle == 0) {
        throw std::runtime_error("link_bytes_per_cycle must be greater than zero");
    }
    if (config_.aggregation_threshold == 0) {
        throw std::runtime_error("aggregation_threshold must be greater than zero");
    }
    if (config_.enable_credit_control && config_.initial_credits_per_destination == 0) {
        throw std::runtime_error("initial_credits_per_destination must be greater than zero when credit control is enabled");
    }
}

void Simulator::validate_end_state(const std::vector<TokenDescriptor>& trace) const {
    assert(!link_busy_);
    assert(all_queues_empty());
    if (records_.size() != trace.size()) {
        throw std::runtime_error("Input token count does not match output record count");
    }
    for (const DestinationState& destination : destinations_) {
        if (!destination.queue.empty()) {
            throw std::runtime_error("Simulation ended with non-empty destination queue");
        }
        if (destination.available_credits > destination.max_credits) {
            throw std::runtime_error("Destination credits exceed maximum");
        }
    }
    for (const DispatchRecord& record : records_) {
        if (record.dispatch_cycle < record.arrival_cycle) {
            throw std::runtime_error("Dispatch cycle is earlier than arrival cycle");
        }
        if (record.completion_cycle < record.dispatch_cycle) {
            throw std::runtime_error("Completion cycle is earlier than dispatch cycle");
        }
    }
}
