#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>

#include <dds/dds.hpp>
#include <rti/rti.hpp>

#include "Mining/MiningMsgTypes.hpp"


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


class ControlStationProcess {
public:
    ControlStationProcess(std::string station_id)
        : station_id(station_id)
        , qos_provider(dds::core::QosProvider::Default())
        , participant(qos_provider->create_participant_from_config(
            "ControlStationParticipants::control_station"))
        , status_reader(rti::sub::find_datareader_by_name<
                            dds::sub::DataReader<Mining::MachineStatus>>(
                                participant, "control_subscriber::status_reader"))
        , position_reader(rti::sub::find_datareader_by_name<
                            dds::sub::DataReader<Mining::Position>>(
                                participant, "control_subscriber::position_reader"))
        , command_writer(rti::pub::find_datawriter_by_name<
                            dds::pub::DataWriter<Mining::Command>>(
                                participant, "control_publisher::command_writer"))
    {
        if (participant == nullptr) {
            throw std::runtime_error("Failed to create control_station participant");
        }
        if (status_reader == nullptr) {
            throw std::runtime_error("Failed to create status_reader");
        }
        if (position_reader == nullptr) {
            throw std::runtime_error("Failed to create position_reader");
        }
        if (command_writer == nullptr) {
            throw std::runtime_error("Failed to create command_writer");
        }

        msg_processor.attach_reader(
            status_reader, 
            std::bind(&ControlStationProcess::process_status, this, std::placeholders::_1));
    }

    static void preinitialize() {
        rti::domain::register_type<Mining::MachineStatus>("MachineStatusType");
        rti::domain::register_type<Mining::Position>("PositionType");
        rti::domain::register_type<Mining::Command>("CommandType");
    }

    void run() {
        participant.enable();

        while (!g_shutdown_flag) {
            Mining::Command command;
            command.control_id = station_id;
            bool valid_selection = false;

            std::cout << "\nEnter machine id or 'R' to refresh: \n";
            for (const auto& sample : rti::sub::valid_data(position_reader.read())) {
                const auto& position = sample.data();
                std::cout << "  [" << position.machine_id << "]"
                          << " pos: (" << position.latitude_deg << ", " << position.longitude_deg << ", " << position.altitude_m << ")"
                          << ", heading: " << position.heading_deg
                          << ", speed: " << position.speed_mps
                          << "\n";
            }
            std::cout << "  [R] Refresh\n";
            std::cout << "  [Q] Quit\n";
            std::cout << "Selection: " << std::flush;
            std::string machine_id_input;
            while (!g_shutdown_flag && std::getline(std::cin, machine_id_input)) {
                if (machine_id_input == "Q" || machine_id_input == "q") {
                    g_shutdown_flag = true;
                    break;
                } else if (machine_id_input == "R" || machine_id_input == "r" || machine_id_input.empty()) {
                    break;
                } else {
                    try {
                        command.machine_id = machine_id_input;
                    } catch (const std::exception& error) {
                        std::cerr << "Invalid input: " << error.what() << std::endl;
                        continue;
                    }

                    std::cout << "Enter command: \n"
                              << "  [E] Emergency Stop\n"
                              << "  [S] Start\n"
                              << "  [X] Stop\n"
                              << "  [P] Pause\n"
                              << "  [R] Resume\n"
                              << "  [T] Reset\n"
                              << "  [B] Back\n"
                              << "  [Q] Quit\n"
                              << "Selection: " << std::flush;
                    std::string command_input;
                    while (!g_shutdown_flag && std::getline(std::cin, command_input)) {
                        if (command_input == "E" || command_input == "e") {
                            command.command_type = Mining::CommandType::EMERGENCY_STOP;
                            valid_selection = true;
                        } else if (command_input == "S" || command_input == "s") {
                            command.command_type = Mining::CommandType::START;
                            valid_selection = true;
                        } else if (command_input == "X" || command_input == "x") {
                            command.command_type = Mining::CommandType::STOP;
                            valid_selection = true;
                        } else if (command_input == "P" || command_input == "p") {
                            command.command_type = Mining::CommandType::PAUSE;
                            valid_selection = true;
                        } else if (command_input == "R" || command_input == "r") {
                            command.command_type = Mining::CommandType::RESUME;
                            valid_selection = true;
                        } else if (command_input == "T" || command_input == "t") {
                            command.command_type = Mining::CommandType::RESET;
                            valid_selection = true;
                        } else if (command_input == "B" || command_input == "b") {
                            break;
                        } else if (command_input == "Q" || command_input == "q") {
                            g_shutdown_flag = true;
                            break;
                        } else {
                            std::cerr << "Invalid command: " << command_input << std::endl;
                            continue;
                        }
                        break;
                    }

                    if (valid_selection) {
                        command.issued_timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch()).count();

                        std::cout << "Issuing command: " << command << std::endl;
                        command_writer.write(command);
                    }
                }
            }
        }
    }

private:

    void process_status(const rti::sub::LoanedSample<Mining::MachineStatus>& sample) {
        if (sample.info().valid()) {
            std::cout << "Status: " << sample.data() << std::endl;
        }
    }

private:
    Mining::ComponentId station_id;
    dds::core::QosProvider qos_provider;
    dds::domain::DomainParticipant participant;
    dds::sub::DataReader<Mining::MachineStatus> status_reader;
    dds::sub::DataReader<Mining::Position> position_reader;
    dds::pub::DataWriter<Mining::Command> command_writer;
    rti::sub::SampleProcessor msg_processor;

};

int main(int argc, char **argv)
{
    const std::string station_id = argc > 1 ? argv[1] : "haul-001";

    try {
        setup_signal_handling();
        ControlStationProcess::preinitialize();
        ControlStationProcess control_station(station_id);
        control_station.run();        
    } catch (const std::exception& error) {
        std::cerr << "control_station: " << error.what() << std::endl;
        return EXIT_FAILURE;
    }
}