#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#include "affinity.h"
#include "bench.h"
#include "msr.h"

static int bench_rdmsrs(struct msr_handle *h, uint32_t *msrs, uint32_t n_msrs)
{
    uint64_t data;
    uint32_t m;
    uint32_t cpu = msr_get_cpu(h);
    ssize_t rc = 0;
    for (m = 0; m < n_msrs; m++) {
        rc = msr_read(h, msrs[m], &data);
        if (rc < 0) {
            perror("msr_read");
            return -1;
        }
        // TODO: silent option, but can't let compiler optimize out the read
        printf("%"PRIu32": %"PRIu32": 0x%08lx\n", cpu, msrs[m], data);
    }
    return 0;
}

int bench_serial(struct bench *ctx)
{
    uint32_t i;
    uint32_t g;
    uint32_t h;
    int rc = 0;
    for (i = 0; i < ctx->iters; i++) {
        for (g = 0; g < ctx->n_cpu_groups; g++) {
            for (h = 0; h < ctx->cpu_groups[g].n_handles; h++) {
                rc = bench_rdmsrs(ctx->cpu_groups[g].handles[h], ctx->msrs, ctx->n_msrs);
            }
        }
    }
    return rc;
}

int bench_serial_migrate(struct bench *ctx)
{
    struct affinity aff;
    uint32_t i;
    uint32_t g;
    uint32_t h;
    int rc = 0;
    for (i = 0; i < ctx->iters; i++) {
        for (g = 0; g < ctx->n_cpu_groups; g++) {
            for (h = 0; h < ctx->cpu_groups[g].n_handles; h++) {
                affinity_save_and_set(&aff, msr_get_cpu(ctx->cpu_groups[g].handles[h]));
                rc = bench_rdmsrs(ctx->cpu_groups[g].handles[h], ctx->msrs, ctx->n_msrs);
                affinity_restore(&aff);
            }
        }
    }
    return rc;
}

// TODO: use notifications instead of polling?
struct bench_thr_ctx {
    pthread_t thr;
    struct bench *ctx;
    uint32_t cpu_group;
    int go;
    int die;
    int rc;
};

static void *bench_thr(void *arg)
{
    struct bench_thr_ctx *btc = (struct bench_thr_ctx *)arg;
    struct bench *ctx = btc->ctx;
    struct bench_cpu_group *group = &ctx->cpu_groups[btc->cpu_group];
    uint32_t h;
    while (!btc->die) {
        // wait for go-ahead
        if (btc->go) {
            for (h = 0; h < group->n_handles; h++) {
                btc->rc = bench_rdmsrs(group->handles[h], ctx->msrs, ctx->n_msrs);
            }
            btc->go = 0;
        } else {
            pthread_yield();
        }
    }
    return NULL;
}

static void *bench_thr_migrate(void *arg)
{
    struct bench_thr_ctx *btc = (struct bench_thr_ctx *)arg;
    struct bench *ctx = btc->ctx;
    struct bench_cpu_group *group = &ctx->cpu_groups[btc->cpu_group];
    struct affinity aff;
    uint32_t h;
    while (!btc->die) {
        // wait for go-ahead
        if (btc->go) {
            for (h = 0; h < group->n_handles; h++) {
                affinity_save_and_set(&aff, msr_get_cpu(group->handles[h]));
                btc->rc = bench_rdmsrs(group->handles[h], ctx->msrs, ctx->n_msrs);
                affinity_restore(&aff);
            }
            btc->go = 0;
        } else {
            pthread_yield();
        }
    }
    return NULL;
}

static int bench_thread_exec(struct bench *ctx, void *(*start_routine) (void *))
{
    uint32_t i;
    uint32_t j;
    uint32_t iter;
    int rc = 0;
    struct bench_thr_ctx *thr_ctxs = calloc(ctx->n_cpu_groups,
                                            sizeof(struct bench_thr_ctx));
    if (!thr_ctxs) {
        perror("calloc");
        return -1;
    }

    for (i = 0; i < ctx->n_cpu_groups; i++) {
        thr_ctxs[i].ctx = ctx;
        thr_ctxs[i].cpu_group = i;
        thr_ctxs[i].go = 0;
        errno = pthread_create(&thr_ctxs[i].thr, NULL, start_routine, &thr_ctxs[i]);
        if (errno) {
            perror("pthread_create");
            rc = errno;
            for (j = 0; j < i; j++) {
                pthread_cancel(thr_ctxs[j].thr);
            }
            goto out;
        }
    }

    for (iter = 0; iter < ctx->iters; iter++) {
        // tell threads to start an iteration
        for (i = 0; i < ctx->n_cpu_groups; i++) {
            thr_ctxs[i].go = 1;
        }

        // wait for all threads to complete an iteration
        for (i = 0; i < ctx->n_cpu_groups; i++) {
            while (thr_ctxs[i].go) {
                pthread_yield();
            }
            if (thr_ctxs[i].rc) {
                rc = thr_ctxs[i].rc;
                for (j = 0; j < ctx->n_cpu_groups; j++) {
                    pthread_cancel(thr_ctxs[j].thr);
                }
                goto out;
            }
        }
    }

    for (i = 0; i < ctx->n_cpu_groups; i++) {
        thr_ctxs[i].die = 1;
        errno = pthread_join(thr_ctxs[i].thr, NULL);
        if (errno) {
            perror("pthread_join");
            rc = errno;
        } else if (thr_ctxs[i].rc) {
            rc = thr_ctxs[i].rc;
        }
    }

out:
    free(thr_ctxs);
    return rc;
}

int bench_thread(struct bench *ctx)
{
    return bench_thread_exec(ctx, bench_thr);
}

int bench_thread_migrate(struct bench *ctx)
{
    return bench_thread_exec(ctx, bench_thr_migrate);
}
