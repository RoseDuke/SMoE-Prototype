#include <exception>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct Row {
    std::vector<std::string> fields;
};

void print_usage(std::ostream& stream) {
    stream << "Usage: fpga_verify_against_sim --sim <tokens.csv> --hw <tokens.csv>\n";
}

std::string read_value(int& index, int argc, char** argv, const std::string& name) {
    if (index + 1 >= argc) {
        throw std::runtime_error("Missing value for " + name);
    }
    return argv[++index];
}

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

std::vector<Row> read_csv(const std::string& path, std::string& header) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("Failed to open CSV: " + path);
    }
    if (!std::getline(input, header)) {
        throw std::runtime_error("CSV is empty: " + path);
    }
    if (!header.empty() && header.back() == '\r') {
        header.pop_back();
    }
    std::vector<Row> rows;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            continue;
        }
        rows.push_back(Row{split_csv_line(line)});
    }
    return rows;
}

} // namespace

int main(int argc, char** argv) {
    std::string sim_path;
    std::string hw_path;
    try {
        for (int index = 1; index < argc; ++index) {
            const std::string argument = argv[index];
            if (argument == "--help") {
                print_usage(std::cout);
                return 0;
            }
            if (argument == "--sim") {
                sim_path = read_value(index, argc, argv, argument);
            } else if (argument == "--hw") {
                hw_path = read_value(index, argc, argv, argument);
            } else {
                throw std::runtime_error("Unknown argument: " + argument);
            }
        }
        if (sim_path.empty() || hw_path.empty()) {
            print_usage(std::cerr);
            return 1;
        }

        std::string sim_header;
        std::string hw_header;
        const std::vector<Row> sim_rows = read_csv(sim_path, sim_header);
        const std::vector<Row> hw_rows = read_csv(hw_path, hw_header);

        if (sim_header != hw_header) {
            std::cerr << "Header mismatch\n"
                      << "sim: " << sim_header << '\n'
                      << "hw:  " << hw_header << '\n';
            return 2;
        }
        if (sim_rows.size() != hw_rows.size()) {
            std::cerr << "Row count mismatch: sim=" << sim_rows.size()
                      << " hw=" << hw_rows.size() << '\n';
            return 2;
        }
        for (std::size_t row = 0; row < sim_rows.size(); ++row) {
            if (sim_rows[row].fields != hw_rows[row].fields) {
                std::cerr << "Mismatch at data row " << (row + 1U) << '\n';
                std::cerr << "sim:";
                for (const std::string& field : sim_rows[row].fields) {
                    std::cerr << ' ' << field;
                }
                std::cerr << "\nhw: ";
                for (const std::string& field : hw_rows[row].fields) {
                    std::cerr << ' ' << field;
                }
                std::cerr << '\n';
                return 2;
            }
        }
        std::cout << "FPGA V0 token records match simulator output: "
                  << sim_rows.size() << " rows\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "fpga_verify_against_sim: " << error.what() << '\n';
        return 1;
    }
}
