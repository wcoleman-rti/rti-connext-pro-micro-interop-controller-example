#include <stdio.h>
#include <string.h>

#include "safety_controller_service.h"

#define HPC_INSTANCE_COUNT 2
#define MAX_SAMPLES_PER_TAKE 4L

static const HpcSafety_HpcId hpc_ids[HPC_INSTANCE_COUNT] = { 1UL, 2UL };

static void
print_command_writer_status(struct SafetyController *controller)
{
    DDS_DataWriter *writer = HpcSafety_SafetyCommandDataWriter_as_datawriter(
        controller->command_writer);
    struct DDS_PublicationMatchedStatus matched =
        DDS_PublicationMatchedStatus_INITIALIZER;
    struct DDS_OfferedIncompatibleQosStatus incompatible =
        DDS_OfferedIncompatibleQosStatus_INITIALIZER;
    DDS_ReturnCode_t result;

    result = DDS_DataWriter_get_publication_matched_status(writer, &matched);
    if (result == DDS_RETCODE_OK) {
        printf("command_writer PUBLICATION_MATCHED total=%ld current=%ld\n",
               (long)matched.total_count, (long)matched.current_count);
    } else {
        printf("command_writer PUBLICATION_MATCHED status_error=%d\n",
               (int)result);
    }

    result = DDS_DataWriter_get_offered_incompatible_qos_status(
        writer, &incompatible);
    if (result == DDS_RETCODE_OK) {
        printf("command_writer OFFERED_INCOMPATIBLE_QOS total=%ld last_policy=%d\n",
               (long)incompatible.total_count, (int)incompatible.last_policy_id);
    } else {
        printf("command_writer OFFERED_INCOMPATIBLE_QOS status_error=%d\n",
               (int)result);
    }
}

static DDS_ReturnCode_t
publish_commands(struct SafetyController *controller)
{
    DDS_Long index;

    for (index = 0; index < HPC_INSTANCE_COUNT; ++index) {
        DDS_ReturnCode_t result;

        controller->command->target_hpc_id = hpc_ids[index];
        controller->command->command_sequence = 1U;
        controller->command->command = controller->command_kind;
        result = HpcSafety_SafetyCommandDataWriter_write(
                controller->command_writer,
                controller->command,
                &controller->command_handles[index]);
        if (result != DDS_RETCODE_OK) {
            fprintf(stderr, "Failed to publish command for HPC %lu: rc=%d\n",
                    (unsigned long)hpc_ids[index], (int)result);
            return DDS_RETCODE_ERROR;
        }
    }

    return DDS_RETCODE_OK;
}

static DDS_ReturnCode_t
print_health(struct SafetyController *controller)
{
    struct HpcSafety_HpcHealthSeq samples = DDS_SEQUENCE_INITIALIZER;
    struct DDS_SampleInfoSeq sample_info = DDS_SEQUENCE_INITIALIZER;
    DDS_ReturnCode_t result;
    DDS_Long index;

    result = HpcSafety_HpcHealthDataReader_take(
        controller->health_reader, &samples, &sample_info,
        MAX_SAMPLES_PER_TAKE, DDS_ANY_SAMPLE_STATE, DDS_ANY_VIEW_STATE,
        DDS_ANY_INSTANCE_STATE);
    if (result == DDS_RETCODE_NO_DATA) {
        return DDS_RETCODE_OK;
    }
    if (result != DDS_RETCODE_OK) {
        return result;
    }

    for (index = 0; index < HpcSafety_HpcHealthSeq_get_length(&samples); ++index) {
        struct DDS_SampleInfo *info =
            DDS_SampleInfoSeq_get_reference(&sample_info, index);
        HpcSafety_HpcHealth *sample =
            HpcSafety_HpcHealthSeq_get_reference(&samples, index);

        if (info != NULL && info->valid_data && sample != NULL) {
                 printf("HPC %lu health state=%d uptime_ms=%lu ack=%lu fault=%lu\n",
                     (unsigned long)sample->hpc_id, (int)sample->state,
                   (unsigned long)sample->uptime_ms,
                   (unsigned long)sample->last_command_sequence_applied,
                   (unsigned long)sample->fault_code);
        }
    }

    result = HpcSafety_HpcHealthDataReader_return_loan(
        controller->health_reader, &samples, &sample_info);
#ifndef RTI_CERT
    HpcSafety_HpcHealthSeq_finalize(&samples);
    DDS_SampleInfoSeq_finalize(&sample_info);
#endif
    return result;
}

