#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bench.h"
#include "msr.h"

#ifndef CPU_GROUPS_MAX
#define CPU_GROUPS_MAX 64
#endif

#ifndef CPUS_MAX
#define CPUS_MAX 4096
#endif

#ifndef MSRS_MAX
#define MSRS_MAX 1024
#endif

static void bench_cpu_group_free(struct bench_cpu_group *bcg)
{
    uint32_t i;
    if (bcg->handles) {
        for (i = 0; i < bcg->n_handles; i++) {
            if (bcg->handles[i]) {
                msr_free(bcg->handles[i]);
            }
        }
        free(bcg->handles);
    }
    bcg->n_handles = 0;
}

static int bench_cpu_group_alloc_all(struct bench_cpu_group *bcg)
{
    uint32_t i;
    bcg->n_handles = msr_get_count();
    if (!bcg->n_handles) {
        perror("msr_get_count");
        return -1;
    }
    bcg->handles = calloc(bcg->n_handles, sizeof(struct msr_handle*));
    if (!bcg->handles) {
        perror("calloc");
        bcg->n_handles = 0;
        return -1;
    }
    for (i = 0; i < bcg->n_handles; i++) {
        bcg->handles[i] = msr_alloc(i);
        if (!bcg->handles[i]) {
            perror("msr_alloc");
            bench_cpu_group_free(bcg);
            return -1;
        }
    }
    return 0;
}

static int bench_cpu_group_alloc_list(struct bench_cpu_group *bcg,
                                      const char *cpulist)
{
    const char *cpu_s;
    char *saveptr;
    uint32_t cpu;
    uint32_t i;
    int rc = 0;
    char *clist = strdup(cpulist);
    if (!clist) {
        perror("strdup");
        return -1;
    }

    // first count n_handles needed
    bcg->n_handles = 0;
    cpu_s = strtok_r(clist, ",", &saveptr);
    while (cpu_s) {
        bcg->n_handles++;
        cpu_s = strtok_r(NULL, ",", &saveptr);
    }
    if (!bcg->n_handles) {
        fprintf(stderr, "No CPUs found in list: %s\n", cpulist);
        errno = EINVAL;
        rc = -1;
        goto out;
    }

    // allocate handles
    bcg->handles = calloc(bcg->n_handles, sizeof(struct msr_handle*));
    if (!bcg->handles) {
        perror("calloc");
        rc = -1;
        goto out;
    }

    // parse cpulist to populate handles
    i = 0;
    strncpy(clist, cpulist, strlen(cpulist));
    cpu_s = strtok_r(clist, ",", &saveptr);
    while (cpu_s) {
        cpu = strtoul(cpu_s, NULL, 0);
        if (cpu == ULONG_MAX && errno == ERANGE) {
            perror("strtoul");
            rc = -1;
            goto fail_free_handles;
        }
        bcg->handles[i] = msr_alloc(cpu);
        if (!bcg->handles[i]) {
            perror("msr_alloc");
            rc = -1;
            goto fail_free_handles;
        }
        i++;
        cpu_s = strtok_r(NULL, ",", &saveptr);
    }

out:
    free(clist);
    return rc;

fail_free_handles:
    bench_cpu_group_free(bcg);
    free(clist);
    return rc;
}

static int bench_cpu_group_close(struct bench_cpu_group *bcg)
{
    uint32_t i;
    int rc = 0;
    for (i = 0; i < bcg->n_handles; i++) {
        rc |= msr_close(bcg->handles[i]);
    }
    return rc;
}

static int bench_cpu_group_open(struct bench_cpu_group *bcg)
{
    uint32_t i;
    int e;
    for (i = 0; i < bcg->n_handles; i++) {
        if (msr_open(bcg->handles[i])) {
            e = errno;
            bench_cpu_group_close(bcg);
            errno = e;
            return -1;
        }
    }
    return 0;
}

