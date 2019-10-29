#include <inttypes.h>
#include <pthread.h>
#include <sched.h>

#include "affinity.h"

void affinity_save(struct affinity *aff)
{
    pthread_getaffinity_np(pthread_self(), sizeof(cpu_set_t), &aff->aff_orig);
}

void affinity_restore(const struct affinity *aff)
{
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &aff->aff_orig);
}

void affinity_set_cpu(uint32_t cpu)
{
    cpu_set_t aff_new;
    CPU_ZERO(&aff_new);
    CPU_SET(cpu, &aff_new);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &aff_new);
}
