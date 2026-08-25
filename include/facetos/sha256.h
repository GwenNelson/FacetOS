#pragma once

#include <stddef.h>
#include <stdint.h>

/* Small freestanding SHA-256 primitive used for the development-only local
 * password verifier.  It hashes bytes; callers perform their own encoding. */
void facet_sha256(const uint8_t *data, size_t size, uint8_t digest[32]);
void facet_sha256_hex(const uint8_t digest[32], char output[65]);
