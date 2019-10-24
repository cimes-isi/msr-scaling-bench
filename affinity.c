#include <inttypes.h>
#include <pthread.h>
#include <sched.h>

#include "affinity.h"

void affinity_save_and_set(struct affinity *aff, uint32_t cpu)
{
    cpu_set_t aff_new;
    CPU_ZERO(&aff_new);
    CPU_SET(cpu, &aff_new);
    pthread_getaffinity_np(pthread_self(), sizeof(cpu_set_t), &aff->aff_orig);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &aff_new);
}

void affinity_restore(struct affinity *aff)
{
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &aff->aff_orig);
}
