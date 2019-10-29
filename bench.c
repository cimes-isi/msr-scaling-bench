#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#include "affinity.h"
#include "bench.h"
#include "msr.h"

#ifndef BENCH_DEBUG
#define BENCH_DEBUG 0
#endif

static int bench_rdmsrs(const struct msr_handle *h, const uint32_t *msrs, uint32_t n_msrs)
{
    uint64_t data;
    uint32_t m;
#if BENCH_DEBUG
    uint32_t cpu = msr_get_cpu(h);
#endif
    for (m = 0; m < n_msrs; m++) {
        if (msr_read(h, msrs[m], &data) < 0) {
            perror("msr_read");
            return -1;
        }
#if BENCH_DEBUG
        printf("%"PRIu32": %"PRIu32": 0x%08lx\n", cpu, msrs[m], data);
#endif
    }
    return 0;
}

int bench_serial(const struct bench *ctx)
{
    uint32_t i;
    uint32_t g;
    uint32_t h;
    for (i = 0; i < ctx->iters; i++) {
        for (g = 0; g < ctx->n_cpu_groups; g++) {
            for (h = 0; h < ctx->cpu_groups[g].n_handles; h++) {
                if (bench_rdmsrs(ctx->cpu_groups[g].handles[h], ctx->msrs, ctx->n_msrs)) {
                    return -1;
                }
            }
        }
    }
    return 0;
}

int bench_serial_migrate(const struct bench *ctx)
{
    struct affinity aff;
    uint32_t i;
    uint32_t g;
    uint32_t h;
    int err = 0;
    for (i = 0; i < ctx->iters; i++) {
        for (g = 0; g < ctx->n_cpu_groups; g++) {
            for (h = 0; h < ctx->cpu_groups[g].n_handles; h++) {
                affinity_save_and_set(&aff, msr_get_cpu(ctx->cpu_groups[g].handles[h]));
                if (bench_rdmsrs(ctx->cpu_groups[g].handles[h], ctx->msrs, ctx->n_msrs)) {
                    err = errno;
                }
                affinity_restore(&aff);
                if (err) {
                    errno = err;
                    return -1;
                }
            }
        }
    }
    return 0;
}

struct bench_thr_ctx {
    pthread_t thr;
    pthread_mutex_t mtx;
    pthread_cond_t cond;
    int is_notif;
    const struct bench *ctx;
    uint32_t cpu_group;
    int go;
    int die;
    int err;
};

static void *bench_thr(void *arg)
{
    struct bench_thr_ctx *btc = (struct bench_thr_ctx *)arg;
    const struct bench *ctx = btc->ctx;
    struct bench_cpu_group *group = &ctx->cpu_groups[btc->cpu_group];
    uint32_t h;
    while (!btc->die) {
        // wait for go-ahead
        if (btc->go) {
            for (h = 0; h < group->n_handles; h++) {
                if (bench_rdmsrs(group->handles[h], ctx->msrs, ctx->n_msrs)) {
                    btc->err = errno;
                }
            }
            btc->go = 0;
        }
        pthread_yield();
    }
    return NULL;
}

static void *bench_thr_migrate(void *arg)
{
    struct bench_thr_ctx *btc = (struct bench_thr_ctx *)arg;
    const struct bench *ctx = btc->ctx;
    struct bench_cpu_group *group = &ctx->cpu_groups[btc->cpu_group];
    struct affinity aff;
    uint32_t h;
    while (!btc->die) {
        // wait for go-ahead
        if (btc->go) {
            for (h = 0; h < group->n_handles; h++) {
                affinity_save_and_set(&aff, msr_get_cpu(group->handles[h]));
                if (bench_rdmsrs(group->handles[h], ctx->msrs, ctx->n_msrs)) {
                    btc->err = errno;
                }
                affinity_restore(&aff);
            }
            btc->go = 0;
        }
        pthread_yield();
    }
    return NULL;
}

static void *bench_thr_notif(void *arg)
{
    struct bench_thr_ctx *btc = (struct bench_thr_ctx *)arg;
    const struct bench *ctx = btc->ctx;
    struct bench_cpu_group *group = &ctx->cpu_groups[btc->cpu_group];
    uint32_t h;
    pthread_mutex_lock(&btc->mtx);
    while (!btc->die) {
        // wait for go-ahead
        while (!btc->go && !btc->die) {
            pthread_cond_wait(&btc->cond, &btc->mtx);
        }
        if (btc->die) {
            break;
        }
        for (h = 0; h < group->n_handles; h++) {
            if (bench_rdmsrs(group->handles[h], ctx->msrs, ctx->n_msrs)) {
                btc->err = errno;
            }
        }
        btc->go = 0;
    }
    pthread_mutex_unlock(&btc->mtx);
    return NULL;
}

