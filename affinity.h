#ifndef AFFINITY_H
#define AFFINITY_H

#include <inttypes.h>
#include <sched.h>

struct affinity {
    cpu_set_t aff_orig;
};

void affinity_save_and_set(struct affinity *aff, uint32_t cpu);

void affinity_restore(const struct affinity *aff);

#endif // AFFINITY_H