static DDS_ReturnCode_t
print_telemetry(struct SafetyController *controller)
{
    struct HpcSafety_HpcTelemetrySeq samples = DDS_SEQUENCE_INITIALIZER;
    struct DDS_SampleInfoSeq sample_info = DDS_SEQUENCE_INITIALIZER;
    DDS_ReturnCode_t result;
    DDS_Long index;

    result = HpcSafety_HpcTelemetryDataReader_take(
        controller->telemetry_reader, &samples, &sample_info,
        MAX_SAMPLES_PER_TAKE, DDS_ANY_SAMPLE_STATE, DDS_ANY_VIEW_STATE,
        DDS_ANY_INSTANCE_STATE);
    if (result == DDS_RETCODE_NO_DATA) {
        return DDS_RETCODE_OK;
    }
    if (result != DDS_RETCODE_OK) {
        return result;
    }

    for (index = 0; index < HpcSafety_HpcTelemetrySeq_get_length(&samples); ++index) {
        struct DDS_SampleInfo *info =
            DDS_SampleInfoSeq_get_reference(&sample_info, index);
        HpcSafety_HpcTelemetry *sample =
            HpcSafety_HpcTelemetrySeq_get_reference(&samples, index);

        if (info != NULL && info->valid_data && sample != NULL) {
                        printf("HPC %lu telemetry cycles=%lu cycle_time_us=%lu cpu_load=%.1f%%\n",
                                     (unsigned long)sample->hpc_id,
                   (unsigned long)sample->completed_cycles,
                   (unsigned long)sample->application_cycle_time_us,
                   (double)sample->cpu_load_percent);
        }
    }

    result = HpcSafety_HpcTelemetryDataReader_return_loan(
        controller->telemetry_reader, &samples, &sample_info);
#ifndef RTI_CERT
    HpcSafety_HpcTelemetrySeq_finalize(&samples);
    DDS_SampleInfoSeq_finalize(&sample_info);
#endif
    return result;
}

DDS_ReturnCode_t
safety_controller_init(
    struct SafetyController *controller,
    HpcSafety_SafetyCommandKind command_kind)
{
    DDS_ReturnCode_t result;

    if (controller == NULL) {
        return DDS_RETCODE_BAD_PARAMETER;
    }
    memset(controller, 0, sizeof(*controller));
    controller->command_kind = command_kind;

    if (!DDS_ConditionSeq_initialize(&controller->active_conditions) ||
        !DDS_ConditionSeq_set_maximum(&controller->active_conditions, 2)) {
        return DDS_RETCODE_ERROR;
    }

    controller->factory = DDS_DomainParticipantFactory_get_instance();
    controller->registry = RT_Registry_get_instance();
    if (controller->factory == NULL || controller->registry == NULL) {
        return DDS_RETCODE_ERROR;
    }

    controller->appgen_property = (struct APPGEN_FactoryProperty)
        APPGEN_FactoryProperty_INITIALIZER;
    controller->appgen_property._model = APPGEN_get_library_seq();
    if (!RT_Registry_register(
            controller->registry, PROFILE_DEFAULT_APPGEN_NAME,
            APPGEN_Factory_get_interface(),
            &controller->appgen_property._parent, NULL)) {
        return DDS_RETCODE_ERROR;
    }
    controller->appgen_registered = DDS_BOOLEAN_TRUE;

    controller->participant =
        DDS_DomainParticipantFactory_create_participant_from_config(
            controller->factory,
            "HpcSafetyParticipants::safety_controller");
    if (controller->participant == NULL) {
        return DDS_RETCODE_ERROR;
    }

    controller->publisher = DDS_DomainParticipant_lookup_publisher_by_name(
        controller->participant, "safety_publisher");
    controller->subscriber = DDS_DomainParticipant_lookup_subscriber_by_name(
        controller->participant, "safety_subscriber");
    controller->command_writer = HpcSafety_SafetyCommandDataWriter_narrow(
        DDS_DomainParticipant_lookup_datawriter_by_name(
            controller->participant, "safety_publisher::command_writer"));
    controller->health_reader = HpcSafety_HpcHealthDataReader_narrow(
        DDS_DomainParticipant_lookup_datareader_by_name(
            controller->participant, "safety_subscriber::health_reader"));
    controller->telemetry_reader = HpcSafety_HpcTelemetryDataReader_narrow(
        DDS_DomainParticipant_lookup_datareader_by_name(
            controller->participant, "safety_subscriber::telemetry_reader"));
    if (controller->publisher == NULL || controller->subscriber == NULL ||
        controller->command_writer == NULL || controller->health_reader == NULL ||
        controller->telemetry_reader == NULL) {
        return DDS_RETCODE_ERROR;
    }

    controller->command = HpcSafety_SafetyCommandTypeSupport_create_data();
    if (controller->command == NULL) {
        return DDS_RETCODE_OUT_OF_RESOURCES;
    }

    controller->waitset = DDS_WaitSet_new();
    controller->health_condition = DDS_Entity_get_statuscondition(
        DDS_DataReader_as_entity(
            HpcSafety_HpcHealthDataReader_as_datareader(controller->health_reader)));
    controller->telemetry_condition = DDS_Entity_get_statuscondition(
        DDS_DataReader_as_entity(
            HpcSafety_HpcTelemetryDataReader_as_datareader(
                controller->telemetry_reader)));
    if (controller->waitset == NULL || controller->health_condition == NULL ||
        controller->telemetry_condition == NULL) {
        return DDS_RETCODE_OUT_OF_RESOURCES;
    }

    result = DDS_StatusCondition_set_enabled_statuses(
        controller->health_condition, DDS_DATA_AVAILABLE_STATUS);
    if (result != DDS_RETCODE_OK) {
        return result;
    }
    result = DDS_StatusCondition_set_enabled_statuses(
        controller->telemetry_condition, DDS_DATA_AVAILABLE_STATUS);
    if (result != DDS_RETCODE_OK) {
        return result;
    }
    result = DDS_WaitSet_attach_condition(
        controller->waitset,
        DDS_StatusCondition_as_condition(controller->health_condition));
    if (result != DDS_RETCODE_OK) {
        return result;
    }
    result = DDS_WaitSet_attach_condition(
        controller->waitset,
        DDS_StatusCondition_as_condition(controller->telemetry_condition));
    if (result != DDS_RETCODE_OK) {
        return result;
    }

    result = DDS_Entity_enable(
        DDS_DomainParticipant_as_entity(controller->participant));
    if (result != DDS_RETCODE_OK) {
        return result;
    }

    controller->command->target_hpc_id = hpc_ids[0];
    controller->command->command_sequence = 1U;
    controller->command->command = command_kind;
    controller->command_handles[0] =
        HpcSafety_SafetyCommandDataWriter_register_instance(
            controller->command_writer, controller->command);
    controller->command->target_hpc_id = hpc_ids[1];
    controller->command_handles[1] =
        HpcSafety_SafetyCommandDataWriter_register_instance(
            controller->command_writer, controller->command);
    if (DDS_InstanceHandle_is_nil(&controller->command_handles[0]) ||
        DDS_InstanceHandle_is_nil(&controller->command_handles[1])) {
        return DDS_RETCODE_OUT_OF_RESOURCES;
    }

    controller->initialized = DDS_BOOLEAN_TRUE;
    return DDS_RETCODE_OK;
}

