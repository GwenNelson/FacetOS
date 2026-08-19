#pragma once

#include <stdint.h>

typedef struct {
	uint8_t bytes[16];
} uuid_t;

#define UUID_INIT(a, b, c, d, e)			\
	((uuid_t) {{					\
		((a) >> 24) & 0xff,			\
		((a) >> 16) & 0xff,			\
		((a) >>  8) & 0xff,			\
		(a) & 0xff,				\
		((b) >> 8) & 0xff,			\
		(b) & 0xff,				\
		((c) >> 8) & 0xff,			\
		(c) & 0xff,				\
		((d) >> 8) & 0xff,			\
		(d) & 0xff,				\
		((e) >> 40) & 0xff,			\
		((e) >> 32) & 0xff,			\
		((e) >> 24) & 0xff,			\
		((e) >> 16) & 0xff,			\
		((e) >>  8) & 0xff,			\
		(e) & 0xff				\
	}})

