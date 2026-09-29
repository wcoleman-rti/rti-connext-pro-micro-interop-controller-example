#include <stdio.h>
#include <string.h>

#include "vehicle_agent_service.h"
#include "monotonic_clock.h"

#define MACHINE_ID_MAX_LENGTH 32U
#define MAX_COMMANDS_PER_TAKE 10L
#define POSITION_PERIOD_NS UINT64_C(100000000)
#define NANOSECONDS_PER_SECOND UINT64_C(1000000000)

static void
set_status_message(struct VehicleAgent *agent, const char *message)
{
#ifndef RTI_CERT
    char *replacement = DDS_String_dup(message);

    if (replacement != NULL) {
        DDS_String_free(agent->status->status_message);
        agent->status->status_message = replacement;
    }
#else
    size_t length = strlen(message);

    if (length >= sizeof(agent->status_message_buffer)) {
        length = sizeof(agent->status_message_buffer) - 1U;
    }
    memcpy(agent->status_message_buffer, message, length);
    agent->status_message_buffer[length] = '\0';
    agent->status->status_message = agent->status_message_buffer;
#endif
}

static DDS_ReturnCode_t
publish_status(struct VehicleAgent *agent)
{
    return Mining_MachineStatusDataWriter_write(
        agent->status_writer, agent->status, &agent->status_handle);
}

static DDS_ReturnCode_t
publish_position(struct VehicleAgent *agent)
{
    return Mining_PositionDataWriter_write(
        agent->position_writer, agent->position, &agent->position_handle);
}

static void
apply_command(const Mining_Command *command, struct VehicleAgent *agent)
{
    switch (command->command_type) {
        case START:
        case RESUME:
            if (!agent->status->emergency_stop_active &&
                agent->status->operational_state != FAULTED) {
                agent->status->operational_state = RUNNING;
                agent->position->speed_mps = 1.0f;
                set_status_message(agent, "Running");
            }
            break;
        case STOP:
            agent->status->operational_state = STOPPED;
            agent->position->speed_mps = 0.0f;
            set_status_message(agent, "Stopped");
            break;
        case PAUSE:
            if (agent->status->operational_state == RUNNING) {
                agent->status->operational_state = IDLE;
            }
            agent->position->speed_mps = 0.0f;
            set_status_message(agent, "Paused");
            break;
        case RESET:
            agent->status->emergency_stop_active = DDS_BOOLEAN_FALSE;
            agent->status->operational_state = STOPPED;
            agent->position->speed_mps = 0.0f;
            set_status_message(agent, "Reset");
            break;
        case EMERGENCY_STOP:
            agent->status->emergency_stop_active = DDS_BOOLEAN_TRUE;
            agent->status->operational_state = FAULTED;
            agent->position->speed_mps = 0.0f;
            set_status_message(agent, "Emergency stop active");
            break;
        default:
            fprintf(stderr, "Unknown command type: %d\n",
                    (int)command->command_type);
            break;
    }
}

static DDS_ReturnCode_t
process_commands(struct VehicleAgent *agent)
{
    struct Mining_CommandSeq samples = DDS_SEQUENCE_INITIALIZER;
    struct DDS_SampleInfoSeq sample_info = DDS_SEQUENCE_INITIALIZER;
    DDS_ReturnCode_t result;
    DDS_Long index;

    result = Mining_CommandDataReader_take(
        agent->command_reader, &samples, &sample_info,
        MAX_COMMANDS_PER_TAKE, DDS_ANY_SAMPLE_STATE, DDS_ANY_VIEW_STATE,
        DDS_ANY_INSTANCE_STATE);
    if (result != DDS_RETCODE_OK) {
        goto cleanup;
    }

    for (index = 0; index < Mining_CommandSeq_get_length(&samples); ++index) {
        struct DDS_SampleInfo *info =
            DDS_SampleInfoSeq_get_reference(&sample_info, index);
        Mining_Command *command = Mining_CommandSeq_get_reference(&samples, index);

        if (info != NULL && info->valid_data && command != NULL &&
            command->machine_id != NULL && agent->status->machine_id != NULL &&
            strcmp(command->machine_id, agent->status->machine_id) == 0) {
            apply_command(command, agent);
            result = publish_status(agent);
            if (result != DDS_RETCODE_OK) {
                fprintf(stderr, "Failed to publish machine status: %d\n",
                        (int)result);
            }
        }
    }

    result = Mining_CommandDataReader_return_loan(
        agent->command_reader, &samples, &sample_info);

cleanup:
#ifndef RTI_CERT
    Mining_CommandSeq_finalize(&samples);
    DDS_SampleInfoSeq_finalize(&sample_info);
#endif
    return result;
}

