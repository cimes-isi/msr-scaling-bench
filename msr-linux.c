#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

struct msr_handle {
    uint32_t cpu;
    int fd;
};


uint32_t msr_get_count(void)
{
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    if (n <= 0 || n > UINT32_MAX) {
        errno = ENODEV;
        return 0;
    }
    return (uint32_t) n;
}

struct msr_handle *msr_alloc(uint32_t cpu)
{
    struct msr_handle *m;
    m = malloc(sizeof(*m));
    if (!m) {
        perror("malloc");
        return NULL;
    }
    m->cpu = cpu;
    return m;
}

void msr_free(struct msr_handle *m)
{
    free(m);
}

uint32_t msr_get_cpu(struct msr_handle *m)
{
    return m->cpu;
}

int msr_open(struct msr_handle *m)
{
    char fname[32];
    snprintf(fname, sizeof(fname), "/dev/cpu/%"PRIu32"/msr", m->cpu);
    if ((m->fd = open(fname, O_RDONLY)) < 0) {
        fprintf(stderr, "%s: %s\n", fname, strerror(errno));
        return -1;
    }
    return 0;
}

int msr_close(struct msr_handle *m)
{
    int rc = 0;
    if (m->fd > 0) {
        rc = close(m->fd);
        if (rc) {
            perror("close");
        }
        m->fd = 0;
    }
    return rc;
}

ssize_t msr_read(struct msr_handle *m, uint32_t msr, uint64_t* data)
{
    return pread(m->fd, data, sizeof(uint64_t), msr);
}
