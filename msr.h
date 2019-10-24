#ifndef MSR_H
#define MSR_H

#include <inttypes.h>
#include <sys/types.h>

struct msr_handle;

uint32_t msr_get_count(void);

struct msr_handle *msr_alloc(uint32_t cpu);

void msr_free(struct msr_handle *m);

uint32_t msr_get_cpu(struct msr_handle *m);

int msr_open(struct msr_handle *m);

int msr_close(struct msr_handle *m);

ssize_t msr_read(struct msr_handle *m, uint32_t msr, uint64_t* data);

#endif // MSR_H