static DDS_ReturnCode_t
configure_waitset(struct VehicleAgent *agent)
{
    DDS_ReturnCode_t result;

    if (!DDS_ConditionSeq_set_maximum(&agent->active_conditions, 2)) {
        return DDS_RETCODE_ERROR;
    }

    agent->waitset = DDS_WaitSet_new();
    agent->shutdown_condition = DDS_GuardCondition_new();
    agent->command_condition = DDS_Entity_get_statuscondition(
        DDS_DataReader_as_entity(
            Mining_CommandDataReader_as_datareader(agent->command_reader)));
    if (agent->waitset == NULL || agent->shutdown_condition == NULL ||
        agent->command_condition == NULL) {
        return DDS_RETCODE_OUT_OF_RESOURCES;
    }

    result = DDS_StatusCondition_set_enabled_statuses(
        agent->command_condition, DDS_DATA_AVAILABLE_STATUS);
    if (result != DDS_RETCODE_OK) {
        return result;
    }
    result = DDS_WaitSet_attach_condition(
        agent->waitset,
        DDS_StatusCondition_as_condition(agent->command_condition));
    if (result != DDS_RETCODE_OK) {
        return result;
    }
    return DDS_WaitSet_attach_condition(
        agent->waitset,
        DDS_GuardCondition_as_condition(agent->shutdown_condition));
}

DDS_ReturnCode_t
vehicle_agent_init(struct VehicleAgent *agent, const char *machine_id)
{
    DDS_ReturnCode_t result;
    struct DDS_ConditionSeq empty_conditions = DDS_SEQUENCE_INITIALIZER;

    // OSAPI_Log_set_verbosity(OSAPI_LOG_VERBOSITY_WARNING);

    if (agent == NULL || machine_id == NULL ||
        strlen(machine_id) > MACHINE_ID_MAX_LENGTH) {
        return DDS_RETCODE_BAD_PARAMETER;
    }
    memset(agent, 0, sizeof(*agent));

    agent->active_conditions = empty_conditions;
    if (!DDS_ConditionSeq_initialize(&agent->active_conditions)) {
        return DDS_RETCODE_ERROR;
    }

    agent->factory = DDS_DomainParticipantFactory_get_instance();
    agent->registry = RT_Registry_get_instance();
    if (agent->factory == NULL || agent->registry == NULL) {
        return DDS_RETCODE_ERROR;
    }

    agent->appgen_property = (struct APPGEN_FactoryProperty)
        APPGEN_FactoryProperty_INITIALIZER;
    agent->appgen_property._model = APPGEN_get_library_seq();
    if (!RT_Registry_register(agent->registry, PROFILE_DEFAULT_APPGEN_NAME,
                              APPGEN_Factory_get_interface(),
                              &agent->appgen_property._parent, NULL)) {
        return DDS_RETCODE_ERROR;
    }
    agent->appgen_registered = DDS_BOOLEAN_TRUE;

    agent->participant = DDS_DomainParticipantFactory_create_participant_from_config(
        agent->factory, "VehicleAgentParticipants::vehicle_agent");
    if (agent->participant == NULL) {
        return DDS_RETCODE_ERROR;
    }

    agent->publisher = DDS_DomainParticipant_lookup_publisher_by_name(
        agent->participant, "vehicle_publisher");
    agent->subscriber = DDS_DomainParticipant_lookup_subscriber_by_name(
        agent->participant, "vehicle_subscriber");
    agent->status_writer = Mining_MachineStatusDataWriter_narrow(
        DDS_DomainParticipant_lookup_datawriter_by_name(
            agent->participant, "vehicle_publisher::status_writer"));
    agent->position_writer = Mining_PositionDataWriter_narrow(
        DDS_DomainParticipant_lookup_datawriter_by_name(
            agent->participant, "vehicle_publisher::position_writer"));
    agent->command_reader = Mining_CommandDataReader_narrow(
        DDS_DomainParticipant_lookup_datareader_by_name(
            agent->participant, "vehicle_subscriber::command_reader"));
    if (agent->publisher == NULL || agent->subscriber == NULL ||
        agent->status_writer == NULL || agent->position_writer == NULL ||
        agent->command_reader == NULL) {
        return DDS_RETCODE_ERROR;
    }

    agent->status = Mining_MachineStatusTypeSupport_create_data();
    agent->position = Mining_PositionTypeSupport_create_data();
    if (agent->status == NULL || agent->position == NULL) {
        return DDS_RETCODE_OUT_OF_RESOURCES;
    }
    agent->status->machine_id = DDS_String_dup(machine_id);
    agent->position->machine_id = DDS_String_dup(machine_id);
    if (agent->status->machine_id == NULL ||
        agent->position->machine_id == NULL) {
        return DDS_RETCODE_OUT_OF_RESOURCES;
    }
    set_status_message(agent, "Stopped");
    agent->status->operational_state = STOPPED;
    agent->status->emergency_stop_active = DDS_BOOLEAN_FALSE;
    agent->position->latitude_deg = 0.0;
    agent->position->longitude_deg = 0.0;
    agent->position->altitude_m = 0.0f;
    agent->position->heading_deg = 0.0f;
    agent->position->speed_mps = 0.0f;

    result = configure_waitset(agent);
    if (result != DDS_RETCODE_OK) {
        return result;
    }

    agent->initialized = DDS_BOOLEAN_TRUE;
    return DDS_RETCODE_OK;
}

