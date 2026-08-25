#pragma once

#include <facetos/dominit0/config.h>

/* Builds the configured polling serial terminal and delegates its separate
 * reader, writer, and terminal authorities to the assigned domain. */
int dominit0_terminal_initialize(Dominit0SystemConfig *system);
void dominit0_terminal_destroy(void);
