#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <unistd.h>

#include "config.hpp"
#include "scheduler.hpp"
#include "simulator.hpp"
#include "trace_reader.hpp"

namespace {

const std::string kHeader =
    "arrival_cycle,token_id,batch_id,layer_id,src_rank,dst_rank,expert_id,payload_bytes\n";

std::string make_temp_path(const std::string& name) {
    return std::string("/tmp/smartnic_ref_") + name + "_" + std::to_string(::getpid()) + ".csv";
}

void write_file(const std::string& path, const std::string& contents) {
    std::ofstream output(path);
    if (!output) {
        throw std::runtime_error("failed to write temp file");
    }
    output << contents;
}

bool throws_runtime_error(void (*fn)()) {
    try {
        fn();
    } catch (const std::runtime_error&) {
        return true;
    }
    return false;
}

SimulatorConfig baseline_config() {
    SimulatorConfig config;
    config.num_destinations = 4;
    config.initial_credits_per_destination = 16;
    config.link_bytes_per_cycle = 32;
    config.packet_fixed_overhead_cycles = 20;
    config.receiver_processing_cycles = 100;
    config.scheduling_policy = SchedulingPolicy::RoundRobin;
    config.enable_credit_control = false;
    config.enable_aggregation = false;
    return config;
}

std::vector<TokenDescriptor> uniform_trace() {
    return {
        TokenDescriptor{0, 0, 0, 0, 0, 0, 0, 64},
        TokenDescriptor{0, 1, 0, 0, 0, 1, 1, 64},
        TokenDescriptor{0, 2, 0, 0, 0, 2, 2, 64},
        TokenDescriptor{0, 3, 0, 0, 0, 3, 3, 64},
    };
}

std::vector<DispatchRecord> sorted_records(std::vector<DispatchRecord> records) {
    std::sort(records.begin(), records.end(), [](const DispatchRecord& lhs, const DispatchRecord& rhs) {
        return lhs.token_id < rhs.token_id;
    });
    return records;
}

void test_parser_accepts_valid_csv() {
    const std::string path = make_temp_path("valid");
    write_file(path, kHeader + "0,0,0,0,0,1,3,4096\n2,1,0,0,0,2,7,4096\n");
    const std::vector<TokenDescriptor> trace = read_trace_csv(path, baseline_config());
    assert(trace.size() == 2);
    assert(trace[0].token_id == 0);
    assert(trace[1].dst_rank == 2);
}

void malformed_row_case() {
    const std::string path = make_temp_path("malformed");
    write_file(path, kHeader + "0,0,0\n");
    (void)read_trace_csv(path, baseline_config());
}

void duplicate_token_case() {
    const std::string path = make_temp_path("duplicate");
    write_file(path, kHeader + "0,0,0,0,0,1,3,64\n1,0,0,0,0,2,7,64\n");
    (void)read_trace_csv(path, baseline_config());
}

void invalid_destination_case() {
    const std::string path = make_temp_path("bad_dst");
    write_file(path, kHeader + "0,0,0,0,0,9,3,64\n");
    (void)read_trace_csv(path, baseline_config());
}

void test_parser_rejects_bad_inputs() {
    assert(throws_runtime_error(malformed_row_case));
    assert(throws_runtime_error(duplicate_token_case));
    assert(throws_runtime_error(invalid_destination_case));
}

void test_scheduler_policies() {
    std::vector<DestinationState> destinations(4);
    destinations[0].queue.push_back(TokenDescriptor{10, 0, 0, 0, 0, 0, 0, 64});
    destinations[1].queue.push_back(TokenDescriptor{3, 1, 0, 0, 0, 1, 0, 64});
    destinations[2].queue.push_back(TokenDescriptor{6, 2, 0, 0, 0, 2, 0, 64});
    destinations[2].queue.push_back(TokenDescriptor{7, 3, 0, 0, 0, 2, 0, 64});
    for (DestinationState& destination : destinations) {
        destination.available_credits = 1;
        destination.max_credits = 1;
    }

    Scheduler round_robin(SchedulingPolicy::RoundRobin);
    assert(round_robin.select_destination(destinations, 20, false) == 0);
    assert(round_robin.select_destination(destinations, 20, false) == 1);

    Scheduler oldest_first(SchedulingPolicy::OldestFirst);
    assert(oldest_first.select_destination(destinations, 20, false) == 1);

    Scheduler largest_queue(SchedulingPolicy::LargestQueue);
    assert(largest_queue.select_destination(destinations, 20, false) == 2);

    destinations[1].available_credits = 0;
    Scheduler credit_aware(SchedulingPolicy::CreditAware);
    assert(credit_aware.select_destination(destinations, 20, true) != 1);
}

void test_round_robin_order_and_service_cycles() {
    Simulator simulator(baseline_config());
    const std::vector<DispatchRecord> records = sorted_records(simulator.run(uniform_trace()));
    assert(records.size() == 4);
    assert(records[0].dispatch_cycle == 0);
    assert(records[0].completion_cycle == 22);
    assert(records[1].dispatch_cycle == 22);
    assert(records[2].dispatch_cycle == 44);
    assert(records[3].dispatch_cycle == 66);
    assert(simulator.final_cycle() == 88);
}

void test_credit_flow_control() {
    SimulatorConfig config = baseline_config();
    config.initial_credits_per_destination = 2;
    config.receiver_processing_cycles = 1000;
    config.enable_credit_control = true;
    config.scheduling_policy = SchedulingPolicy::RoundRobin;

    std::vector<TokenDescriptor> trace;
    for (std::uint32_t token = 0; token < 6; ++token) {
        trace.push_back(TokenDescriptor{0, token, 0, 0, 0, 0, token, 64});
    }

    Simulator simulator(config);
    const std::vector<DispatchRecord> records = simulator.run(trace);
    assert(records.size() == trace.size());
    assert(simulator.destination_states()[0].available_credits <= simulator.destination_states()[0].max_credits);
    assert(simulator.destination_states()[0].available_credits == simulator.destination_states()[0].max_credits);
    assert(simulator.destination_states()[0].credit_stall_cycles > 0);

    bool saw_stalled_record = false;
    for (const DispatchRecord& record : records) {
        assert(record.dispatch_cycle >= record.arrival_cycle);
        assert(record.completion_cycle >= record.dispatch_cycle);
        saw_stalled_record = saw_stalled_record || record.credit_stalled;
    }
    assert(saw_stalled_record);
}

void test_aggregation_reduces_packet_count() {
    SimulatorConfig config = baseline_config();
    config.enable_aggregation = true;
    config.aggregation_threshold = 4;
    config.aggregation_timeout_cycles = 0;

    std::vector<TokenDescriptor> trace;
    for (std::uint32_t token = 0; token < 8; ++token) {
        trace.push_back(TokenDescriptor{0, token, 0, 0, 0, 0, token, 64});
    }

    Simulator simulator(config);
    const std::vector<DispatchRecord> records = simulator.run(trace);
    assert(records.size() == trace.size());
    assert(simulator.destination_states()[0].total_packets_sent == 2);
    for (const DispatchRecord& record : records) {
        assert(record.aggregation_size == 4);
    }
}

void test_aggregation_timeout_drains_partial_packet() {
    SimulatorConfig config = baseline_config();
    config.enable_aggregation = true;
    config.aggregation_threshold = 4;
    config.aggregation_timeout_cycles = 50;

    std::vector<TokenDescriptor> trace = {
        TokenDescriptor{0, 0, 0, 0, 0, 0, 0, 64},
        TokenDescriptor{0, 1, 0, 0, 0, 0, 1, 64},
    };

    Simulator simulator(config);
    const std::vector<DispatchRecord> records = simulator.run(trace);
    assert(records.size() == trace.size());
    assert(simulator.destination_states()[0].total_packets_sent == 1);
    for (const DispatchRecord& record : records) {
        assert(record.dispatch_cycle == 50);
        assert(record.aggregation_size == 2);
    }
}

void test_end_state_and_determinism() {
    SimulatorConfig config = baseline_config();
    config.scheduling_policy = SchedulingPolicy::LargestQueue;
    std::vector<TokenDescriptor> trace = {
        TokenDescriptor{0, 0, 0, 0, 0, 0, 0, 64},
        TokenDescriptor{0, 1, 0, 0, 0, 0, 1, 64},
        TokenDescriptor{0, 2, 0, 0, 0, 1, 2, 64},
        TokenDescriptor{5, 3, 0, 0, 0, 2, 3, 128},
    };

    Simulator first(config);
    Simulator second(config);
    const std::vector<DispatchRecord> first_records = sorted_records(first.run(trace));
    const std::vector<DispatchRecord> second_records = sorted_records(second.run(trace));
    assert(first_records.size() == trace.size());
    assert(second_records.size() == trace.size());
    assert(first.final_cycle() == second.final_cycle());
    for (std::size_t index = 0; index < first_records.size(); ++index) {
        assert(first_records[index].token_id == second_records[index].token_id);
        assert(first_records[index].dispatch_cycle == second_records[index].dispatch_cycle);
        assert(first_records[index].completion_cycle == second_records[index].completion_cycle);
        assert(first_records[index].dispatch_cycle >= first_records[index].arrival_cycle);
        assert(first_records[index].completion_cycle >= first_records[index].dispatch_cycle);
    }
    for (const DestinationState& destination : first.destination_states()) {
        assert(destination.queue.empty());
    }
}

} // namespace

int main() {
    test_parser_accepts_valid_csv();
    test_parser_rejects_bad_inputs();
    test_scheduler_policies();
    test_round_robin_order_and_service_cycles();
    test_credit_flow_control();
    test_aggregation_reduces_packet_count();
    test_aggregation_timeout_drains_partial_packet();
    test_end_state_and_determinism();

    std::cout << "All reference model tests passed\n";
    return 0;
}
