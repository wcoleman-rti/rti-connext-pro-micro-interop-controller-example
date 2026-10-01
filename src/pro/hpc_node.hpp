#pragma once

#include <mutex>
#include <string>

#include <dds/dds.hpp>
#include <rti/rti.hpp>

#include "HpcSafety/HpcSafetyTypes.hpp"


class HpcNode {
public:
    HpcNode(HpcSafety::HpcId id);

    void run();

private:
    void process_command(const rti::sub::LoanedSample<HpcSafety::SafetyCommand>& sample);

    static std::string get_participant_name(HpcSafety::HpcId id);

private:
    HpcSafety::HpcId id;
    HpcSafety::HpcHealth health;
    HpcSafety::HpcTelemetry telemetry;
    std::mutex state_mutex;

    dds::core::QosProvider qos_provider;
    dds::domain::DomainParticipant participant;
    dds::pub::DataWriter<HpcSafety::HpcHealth> hpc_health_writer;
    dds::pub::DataWriter<HpcSafety::HpcTelemetry> hpc_telemetry_writer;
    dds::sub::DataReader<HpcSafety::SafetyCommand> safety_command_reader;
    rti::sub::SampleProcessor msg_processor;
};