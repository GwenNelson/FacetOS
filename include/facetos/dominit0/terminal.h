#pragma once

#include <facetos/dominit0/config.h>

typedef struct CurrentSeatTerminal {
    FacetHandle terminal;
    FacetHandle input;
    FacetHandle output;
    FacetHandle control;
    bool usable;
} CurrentSeatTerminal;

typedef struct CurrentSeat {
    const FacetConfigSeatDefinition *config;
    FacetHandle seat;
    CurrentSeatTerminal *terminals;
    bool usable;
    void *platform_state;
} CurrentSeat;

/* Builds the configured polling serial terminal and delegates its separate
 * reader, writer, and terminal authorities to the assigned domain. */
int dominit0_terminal_initialize(Dominit0SystemConfig *system);
typedef struct Dominit0ProcessEnvironment Dominit0ProcessEnvironment;
int dominit0_terminal_bind_process_environment(
    CurrentDomain *domain, size_t assignment_index,
    Dominit0ProcessEnvironment *environment);
void dominit0_terminal_destroy(void);
