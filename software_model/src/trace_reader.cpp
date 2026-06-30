#include "trace_reader.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>

namespace {

constexpr const char* kExpectedHeader =
    "arrival_cycle,token_id,batch_id,layer_id,src_rank,dst_rank,expert_id,payload_bytes";

std::vector<std::string> split_csv_line(const std::string& line) {
    std::vector<std::string> fields;
    std::stringstream stream(line);
    std::string field;
    while (std::getline(stream, field, ',')) {
        fields.push_back(field);
    }
    if (!line.empty() && line.back() == ',') {
        fields.emplace_back();
    }
    return fields;
}

std::uint64_t parse_u64_field(
    const std::string& value,
    const std::string& field_name,
    std::uint64_t line_number) {
    std::size_t parsed = 0;
    unsigned long long result = 0;
    try {
        result = std::stoull(value, &parsed, 10);
    } catch (const std::exception&) {
        throw std::runtime_error(
            "Invalid numeric value for '" + field_name + "' on trace line " +
            std::to_string(line_number) + ": " + value);
    }
    if (parsed != value.size()) {
        throw std::runtime_error(
            "Invalid trailing characters for '" + field_name + "' on trace line " +
            std::to_string(line_number) + ": " + value);
    }
    return static_cast<std::uint64_t>(result);
}

std::uint32_t parse_u32_field(
    const std::string& value,
    const std::string& field_name,
    std::uint64_t line_number) {
    const std::uint64_t parsed = parse_u64_field(value, field_name, line_number);
    if (parsed > UINT32_MAX) {
        throw std::runtime_error(
            "Value for '" + field_name + "' on trace line " +
            std::to_string(line_number) + " exceeds uint32 range");
    }
    return static_cast<std::uint32_t>(parsed);
}

} // namespace

std::vector<TokenDescriptor> read_trace_csv(
    const std::string& path,
    const SimulatorConfig& config) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("Failed to open trace file: " + path);
    }

    std::string line;
    if (!std::getline(input, line)) {
        throw std::runtime_error("Trace file is empty: " + path);
    }
    if (line == std::string(kExpectedHeader) + "\r") {
        line.pop_back();
    }
    if (line != kExpectedHeader) {
        throw std::runtime_error("Trace CSV header mismatch in " + path);
    }

    std::vector<TokenDescriptor> trace;
    std::unordered_set<std::uint32_t> token_ids;
    std::uint64_t previous_arrival = 0;
    bool have_previous = false;
    std::uint64_t line_number = 1;

    while (std::getline(input, line)) {
        ++line_number;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            continue;
        }

        const std::vector<std::string> fields = split_csv_line(line);
        if (fields.size() != 8) {
            throw std::runtime_error(
                "Trace line " + std::to_string(line_number) +
                " has " + std::to_string(fields.size()) + " fields; expected 8");
        }

        TokenDescriptor token;
        token.arrival_cycle = parse_u64_field(fields[0], "arrival_cycle", line_number);
        token.token_id = parse_u32_field(fields[1], "token_id", line_number);
        token.batch_id = parse_u32_field(fields[2], "batch_id", line_number);
        token.layer_id = parse_u32_field(fields[3], "layer_id", line_number);
        token.src_rank = parse_u32_field(fields[4], "src_rank", line_number);
        token.dst_rank = parse_u32_field(fields[5], "dst_rank", line_number);
        token.expert_id = parse_u32_field(fields[6], "expert_id", line_number);
        token.payload_bytes = parse_u32_field(fields[7], "payload_bytes", line_number);

        if (token.dst_rank >= config.num_destinations) {
            throw std::runtime_error(
                "Trace line " + std::to_string(line_number) +
                " has dst_rank outside configured num_destinations");
        }
        if (token.payload_bytes == 0) {
            throw std::runtime_error(
                "Trace line " + std::to_string(line_number) +
                " has payload_bytes == 0");
        }
        if (!token_ids.insert(token.token_id).second) {
            throw std::runtime_error(
                "Duplicate token_id on trace line " + std::to_string(line_number) +
                ": " + std::to_string(token.token_id));
        }
        if (have_previous && token.arrival_cycle < previous_arrival) {
            throw std::runtime_error(
                "Trace line " + std::to_string(line_number) +
                " has decreasing arrival_cycle");
        }

        previous_arrival = token.arrival_cycle;
        have_previous = true;
        trace.push_back(token);
    }

    return trace;
}
