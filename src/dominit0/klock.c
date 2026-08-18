#include <klock.h>
#include <stdatomic.h>

static inline void klock_cpu_relax() {
#if defined(__x86_64__) || defined(__i386__)
    __asm__ volatile ("pause");
#else
    /*
     * Compiler barrier for architectures where we haven't
     * provided a specific spin-wait hint yet.
     */
    __asm__ volatile ("" ::: "memory");
#endif
}

void klock_init(klock_t *lock) {
    __atomic_store_n(&lock->locked, 0, __ATOMIC_RELAXED);
}

void klock_lock(klock_t *lock) {
    for (;;) {
        /*
         * Try to acquire the lock.
         */
        if (!__atomic_exchange_n(&lock->locked, 1, __ATOMIC_ACQUIRE))
            return;

        /*
         * Someone else owns it. Spin using ordinary loads rather
         * than repeatedly performing atomic exchanges and bouncing
         * the cache line between CPUs.
         */
        while (__atomic_load_n(&lock->locked, __ATOMIC_RELAXED))
            klock_cpu_relax();
    }
}

int klock_trylock(klock_t *lock) {
    return !__atomic_exchange_n(
        &lock->locked,
        1,
        __ATOMIC_ACQUIRE
    );
}

void klock_unlock(klock_t *lock) {
    __atomic_store_n(
        &lock->locked,
        0,
        __ATOMIC_RELEASE
    );
}