static void *bench_thr_notif_migrate(void *arg)
{
    struct bench_thr_ctx *btc = (struct bench_thr_ctx *)arg;
    const struct bench *ctx = btc->ctx;
    struct bench_cpu_group *group = &ctx->cpu_groups[btc->cpu_group];
    struct affinity aff;
    uint32_t h;
    pthread_mutex_lock(&btc->mtx);
    while (!btc->die) {
        // wait for go-ahead
        while (!btc->go && !btc->die) {
            pthread_cond_wait(&btc->cond, &btc->mtx);
        }
        if (btc->die) {
            break;
        }
        for (h = 0; h < group->n_handles; h++) {
            affinity_save_and_set(&aff, msr_get_cpu(group->handles[h]));
            if (bench_rdmsrs(group->handles[h], ctx->msrs, ctx->n_msrs)) {
                btc->err = errno;
            }
            affinity_restore(&aff);
        }
        btc->go = 0;
    }
    pthread_mutex_unlock(&btc->mtx);
    return NULL;
}
    
static int bench_thread_create(const struct bench *ctx,
                               void *(*start_routine) (void *),
                               struct bench_thr_ctx *thr_ctxs,
                               int is_notif)
{
    uint32_t i;
    for (i = 0; i < ctx->n_cpu_groups; i++) {
        pthread_mutex_init(&thr_ctxs[i].mtx, NULL);
        pthread_cond_init(&thr_ctxs[i].cond, NULL);
        thr_ctxs[i].is_notif = is_notif;
        thr_ctxs[i].ctx = ctx;
        thr_ctxs[i].cpu_group = i;
        thr_ctxs[i].go = 0;
        thr_ctxs[i].die = 0;
        thr_ctxs[i].err = 0;
        errno = pthread_create(&thr_ctxs[i].thr, NULL, start_routine, &thr_ctxs[i]);
        if (errno) {
            perror("pthread_create");
            return -1;
        }
    }
    return 0;
}

static int bench_thread_drive(const struct bench *ctx,
                              struct bench_thr_ctx *thr_ctxs)
{
    uint32_t iter;
    uint32_t i;
    for (iter = 0; iter < ctx->iters; iter++) {
        // tell threads to start an iteration
        for (i = 0; i < ctx->n_cpu_groups; i++) {
            if (thr_ctxs[i].is_notif) {
                pthread_mutex_lock(&thr_ctxs[i].mtx);
                thr_ctxs[i].go = 1;
                pthread_cond_signal(&thr_ctxs[i].cond);
                pthread_mutex_unlock(&thr_ctxs[i].mtx);
            } else {
                thr_ctxs[i].go = 1;
            }
        }
        // wait for all threads to complete an iteration
        for (i = 0; i < ctx->n_cpu_groups; i++) {
            while (thr_ctxs[i].go) {
                pthread_yield();
            }
            if (thr_ctxs[i].err) {
                errno = thr_ctxs[i].err;
                return -1;
            }
        }
    }
    return 0;
}

static int bench_thread_join(const struct bench *ctx,
                             struct bench_thr_ctx *thr_ctxs)
{
    uint32_t i;
    int err = 0;
    for (i = 0; i < ctx->n_cpu_groups; i++) {
        if (thr_ctxs[i].is_notif) {
            pthread_mutex_lock(&thr_ctxs[i].mtx);
            thr_ctxs[i].die = 1;
            pthread_cond_signal(&thr_ctxs[i].cond);
            pthread_mutex_unlock(&thr_ctxs[i].mtx);
        } else {
            thr_ctxs[i].die = 1;
        }
        errno = pthread_join(thr_ctxs[i].thr, NULL);
        if (errno) {
            perror("pthread_join");
            err = errno;
        } else if (thr_ctxs[i].err) {
            err = thr_ctxs[i].err;
        }
        pthread_cond_destroy(&thr_ctxs[i].cond);
        pthread_mutex_destroy(&thr_ctxs[i].mtx);
    }
    errno = err;
    return err ? -1 : 0;
}

static int bench_thread_exec(const struct bench *ctx,
                             void *(*start_routine) (void *),
                             int is_notif)
{
    int rc;
    int err;
    struct bench_thr_ctx *thr_ctxs = calloc(ctx->n_cpu_groups,
                                            sizeof(struct bench_thr_ctx));
    if (!thr_ctxs) {
        perror("calloc");
        return -1;
    }
    rc = bench_thread_create(ctx, start_routine, thr_ctxs, is_notif);
    if (!rc) {
        rc = bench_thread_drive(ctx, thr_ctxs);
    }
    if (rc) {
        err = errno;
        bench_thread_join(ctx, thr_ctxs);
        errno = err;
    } else {
        rc = bench_thread_join(ctx, thr_ctxs);
    }
    free(thr_ctxs);
    return rc;
}

int bench_thread(const struct bench *ctx)
{
    return bench_thread_exec(ctx, bench_thr, 0);
}

int bench_thread_migrate(const struct bench *ctx)
{
    return bench_thread_exec(ctx, bench_thr_migrate, 0);
}

int bench_thread_notif(const struct bench *ctx)
{
    return bench_thread_exec(ctx, bench_thr_notif, 1);
}

int bench_thread_notif_migrate(const struct bench *ctx)
{
    return bench_thread_exec(ctx, bench_thr_notif_migrate, 1);
}