DDS_ReturnCode_t
vehicle_agent_start(struct VehicleAgent *agent)
{
    DDS_ReturnCode_t result;

    if (agent == NULL || !agent->initialized || agent->started) {
        return DDS_RETCODE_PRECONDITION_NOT_MET;
    }

    result = DDS_Entity_enable(
        DDS_DomainParticipant_as_entity(agent->participant));
    if (result != DDS_RETCODE_OK) {
        return result;
    }

    agent->status_handle = Mining_MachineStatusDataWriter_register_instance(
        agent->status_writer, agent->status);
    if (DDS_InstanceHandle_is_nil(&agent->status_handle)) {
        return DDS_RETCODE_OUT_OF_RESOURCES;
    }
    agent->position_handle = Mining_PositionDataWriter_register_instance(
        agent->position_writer, agent->position);
    if (DDS_InstanceHandle_is_nil(&agent->position_handle)) {
        return DDS_RETCODE_OUT_OF_RESOURCES;
    }

    if (monotonic_clock_now_ns(&agent->next_position_release_ns) != 0) {
        return DDS_RETCODE_ERROR;
    }
    agent->next_position_release_ns += POSITION_PERIOD_NS;

    result = publish_status(agent);
    if (result != DDS_RETCODE_OK) {
        return result;
    }
    agent->started = DDS_BOOLEAN_TRUE;
    return DDS_RETCODE_OK;
}

DDS_ReturnCode_t
vehicle_agent_request_stop(struct VehicleAgent *agent)
{
    if (agent == NULL || agent->shutdown_condition == NULL) {
        return DDS_RETCODE_PRECONDITION_NOT_MET;
    }
    return DDS_GuardCondition_set_trigger_value(
        agent->shutdown_condition, DDS_BOOLEAN_TRUE);
}

