#pragma once

#include <facetos/dominit0/config.h>

/* Builds the configured polling serial terminal and delegates its separate
 * reader, writer, and terminal authorities to the assigned domain. */
int dominit0_terminal_initialize(Dominit0SystemConfig *system);
typedef struct Dominit0ProcessEnvironment Dominit0ProcessEnvironment;
int dominit0_terminal_bind_process_environment(
    CurrentDomain *domain, size_t assignment_index,
    Dominit0ProcessEnvironment *environment);
void dominit0_terminal_destroy(void);
