#pragma once

#include <cstdint>
#include <vector>

#include "config.hpp"
#include "types.hpp"

class Scheduler {
public:
    explicit Scheduler(SchedulingPolicy policy);

    int select_destination(
        const std::vector<DestinationState>& destinations,
        std::uint64_t current_cycle,
        bool credit_control_enabled);

private:
    SchedulingPolicy policy_;
    std::uint32_t next_round_robin_destination_ = 0;
};
