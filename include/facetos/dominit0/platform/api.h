#pragma once


void platform_init_early(void); // anything the platform needs IMMEDIATELY, usually a NOP

void platform_init(void);       // sets up everything for the platform before other subsystems

void platform_yield(void);	// yields to the microkernel or other tasks

void platform_debug_print(char* str); // prints a debug string
