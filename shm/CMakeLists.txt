cmake_minimum_required(VERSION 3.16)
project(shm_demo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================
# shm — POSIX 共享内存 C++17 Demo
#
#   include/shm_cxx.hpp    C++ RAII 封装 (ShmHandle / ShmMapper / Shm<T> / ShmRingBuf)
#   include/ringbuf.h      无锁环形缓冲区 (C/C++ 双兼容, SPSC)
#   demo/demo_basic.cpp    基本读写 (车辆传感器)
#   demo/demo_ringbuf.cpp  无锁环形缓冲区 (机器人关节指令队列)
# ============================================================

# ---- 基本读写 ----
add_executable(demo_basic demo/demo_basic.cpp)
target_include_directories(demo_basic PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(demo_basic rt pthread)

# ---- 无锁环形缓冲区 ----
add_executable(demo_ringbuf demo/demo_ringbuf.cpp)
target_include_directories(demo_ringbuf PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(demo_ringbuf rt pthread)
