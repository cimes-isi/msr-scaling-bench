cmake_minimum_required(VERSION 2.8.12)

project(msr-scaling-bench)
set(VERSION_MAJOR 0)
set(VERSION_MINOR 1)
set(VERSION_PATCH 0)
set(PROJECT_VERSION ${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH})

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -std=c99")
add_definitions(-D_GNU_SOURCE)


find_package(Threads REQUIRED)

# Binaries

add_executable(msr-scaling-bench msr-scaling-bench.c affinity.c bench.c msr-linux.c)
target_link_libraries(msr-scaling-bench ${CMAKE_THREAD_LIBS_INIT})
