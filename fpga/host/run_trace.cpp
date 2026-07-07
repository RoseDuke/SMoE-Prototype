#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "load_trace.hpp"
#include "smartnic_v0.hpp"

namespace {

void print_usage(std::ostream& stream) {
    stream << "Usage: fpga_run_trace --trace <csv> --config <cfg> --out <dir>\n";
}

std::string read_value(int& index, int argc, char** argv, const std::string& name) {
    if (index + 1 >= argc) {
        throw std::runtime_error("Missing value for " + name);
    }
    return argv[++index];
}

} // namespace

int main(int argc, char** argv) {
    std::string trace_path;
    std::string config_path;
    std::string output_dir;

    try {
        for (int index = 1; index < argc; ++index) {
            const std::string argument = argv[index];
            if (argument == "--help") {
                print_usage(std::cout);
                return 0;
            }
            if (argument == "--trace") {
                trace_path = read_value(index, argc, argv, argument);
            } else if (argument == "--config") {
                config_path = read_value(index, argc, argv, argument);
            } else if (argument == "--out") {
                output_dir = read_value(index, argc, argv, argument);
            } else {
                throw std::runtime_error("Unknown argument: " + argument);
            }
        }

        if (trace_path.empty() || config_path.empty() || output_dir.empty()) {
            print_usage(std::cerr);
            return 1;
        }

        const smartnic_fpga::SmartnicRuntimeConfig config =
            smartnic_fpga::read_runtime_config(config_path);
        const std::vector<smartnic_fpga::SmartnicTokenDescriptor> trace =
            smartnic_fpga::read_trace_csv(trace_path, config);

        std::vector<smartnic_fpga::SmartnicTokenRecord> token_records(trace.size());
        std::vector<smartnic_fpga::SmartnicPacketRecord> packet_records(smartnic_fpga::kMaxPackets);
        smartnic_fpga::SmartnicMetrics metrics;

        const int status = smartnic_moe_dispatch_v0(
            trace.data(),
            static_cast<std::uint32_t>(trace.size()),
            &config,
            token_records.data(),
            static_cast<std::uint32_t>(token_records.size()),
            packet_records.data(),
            static_cast<std::uint32_t>(packet_records.size()),
            &metrics);
        if (status != smartnic_fpga::kStatusOk) {
            std::cerr << "smartnic_moe_dispatch_v0 failed with status " << status << '\n';
            return 2;
        }

        token_records.resize(static_cast<std::size_t>(metrics.total_tokens));
        packet_records.resize(static_cast<std::size_t>(metrics.total_packets));

        std::filesystem::create_directories(output_dir);
        smartnic_fpga::write_token_records_csv(output_dir + "/tokens.csv", token_records);
        smartnic_fpga::write_packet_records_csv(output_dir + "/packets.csv", packet_records);
        smartnic_fpga::write_summary_file(
            output_dir + "/summary.txt",
            metrics,
            token_records,
            config.num_destinations);
        smartnic_fpga::write_metrics_json(
            output_dir + "/metrics.json",
            metrics,
            config.num_destinations);

        std::cout << "Wrote FPGA V0 trace results to " << output_dir << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "fpga_run_trace: " << error.what() << '\n';
        return 1;
    }
}
