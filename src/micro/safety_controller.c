#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include "safety_controller_service.h"

static volatile sig_atomic_t stop_requested = 0;

static void
handle_signal(int signal_number)
{
    (void)signal_number;
    stop_requested = 1;
}

static int
parse_command(const char *name, HpcSafety_SafetyCommandKind *command_kind)
{
    if (strcmp(name, "noop") == 0) {
        *command_kind = SAFETY_COMMAND_NOOP;
    } else if (strcmp(name, "enable") == 0) {
        *command_kind = SAFETY_COMMAND_ENABLE;
    } else if (strcmp(name, "disable") == 0) {
        *command_kind = SAFETY_COMMAND_DISABLE;
    } else if (strcmp(name, "reset") == 0) {
        *command_kind = SAFETY_COMMAND_RESET;
    } else {
        return 0;
    }
    return 1;
}

int
main(int argc, char **argv)
{
    struct SafetyController controller = { 0 };
    HpcSafety_SafetyCommandKind command_kind = SAFETY_COMMAND_NOOP;
    DDS_ReturnCode_t result;

    // OSAPI_Log_set_verbosity(OSAPI_LOG_VERBOSITY_DEBUG);

    if (argc > 2 || (argc == 2 && !parse_command(argv[1], &command_kind))) {
        fprintf(stderr, "Usage: %s [noop|enable|disable|reset]\n", argv[0]);
        return EXIT_FAILURE;
    }

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    result = safety_controller_init(&controller, command_kind);
    if (result != DDS_RETCODE_OK) {
        fprintf(stderr, "Safety Controller initialization failed: %d\n",
                (int)result);
        safety_controller_fini(&controller);
        return EXIT_FAILURE;
    }

    printf("Safety Controller started\n");
    result = safety_controller_run(&controller, &stop_requested);
    if (result != DDS_RETCODE_OK) {
        fprintf(stderr, "Safety Controller stopped with error: %d\n",
                (int)result);
    }
    safety_controller_fini(&controller);
    return result == DDS_RETCODE_OK ? EXIT_SUCCESS : EXIT_FAILURE;
}