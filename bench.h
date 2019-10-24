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

void bench_serial(struct bench *ctx);

void bench_serial_migrate(struct bench *ctx);

#endif // BENCH_H
