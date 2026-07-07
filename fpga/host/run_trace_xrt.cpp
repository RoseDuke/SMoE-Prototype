#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "load_trace.hpp"
#include "smartnic_config.hpp"

#include "xrt/xrt_bo.h"
#include "xrt/xrt_device.h"
#include "xrt/xrt_kernel.h"

namespace {

void print_usage(std::ostream& stream) {
    stream << "Usage: fpga_run_trace_xrt --xclbin <path> --trace <csv> "
           << "--config <cfg> --out <dir> [--device <index>] [--kernel <name>]\n";
}

std::string read_value(int& index, int argc, char** argv, const std::string& name) {
    if (index + 1 >= argc) {
        throw std::runtime_error("Missing value for " + name);
    }
    return argv[++index];
}

std::uint32_t parse_u32(const std::string& name, const std::string& value) {
    std::size_t parsed = 0;
    unsigned long result = 0;
    try {
        result = std::stoul(value, &parsed, 10);
    } catch (const std::exception&) {
        throw std::runtime_error("Invalid integer for " + name + ": " + value);
    }
    if (parsed != value.size() || result > UINT32_MAX) {
        throw std::runtime_error("Invalid integer for " + name + ": " + value);
    }
    return static_cast<std::uint32_t>(result);
}

} // namespace

int main(int argc, char** argv) {
    std::string xclbin_path;
    std::string trace_path;
    std::string config_path;
    std::string output_dir;
    std::string kernel_name = "smartnic_moe_dispatch_v0";
    std::uint32_t device_index = 0;

    try {
        for (int index = 1; index < argc; ++index) {
            const std::string argument = argv[index];
            if (argument == "--help") {
                print_usage(std::cout);
                return 0;
            }
            if (argument == "--xclbin") {
                xclbin_path = read_value(index, argc, argv, argument);
            } else if (argument == "--trace") {
                trace_path = read_value(index, argc, argv, argument);
            } else if (argument == "--config") {
                config_path = read_value(index, argc, argv, argument);
            } else if (argument == "--out") {
                output_dir = read_value(index, argc, argv, argument);
            } else if (argument == "--device") {
                device_index = parse_u32(argument, read_value(index, argc, argv, argument));
            } else if (argument == "--kernel") {
                kernel_name = read_value(index, argc, argv, argument);
            } else {
                throw std::runtime_error("Unknown argument: " + argument);
            }
        }

        if (xclbin_path.empty() || trace_path.empty() ||
            config_path.empty() || output_dir.empty()) {
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

        xrt::device device(device_index);
        const xrt::uuid xclbin_uuid = device.load_xclbin(xclbin_path);
        xrt::kernel kernel(device, xclbin_uuid, kernel_name);

        const std::size_t descriptor_bytes =
            trace.size() * sizeof(smartnic_fpga::SmartnicTokenDescriptor);
        const std::size_t config_bytes = sizeof(smartnic_fpga::SmartnicRuntimeConfig);
        const std::size_t token_record_bytes =
            token_records.size() * sizeof(smartnic_fpga::SmartnicTokenRecord);
        const std::size_t packet_record_bytes =
            packet_records.size() * sizeof(smartnic_fpga::SmartnicPacketRecord);
        const std::size_t metrics_bytes = sizeof(smartnic_fpga::SmartnicMetrics);

        xrt::bo descriptors_bo(device, descriptor_bytes, kernel.group_id(0));
        xrt::bo config_bo(device, config_bytes, kernel.group_id(2));
        xrt::bo token_records_bo(device, token_record_bytes, kernel.group_id(3));
        xrt::bo packet_records_bo(device, packet_record_bytes, kernel.group_id(5));
        xrt::bo metrics_bo(device, metrics_bytes, kernel.group_id(7));

        descriptors_bo.write(trace.data(), descriptor_bytes, 0);
        config_bo.write(&config, config_bytes, 0);
        token_records_bo.write(token_records.data(), token_record_bytes, 0);
        packet_records_bo.write(packet_records.data(), packet_record_bytes, 0);
        metrics_bo.write(&metrics, metrics_bytes, 0);

        descriptors_bo.sync(XCL_BO_SYNC_BO_TO_DEVICE, descriptor_bytes, 0);
        config_bo.sync(XCL_BO_SYNC_BO_TO_DEVICE, config_bytes, 0);
        token_records_bo.sync(XCL_BO_SYNC_BO_TO_DEVICE, token_record_bytes, 0);
        packet_records_bo.sync(XCL_BO_SYNC_BO_TO_DEVICE, packet_record_bytes, 0);
        metrics_bo.sync(XCL_BO_SYNC_BO_TO_DEVICE, metrics_bytes, 0);

        xrt::run run = kernel(
            descriptors_bo,
            static_cast<std::uint32_t>(trace.size()),
            config_bo,
            token_records_bo,
            static_cast<std::uint32_t>(token_records.size()),
            packet_records_bo,
            static_cast<std::uint32_t>(packet_records.size()),
            metrics_bo);
        run.wait();

        metrics_bo.sync(XCL_BO_SYNC_BO_FROM_DEVICE, metrics_bytes, 0);
        metrics_bo.read(&metrics, metrics_bytes, 0);
        if (metrics.status_code != smartnic_fpga::kStatusOk) {
            std::cerr << "FPGA kernel reported status " << metrics.status_code << '\n';
            return 2;
        }
        if (metrics.total_tokens > token_records.size() ||
            metrics.total_packets > packet_records.size()) {
            std::cerr << "FPGA kernel reported output counts beyond host buffer capacity\n";
            return 2;
        }

        token_records_bo.sync(XCL_BO_SYNC_BO_FROM_DEVICE, token_record_bytes, 0);
        packet_records_bo.sync(XCL_BO_SYNC_BO_FROM_DEVICE, packet_record_bytes, 0);
        token_records_bo.read(token_records.data(), token_record_bytes, 0);
        packet_records_bo.read(packet_records.data(), packet_record_bytes, 0);

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

        std::cout << "Wrote XRT FPGA V0 trace results to " << output_dir << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "fpga_run_trace_xrt: " << error.what() << '\n';
        return 1;
    }
}