DDS_ReturnCode_t
vehicle_agent_run(struct VehicleAgent *agent)
{
    DDS_ReturnCode_t result;
    DDS_ReturnCode_t run_result = DDS_RETCODE_OK;
    struct DDS_Duration_t timeout;
    uint64_t now_ns;

    if (agent == NULL || !agent->started) {
        return DDS_RETCODE_PRECONDITION_NOT_MET;
    }

    while (DDS_Condition_get_trigger_value(
               DDS_GuardCondition_as_condition(agent->shutdown_condition)) !=
           DDS_BOOLEAN_TRUE) {
        uint64_t remaining_ns;

        if (monotonic_clock_now_ns(&now_ns) != 0) {
            run_result = DDS_RETCODE_ERROR;
            break;
        }
        remaining_ns = now_ns < agent->next_position_release_ns
            ? agent->next_position_release_ns - now_ns
            : 0U;
        timeout.sec = (DDS_Long)(remaining_ns / NANOSECONDS_PER_SECOND);
        timeout.nanosec = (DDS_UnsignedLong)(remaining_ns % NANOSECONDS_PER_SECOND);

        result = DDS_WaitSet_wait(
            agent->waitset, &agent->active_conditions, &timeout);
        if (result != DDS_RETCODE_OK && result != DDS_RETCODE_TIMEOUT) {
            fprintf(stderr, "Command wait failed: %d\n", (int)result);
            run_result = result;
            break;
        }

        if (DDS_Condition_get_trigger_value(
                DDS_GuardCondition_as_condition(agent->shutdown_condition)) ==
            DDS_BOOLEAN_TRUE) {
            break;
        }

        if (monotonic_clock_now_ns(&now_ns) != 0) {
            run_result = DDS_RETCODE_ERROR;
            break;
        }
        if (now_ns >= agent->next_position_release_ns) {
            if (agent->status->operational_state == RUNNING) {
                agent->position->latitude_deg += 0.000001;
                agent->position->longitude_deg += 0.000001;
            }
            result = publish_position(agent);
            if (result != DDS_RETCODE_OK) {
                fprintf(stderr, "Failed to publish position: %d\n", (int)result);
            }
            do {
                agent->next_position_release_ns += POSITION_PERIOD_NS;
            } while (agent->next_position_release_ns <= now_ns);
        }

        if (DDS_Condition_get_trigger_value(
                DDS_StatusCondition_as_condition(agent->command_condition)) ==
            DDS_BOOLEAN_TRUE) {
            result = process_commands(agent);
            if (result != DDS_RETCODE_OK) {
                fprintf(stderr, "Failed to process commands: %d\n", (int)result);
            }
        }

    }

    return run_result;
}

void
vehicle_agent_fini(struct VehicleAgent *agent)
{
    if (agent == NULL) {
        return;
    }

#ifndef RTI_CERT
    if (agent->waitset != NULL) {
        if (agent->command_condition != NULL) {
            DDS_WaitSet_detach_condition(
                agent->waitset,
                DDS_StatusCondition_as_condition(agent->command_condition));
        }
        if (agent->shutdown_condition != NULL) {
            DDS_WaitSet_detach_condition(
                agent->waitset,
                DDS_GuardCondition_as_condition(agent->shutdown_condition));
        }
        DDS_WaitSet_delete(agent->waitset);
    }
    DDS_ConditionSeq_finalize(&agent->active_conditions);
    if (agent->shutdown_condition != NULL) {
        DDS_GuardCondition_delete(agent->shutdown_condition);
    }
    if (agent->participant != NULL) {
        DDS_DomainParticipant_delete_contained_entities(agent->participant);
        DDS_DomainParticipantFactory_delete_participant(
            agent->factory, agent->participant);
    }
    if (agent->appgen_registered) {
        RT_Registry_unregister(
            agent->registry, PROFILE_DEFAULT_APPGEN_NAME, NULL, NULL);
    }
    if (agent->status != NULL) {
        Mining_MachineStatusTypeSupport_delete_data(agent->status);
    }
    if (agent->position != NULL) {
        Mining_PositionTypeSupport_delete_data(agent->position);
    }
    if (agent->factory != NULL) {
        DDS_DomainParticipantFactory_finalize_instance();
    }
#endif
    memset(agent, 0, sizeof(*agent));
}