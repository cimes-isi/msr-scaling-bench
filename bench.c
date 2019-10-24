#include <inttypes.h>
#include <stdio.h>

#include "affinity.h"
#include "bench.h"
#include "msr.h"

static void bench_rdmsrs(struct msr_handle *h, uint32_t *msrs, uint32_t n_msrs)
{
    uint64_t data;
    uint32_t m;
    uint32_t cpu = msr_get_cpu(h);
    for (m = 0; m < n_msrs; m++) {
        msr_read(h, msrs[m], &data);
        // TODO: silent option, but can't let compiler optimize out the read
        printf("%"PRIu32": %"PRIu32": 0x%08lx\n", cpu, msrs[m], data);
    }
}

/**
 * Iterate without explicit CPU migration (let the kernel migrate).
 */
void bench_serial(struct bench *ctx)
{
    uint32_t i;
    uint32_t g;
    uint32_t h;
    for (i = 0; i < ctx->iters; i++) {
        for (g = 0; g < ctx->n_cpu_groups; g++) {
            for (h = 0; h < ctx->cpu_groups[g].n_handles; h++) {
                bench_rdmsrs(ctx->cpu_groups[g].handles[h], ctx->msrs, ctx->n_msrs);
            }
        }
    }
}

/**
 * Iterate with explicit CPU migration for each handle.
 */
void bench_serial_migrate(struct bench *ctx)
{
    struct affinity aff;
    uint32_t i;
    uint32_t g;
    uint32_t h;
    for (i = 0; i < ctx->iters; i++) {
        for (g = 0; g < ctx->n_cpu_groups; g++) {
            for (h = 0; h < ctx->cpu_groups[g].n_handles; h++) {
                affinity_save_and_set(&aff, msr_get_cpu(ctx->cpu_groups[g].handles[h]));
                bench_rdmsrs(ctx->cpu_groups[g].handles[h], ctx->msrs, ctx->n_msrs);
                affinity_restore(&aff);
            }
        }
    }
}