DDS_ReturnCode_t
safety_controller_run(
    struct SafetyController *controller,
    const volatile sig_atomic_t *stop_requested)
{
    struct DDS_Duration_t timeout = { 0, 500000000U };
    DDS_ReturnCode_t result;
    DDS_UnsignedLong diagnostics_cycles = 0;

    if (controller == NULL || !controller->initialized || stop_requested == NULL) {
        return DDS_RETCODE_PRECONDITION_NOT_MET;
    }

    result = publish_commands(controller);
    if (result != DDS_RETCODE_OK) {
        return result;
    }
    printf("Safety Controller sent command %d to HPC IDs 1 and 2\n",
           (int)controller->command_kind);

    while (!*stop_requested) {
        result = DDS_WaitSet_wait(
            controller->waitset, &controller->active_conditions, &timeout);
        if (result != DDS_RETCODE_OK && result != DDS_RETCODE_TIMEOUT) {
            return result;
        }

        if (++diagnostics_cycles % 4U == 0U) {
            print_command_writer_status(controller);
        }

        if (DDS_Condition_get_trigger_value(
                DDS_StatusCondition_as_condition(controller->health_condition))) {
            result = print_health(controller);
            if (result != DDS_RETCODE_OK) {
                return result;
            }
        }
        if (DDS_Condition_get_trigger_value(
                DDS_StatusCondition_as_condition(controller->telemetry_condition))) {
            result = print_telemetry(controller);
            if (result != DDS_RETCODE_OK) {
                return result;
            }
        }
    }

    return DDS_RETCODE_OK;
}

void
safety_controller_fini(struct SafetyController *controller)
{
    if (controller == NULL) {
        return;
    }

#ifndef RTI_CERT
    if (controller->waitset != NULL) {
        if (controller->health_condition != NULL) {
            DDS_WaitSet_detach_condition(
                controller->waitset,
                DDS_StatusCondition_as_condition(controller->health_condition));
        }
        if (controller->telemetry_condition != NULL) {
            DDS_WaitSet_detach_condition(
                controller->waitset,
                DDS_StatusCondition_as_condition(controller->telemetry_condition));
        }
        DDS_WaitSet_delete(controller->waitset);
    }
    DDS_ConditionSeq_finalize(&controller->active_conditions);
    if (controller->participant != NULL) {
        DDS_DomainParticipant_delete_contained_entities(controller->participant);
        DDS_DomainParticipantFactory_delete_participant(
            controller->factory, controller->participant);
    }
    if (controller->appgen_registered) {
        RT_Registry_unregister(
            controller->registry, PROFILE_DEFAULT_APPGEN_NAME, NULL, NULL);
    }
    if (controller->command != NULL) {
        HpcSafety_SafetyCommandTypeSupport_delete_data(controller->command);
    }
    if (controller->factory != NULL) {
        DDS_DomainParticipantFactory_finalize_instance();
    }
#endif
    memset(controller, 0, sizeof(*controller));
}