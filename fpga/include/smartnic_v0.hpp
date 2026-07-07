#pragma once

#include <cstdint>

#include "smartnic_abi.hpp"

extern "C" int smartnic_moe_dispatch_v0(
    const smartnic_fpga::SmartnicTokenDescriptor* descriptors,
    std::uint32_t descriptor_count,
    const smartnic_fpga::SmartnicRuntimeConfig* config,
    smartnic_fpga::SmartnicTokenRecord* token_records,
    std::uint32_t token_record_capacity,
    smartnic_fpga::SmartnicPacketRecord* packet_records,
    std::uint32_t packet_record_capacity,
    smartnic_fpga::SmartnicMetrics* metrics);
