#include <exception>
#include <iostream>
#include <string>

#include "config.hpp"
#include "metrics.hpp"
#include "simulator.hpp"
#include "trace_reader.hpp"

namespace {

void print_usage(std::ostream& stream) {
    stream << "Usage: smartnic_ref --trace <path> --config <path> "
           << "--output <path> --summary <path>\n";
}

} // namespace

int main(int argc, char** argv) {
    std::string trace_path;
    std::string config_path;
    std::string output_path;
    std::string summary_path;

    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--help") {
            print_usage(std::cout);
            return 0;
        }
        auto read_value = [&](const std::string& name, std::string& target) {
            if (index + 1 >= argc) {
                throw std::runtime_error("Missing value for " + name);
            }
            target = argv[++index];
        };

        if (argument == "--trace") {
            read_value(argument, trace_path);
        } else if (argument == "--config") {
            read_value(argument, config_path);
        } else if (argument == "--output") {
            read_value(argument, output_path);
        } else if (argument == "--summary") {
            read_value(argument, summary_path);
        } else {
            std::cerr << "Unknown argument: " << argument << '\n';
            print_usage(std::cerr);
            return 1;
        }
    }

    if (trace_path.empty() || config_path.empty() || output_path.empty() || summary_path.empty()) {
        std::cerr << "Missing required arguments\n";
        print_usage(std::cerr);
        return 1;
    }

    try {
        const SimulatorConfig config = read_config_file(config_path);
        const std::vector<TokenDescriptor> trace = read_trace_csv(trace_path, config);
        Simulator simulator(config);
        const std::vector<DispatchRecord> records = simulator.run(trace);
        write_records_csv(output_path, records);
        const SummaryMetrics metrics =
            compute_summary_metrics(records, simulator.destination_states(), simulator.final_cycle());
        write_summary_file(summary_path, metrics, simulator.destination_states());
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
