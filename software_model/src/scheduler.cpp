#include "scheduler.hpp"

#include <limits>

Scheduler::Scheduler(SchedulingPolicy policy)
    : policy_(policy) {}

int Scheduler::select_destination(
    const std::vector<DestinationState>& destinations,
    std::uint64_t current_cycle,
    bool credit_control_enabled) {
    if (destinations.empty()) {
        return -1;
    }

    switch (policy_) {
    case SchedulingPolicy::RoundRobin: {
        for (std::size_t offset = 0; offset < destinations.size(); ++offset) {
            const std::uint32_t index =
                static_cast<std::uint32_t>((next_round_robin_destination_ + offset) % destinations.size());
            const DestinationState& destination = destinations[index];
            if (!destination.queue.empty() &&
                (!credit_control_enabled || destination.available_credits > 0)) {
                next_round_robin_destination_ =
                    static_cast<std::uint32_t>((index + 1U) % destinations.size());
                return static_cast<int>(index);
            }
        }
        return -1;
    }
    case SchedulingPolicy::OldestFirst: {
        std::uint64_t best_arrival = std::numeric_limits<std::uint64_t>::max();
        int best_destination = -1;
        for (std::size_t index = 0; index < destinations.size(); ++index) {
            const DestinationState& destination = destinations[index];
            if (destination.queue.empty()) {
                continue;
            }
            if (credit_control_enabled && destination.available_credits == 0) {
                continue;
            }
            const std::uint64_t arrival = destination.queue.front().arrival_cycle;
            if (arrival < best_arrival) {
                best_arrival = arrival;
                best_destination = static_cast<int>(index);
            }
        }
        return best_destination;
    }
    case SchedulingPolicy::LargestQueue: {
        std::size_t best_size = 0;
        int best_destination = -1;
        for (std::size_t index = 0; index < destinations.size(); ++index) {
            const DestinationState& destination = destinations[index];
            if (destination.queue.empty()) {
                continue;
            }
            if (credit_control_enabled && destination.available_credits == 0) {
                continue;
            }
            if (destination.queue.size() > best_size) {
                best_size = destination.queue.size();
                best_destination = static_cast<int>(index);
            }
        }
        return best_destination;
    }
    case SchedulingPolicy::CreditAware: {
        std::uint64_t best_wait = 0;
        int best_destination = -1;
        for (std::size_t index = 0; index < destinations.size(); ++index) {
            const DestinationState& destination = destinations[index];
            if (destination.queue.empty() || destination.available_credits == 0) {
                continue;
            }
            const std::uint64_t arrival = destination.queue.front().arrival_cycle;
            const std::uint64_t wait = current_cycle >= arrival ? current_cycle - arrival : 0;
            if (best_destination < 0 || wait > best_wait) {
                best_wait = wait;
                best_destination = static_cast<int>(index);
            }
        }
        return best_destination;
    }
    }

    return -1;
}
