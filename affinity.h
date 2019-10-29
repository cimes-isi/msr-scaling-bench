#ifndef AFFINITY_H
#define AFFINITY_H

#include <inttypes.h>
#include <sched.h>

struct affinity {
    cpu_set_t aff_orig;
};

void affinity_save(struct affinity *aff);

void affinity_restore(const struct affinity *aff);

void affinity_set_cpu(uint32_t cpu);

#endif // AFFINITY_H
