#include <stdio.h>
#include <stdlib.h>

#include "vehicle_agent_service.h"

int
main(int argc, char **argv)
{
    struct VehicleAgent agent = { 0 };
    const char *machine_id = argc > 1 ? argv[1] : "haul-001";
    DDS_ReturnCode_t result;
    int exit_status = EXIT_FAILURE;

    result = vehicle_agent_init(&agent, machine_id);
    if (result != DDS_RETCODE_OK) {
        fprintf(stderr, "Vehicle agent initialization failed: %d\n",
                (int)result);
        vehicle_agent_fini(&agent);
        return EXIT_FAILURE;
    }

    result = vehicle_agent_start(&agent);
    if (result != DDS_RETCODE_OK) {
        fprintf(stderr, "Vehicle agent startup failed: %d\n", (int)result);
        vehicle_agent_fini(&agent);
        return EXIT_FAILURE;
    }

    printf("Vehicle agent '%s' started\n", machine_id);
    result = vehicle_agent_run(&agent);
    if (result == DDS_RETCODE_OK) {
        exit_status = EXIT_SUCCESS;
    } else {
        fprintf(stderr, "Vehicle agent stopped with error: %d\n", (int)result);
    }

    vehicle_agent_fini(&agent);
    return exit_status;
}