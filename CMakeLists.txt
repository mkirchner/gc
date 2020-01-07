cmake_minimum_required(VERSION 3.0)
project(gc C)

set(CMAKE_C_STANDARD 99)

include_directories(src)
include_directories(test)

add_executable(test_gc
        src/gc.h
        src/log.c
        src/log.h
        test/minunit.h
        test/test_gc.c)

