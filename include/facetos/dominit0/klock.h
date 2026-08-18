#pragma once

typedef struct {
    unsigned int locked;
} klock_t;

#define KLOCK_INITIALIZER { .locked = 0 }

void klock_init(klock_t *lock);
void klock_lock(klock_t *lock);
int  klock_trylock(klock_t *lock);
void klock_unlock(klock_t *lock);
