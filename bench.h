#ifndef BENCH_H
#define BENCH_H

#include <inttypes.h>

#include "msr.h"

struct bench_cpu_group {
    struct msr_handle **handles;
    uint32_t n_handles;
};

struct bench {
    struct bench_cpu_group *cpu_groups;
    uint32_t n_cpu_groups;
    uint32_t *msrs;
    uint32_t n_msrs;
    uint32_t iters;
};

/**
 * Iterate without explicit CPU migration (let the kernel migrate).
 */
int bench_serial(const struct bench *ctx);

/**
 * Iterate with explicit CPU migration for each handle.
 */
int bench_serial_migrate(const struct bench *ctx);

/**
 * Iterate CPU groups in threads without explicit CPU migration (let the kernel migrate).
 */
int bench_thread(const struct bench *ctx);

/**
 * Iterate CPU groups in threads with explicit CPU migration for each handle.
 */
int bench_thread_migrate(const struct bench *ctx);

#endif // BENCH_H
