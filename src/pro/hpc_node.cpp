#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>

#include <dds/dds.hpp>
#include <rti/rti.hpp>

#include "hpc_node.hpp"


inline std::atomic<bool> g_shutdown_flag{false};

inline void setup_signal_handling()
{
    struct sigaction sigact;
    sigact.sa_handler = [](int) { g_shutdown_flag = true; };
    sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = 0;
    sigaction(SIGTERM, &sigact, nullptr);
    sigaction(SIGINT, &sigact, nullptr);
}


HpcSafety::HpcId parse_identity(const std::string& value)
{
    std::size_t parsed_characters = 0;
    const unsigned long parsed = std::stoul(value, &parsed_characters, 10);
    if (parsed_characters != value.size() ||
        parsed > std::numeric_limits<HpcSafety::HpcId>::max()) {
        throw std::invalid_argument("HPC ID must be an unsigned 32-bit integer");
    }
    return static_cast<HpcSafety::HpcId>(parsed);
}

std::string HpcNode::get_participant_name(HpcSafety::HpcId id)
{
    if (id == HpcSafety::HPC_ID_A) {
        return "HpcSafetyParticipants::hpc_a";
    }
    if (id == HpcSafety::HPC_ID_B) {
        return "HpcSafetyParticipants::hpc_b";
    }
    throw std::invalid_argument("Unknown HPC ID");
}


HpcNode::HpcNode(HpcSafety::HpcId id)
    : id(id),
      qos_provider(dds::core::QosProvider::Default()),
      participant(nullptr),
      hpc_health_writer(nullptr),
      hpc_telemetry_writer(nullptr),
      safety_command_reader(nullptr)
{
    rti::domain::register_type<HpcSafety::SafetyCommand>("SafetyCommandType");
    rti::domain::register_type<HpcSafety::HpcHealth>("HpcHealthType");
    rti::domain::register_type<HpcSafety::HpcTelemetry>("HpcTelemetryType");

    participant = qos_provider->create_participant_from_config(get_participant_name(id));
    if (participant == nullptr) {
        throw std::runtime_error("Failed to create DDS participant");
    }

    hpc_health_writer = rti::pub::find_datawriter_by_name<dds::pub::DataWriter<HpcSafety::HpcHealth>>(
        participant, "hpc_publisher::health_writer");
    if (hpc_health_writer == nullptr) {
        throw std::runtime_error("Failed to create HpcHealth data writer");
    }

    hpc_telemetry_writer = rti::pub::find_datawriter_by_name<dds::pub::DataWriter<HpcSafety::HpcTelemetry>>(
        participant, "hpc_publisher::telemetry_writer");
    if (hpc_telemetry_writer == nullptr) {
        throw std::runtime_error("Failed to create HpcTelemetry data writer");
    }

    safety_command_reader = rti::sub::find_datareader_by_name<dds::sub::DataReader<HpcSafety::SafetyCommand>>(
        participant, "hpc_subscriber::command_reader");
    if (safety_command_reader == nullptr) {
        throw std::runtime_error("Failed to create SafetyCommand data reader");
    }

    auto cft = dds::core::polymorphic_cast<
                dds::topic::ContentFilteredTopic<HpcSafety::SafetyCommand>>(
                    safety_command_reader.topic_description());
    std::vector<std::string> hpc_ids = {std::to_string(id)};
    cft.filter_parameters(hpc_ids.begin(), hpc_ids.end());

    msg_processor.attach_reader(safety_command_reader, 
        std::bind(&HpcNode::process_command, this, std::placeholders::_1));

    
    health.hpc_id = id;
    health.status_sequence = 0;
    health.state = HpcSafety::HpcHealthState::HPC_HEALTH_READY;
    health.fault_code = 0;
    health.uptime_ms = 0;
    health.last_command_sequence_applied = 0;

    telemetry.hpc_id = id;
    telemetry.telemetry_sequence = 0;
    telemetry.cpu_load_percent = 0.0F;
    telemetry.application_cycle_time_us = 500000U;
    telemetry.completed_cycles = 0;
}

void HpcNode::run()
{
    setup_signal_handling();

    const auto start_time = std::chrono::steady_clock::now();
    auto next_report = start_time;
    auto next_command_diagnostics = start_time;
    auto previous_cpu_time = std::clock();
    auto previous_wall_time = start_time;

    participant.enable();

    std::cout << "HPC " << id << " started" << std::endl;

    while (!g_shutdown_flag) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= next_report) {
            const auto wall_interval =
                std::chrono::duration<double>(now - previous_wall_time).count();
            const std::clock_t cpu_now = std::clock();
            if (wall_interval > 0.0 && cpu_now >= previous_cpu_time) {
                const double cpu_interval = static_cast<double>(
                    cpu_now - previous_cpu_time) / CLOCKS_PER_SEC;
                telemetry.cpu_load_percent = static_cast<float>(
                    std::min(100.0, 100.0 * cpu_interval / wall_interval));
            }
            previous_cpu_time = cpu_now;
            previous_wall_time = now;

            {
                std::lock_guard<std::mutex> lock(state_mutex);
                ++health.status_sequence;
                health.uptime_ms = static_cast<std::uint32_t>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        now - start_time).count());
                telemetry.telemetry_sequence++;
                telemetry.completed_cycles++;
                hpc_health_writer.write(health);
                hpc_telemetry_writer.write(telemetry);
            }
            next_report = now + std::chrono::milliseconds(500);
        }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    {
        std::lock_guard<std::mutex> lock(state_mutex);
        health.state = HpcSafety::HpcHealthState::HPC_HEALTH_OFFLINE;
        ++health.status_sequence;
        hpc_health_writer.write(health);
    }
    std::cout << "HPC " << id << " shutting down\n";
}

void HpcNode::process_command(const rti::sub::LoanedSample<HpcSafety::SafetyCommand>& sample)
{
    if (sample.info().valid()) {
        const HpcSafety::SafetyCommand& command = sample.data();
        // Process the command here
        std::cout << "[" << id << "] SafetyCommand received: " << command << std::endl;
        std::lock_guard<std::mutex> lock(state_mutex);
        switch (command.command) {
            case HpcSafety::SafetyCommandKind::SAFETY_COMMAND_ENABLE:
                health.state = HpcSafety::HpcHealthState::HPC_HEALTH_ACTIVE;
                break;
            case HpcSafety::SafetyCommandKind::SAFETY_COMMAND_DISABLE:
            case HpcSafety::SafetyCommandKind::SAFETY_COMMAND_RESET:
                health.state = HpcSafety::HpcHealthState::HPC_HEALTH_READY;
                break;
            case HpcSafety::SafetyCommandKind::SAFETY_COMMAND_NOOP:
                break;
        }
        health.last_command_sequence_applied = command.command_sequence;
    }
}


int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <HpcId>\n";
        return EXIT_FAILURE;
    }

    try {
        HpcSafety::HpcId hpc_id = static_cast<HpcSafety::HpcId>(std::stoi(argv[1]));
        HpcNode node(hpc_id);
        node.run();
    } catch (const std::exception& error) {
        std::cerr << "HPC Node Error: " << error.what() << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}