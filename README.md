MSR Scaling Benchmark
=====================

Reading from Model-Specific Registers (MSRs) from a CPU requires executing the read on the CPU of interest.
Thus, an application must migrate to the target CPU before reading the desired MSR(s).

In Linux, reading MSRs is achieved by reading the device file `/dev/cpu/N/msr` for CPU `N`.
If the application is not executing on the target CPU, the kernel will migrate when the device file is read.

As the number of CPUs, sockets, and higher NUMA levels on a system increases, so does the overhead of migrating between CPUs and reading MSRs.
This benchmark is to help evaluate different application approaches.

The basic benchmark pattern is:

    Input: Iterations
    Input: CPUGroups
    Input: MSRs
    for _ in Iterations:
        for CPUGroup in CPUGroups:
            for CPU in CPUGroup:
                for MSR in MSRs:
                    read MSR

The following benchmark variations are implemented:

* `serial` - single threaded, without explicit `CPU` binding
* `serial_migrate` - single threaded, with explicit `CPU` binding
* `thread` - threaded by `CPUGroup`, without explicit `CPU` binding (threads poll and yield waiting for iteration go-ahead)
* `thread_migrate` - threaded by `CPUGroup`, with explicit `CPU` binding (threads poll and yield waiting for iteration go-ahead)
* `thread_notif` - threaded by `CPUGroup`, without explicit `CPU` binding (threads wait on conditional for iteration go-ahead)
* `thread_notif_migrate` - threaded by `CPUGroup`, with explicit `CPU` binding (threads wait on conditional for iteration go-ahead)


Prerequisites
-------------

Build tools:

* [CMake](https://cmake.org/) >= `2.8.12`

Libraries:

* POSIX Threads - pthreads-compatible library.


Building
--------

The project uses CMake:

    mkdir build
    cd build
    cmake ..
    make


Usage
-----

See the help output:

    msr-scaling-bench -h

Third-party profiling tools may be used to evaluate benchmark behavior, e.g., `time`, `gprof`, or `Intel vTune`.
