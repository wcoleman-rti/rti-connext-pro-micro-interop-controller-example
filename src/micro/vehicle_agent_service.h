#ifndef VEHICLE_AGENT_SERVICE_H
#define VEHICLE_AGENT_SERVICE_H

#include <stdint.h>

#include "rti_me_c.h"
#include "app_gen/app_gen_plugin.h"
#include "MiningMsgTypes.h"
#include "MiningMsgTypesSupport.h"

struct VehicleAgent {
    DDS_DomainParticipantFactory *factory;
    DDS_DomainParticipant *participant;
    DDS_Publisher *publisher;
    DDS_Subscriber *subscriber;
    Mining_MachineStatusDataWriter *status_writer;
    Mining_PositionDataWriter *position_writer;
    Mining_CommandDataReader *command_reader;
    Mining_MachineStatus *status;
    Mining_Position *position;
    char status_message_buffer[129];
    DDS_InstanceHandle_t status_handle;
    DDS_InstanceHandle_t position_handle;
    DDS_WaitSet *waitset;
    DDS_GuardCondition *shutdown_condition;
    DDS_StatusCondition *command_condition;
    struct DDS_ConditionSeq active_conditions;
    RT_Registry_T *registry;
    struct APPGEN_FactoryProperty appgen_property;
    uint64_t next_position_release_ns;
    DDS_Boolean appgen_registered;
    DDS_Boolean initialized;
    DDS_Boolean started;
};

/* The owner calls init, start, run, then fini; stop may be requested during run. */
DDS_ReturnCode_t vehicle_agent_init(struct VehicleAgent *agent,
                                    const char *machine_id);
DDS_ReturnCode_t vehicle_agent_start(struct VehicleAgent *agent);
DDS_ReturnCode_t vehicle_agent_run(struct VehicleAgent *agent);
DDS_ReturnCode_t vehicle_agent_request_stop(struct VehicleAgent *agent);
void vehicle_agent_fini(struct VehicleAgent *agent);

#endif