#pragma once

#include <string>
#include <vector>

#include "smartnic_abi.hpp"

namespace smartnic_fpga {

SmartnicRuntimeConfig read_runtime_config(const std::string& path);

std::vector<SmartnicTokenDescriptor> read_trace_csv(
    const std::string& path,
    const SmartnicRuntimeConfig& config);

void write_token_records_csv(
    const std::string& path,
    const std::vector<SmartnicTokenRecord>& records);

void write_packet_records_csv(
    const std::string& path,
    const std::vector<SmartnicPacketRecord>& records);

void write_summary_file(
    const std::string& path,
    const SmartnicMetrics& metrics,
    const std::vector<SmartnicTokenRecord>& records,
    std::uint32_t num_destinations);

void write_metrics_json(
    const std::string& path,
    const SmartnicMetrics& metrics,
    std::uint32_t num_destinations);

} // namespace smartnic_fpga
