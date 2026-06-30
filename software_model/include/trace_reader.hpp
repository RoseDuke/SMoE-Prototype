#pragma once

#include <string>
#include <vector>

#include "config.hpp"
#include "types.hpp"

std::vector<TokenDescriptor> read_trace_csv(
    const std::string& path,
    const SimulatorConfig& config);