static void usage(const char *pname, int code)
{
    fprintf(code ? stderr : stdout,
            "Usage: %s [-b BENCH] [-c CPUS]+ [-i N] [-m N]+ [-h]\n"
            "  -b, --bench=BENCH        Benchmark BENCH, one of: [serial, serial_migrate]\n"
            "                           default=serial\n"
            "  -c, --cpu-group=CPUS     Group cpus CPUS together; CPUS: comma-delimited\n"
            "                           If not specified, all cpus are used in one group\n"
            "  -i, --iters=N            Iterate N times (default=1)\n"
            "  -m, --msr=N              Read msr N from each cpu\n"
            "  -h, --help               Print this message and exit\n",
            pname);
    exit(code);
}

static const char opts_short[] = "b:c:i:m:h";
static const struct option opts_long[] = {
    {"bench",       required_argument,  NULL,   'b'},
    {"cpu-group",   required_argument,  NULL,   'c'},
    {"iters",       required_argument,  NULL,   'i'},
    {"msr",         required_argument,  NULL,   'm'},
    {"help",        no_argument,        NULL,   'h'},
    {0, 0, 0, 0}
};

int main(int argc, char **argv)
{
    const char *b = "serial";
    struct bench_cpu_group cpu_groups[CPU_GROUPS_MAX] = { { 0 } };
    uint32_t msrs[MSRS_MAX] = { 0 };
    struct bench ctx = {
        .cpu_groups = cpu_groups,
        .n_cpu_groups = 0,
        .msrs = msrs,
        .n_msrs = 0,
        .iters = 1,
    };
    int c;
    int i;
    int rc = 0;

    while ((c = getopt_long(argc, argv, opts_short, opts_long, NULL)) != -1) {
        switch (c) {
        case 'b':
            b = optarg;
            break;
        case 'c':
            if (ctx.n_cpu_groups == CPU_GROUPS_MAX) {
                fprintf(stderr, "Too many CPU groups requested, max=%u\n",
                        CPU_GROUPS_MAX);
                // TODO: cleanup
                return E2BIG;
            }
            if (bench_cpu_group_alloc_list(&ctx.cpu_groups[ctx.n_cpu_groups], optarg)) {
                return errno;
            }
            ctx.n_cpu_groups++;
            break;
        case 'i':
            ctx.iters = strtoul(optarg, NULL, 0);
            break;
        case 'm':
            if (ctx.n_msrs < MSRS_MAX) {
                ctx.msrs[ctx.n_msrs] = strtoul(optarg, NULL, 0);
                ctx.n_msrs++;
            } else {
                fprintf(stderr, "Too many MSRs requested, max=%u\n", MSRS_MAX);
                // TODO: cleanup
                return E2BIG;
            }
            break;
        case 'h':
            usage(argv[0], 0);
            break;
        default:
            usage(argv[0], EINVAL);
            break;
        }
    }

    if (!ctx.n_cpu_groups) {
        if (bench_cpu_group_alloc_all(&ctx.cpu_groups[0])) {
            return errno;
        }
        ctx.n_cpu_groups++;
    }

    for (i = 0; i < ctx.n_cpu_groups; i++) {
        if (bench_cpu_group_open(&ctx.cpu_groups[i])) {
            rc = errno;
            goto out;
        }
    }

    if (!strncmp(b, "serial", strlen("serial") + 1)) {
        bench_serial(&ctx);
    } else if (!strncmp(b, "serial_migrate", strlen("serial_migrate") + 1)) {
        bench_serial_migrate(&ctx);
    } else {
        fprintf(stderr, "Unknown benchmark: %s\n", b);
        rc = EINVAL;
    }

out:
    for (i = 0; i < ctx.n_cpu_groups; i++) {
        rc |= bench_cpu_group_close(&ctx.cpu_groups[i]);
        bench_cpu_group_free(&ctx.cpu_groups[i]);
    }
    return rc;
}
