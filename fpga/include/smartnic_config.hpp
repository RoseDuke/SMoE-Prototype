#pragma once

#include <cstdint>

namespace smartnic_fpga {

constexpr std::uint32_t kMaxTokens = 4096;
constexpr std::uint32_t kMaxPackets = kMaxTokens;
constexpr std::uint32_t kMaxDestinations = 64;
constexpr std::uint32_t kMaxExpertsPerDestination = 256;
constexpr std::uint32_t kMaxQueueDepth = 1024;
constexpr std::uint32_t kMaxAggTokens = 32;
constexpr std::uint32_t kMaxEvents = (kMaxTokens * 4U) + 64U;

enum SchedulingPolicy : std::uint32_t {
    kRoundRobin = 0,
    kOldestFirst = 1,
    kLargestQueue = 2,
    kCreditAware = 3
};

enum StatusCode : std::int32_t {
    kStatusOk = 0,
    kStatusInvalidConfig = -1,
    kStatusInvalidTrace = -2,
    kStatusQueueOverflow = -3,
    kStatusEventOverflow = -4,
    kStatusOutputOverflow = -5,
    kStatusDeadlock = -6,
    kStatusDuplicateToken = -7
};

} // namespace smartnic_fpga
