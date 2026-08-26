#pragma once

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

/* FacetOS-specific POSIX-view identity extension. */
uint64_t get_domain_id(void);

/* Process/cwd calls are backed by the process's sole IPOSIXView capability. */
pid_t getpid(void);
int chdir(const char *path);
char *getcwd(char *buffer, size_t size);
