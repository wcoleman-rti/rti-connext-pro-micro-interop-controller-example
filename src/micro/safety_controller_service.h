#ifndef SAFETY_CONTROLLER_SERVICE_H
#define SAFETY_CONTROLLER_SERVICE_H

#include <signal.h>

#include "rti_me_c.h"
#include "app_gen/app_gen_plugin.h"
#include "HpcSafetyTypes.h"
#include "HpcSafetyTypesSupport.h"

struct SafetyController {
    DDS_DomainParticipantFactory *factory;
    DDS_DomainParticipant *participant;
    DDS_Publisher *publisher;
    DDS_Subscriber *subscriber;
    HpcSafety_SafetyCommandDataWriter *command_writer;
    HpcSafety_HpcHealthDataReader *health_reader;
    HpcSafety_HpcTelemetryDataReader *telemetry_reader;
    HpcSafety_SafetyCommand *command;
    HpcSafety_HpcHealth *health_sample;
    HpcSafety_HpcTelemetry *telemetry_sample;
    DDS_InstanceHandle_t command_handles[2];
    DDS_WaitSet *waitset;
    DDS_StatusCondition *health_condition;
    DDS_StatusCondition *telemetry_condition;
    struct DDS_ConditionSeq active_conditions;
    RT_Registry_T *registry;
    struct APPGEN_FactoryProperty appgen_property;
    HpcSafety_SafetyCommandKind command_kind;
    DDS_Boolean appgen_registered;
    DDS_Boolean initialized;
};

DDS_ReturnCode_t safety_controller_init(
    struct SafetyController *controller,
    HpcSafety_SafetyCommandKind command_kind);
DDS_ReturnCode_t safety_controller_run(
    struct SafetyController *controller,
    const volatile sig_atomic_t *stop_requested);
void safety_controller_fini(struct SafetyController *controller);

#endif