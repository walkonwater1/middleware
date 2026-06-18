cmake_minimum_required(VERSION 3.10)
project(shm_demo C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# POSIX 共享内存需要链接 rt (real-time) 库
# 信号量需要 pthread 库

# ===== 基础示例: 直接映射读写 =====
add_executable(shm_writer shm_writer.c)
target_link_libraries(shm_writer rt pthread)

add_executable(shm_reader shm_reader.c)
target_link_libraries(shm_reader rt pthread)

# ===== 进阶示例: 信号量 + 环形缓冲区 =====
add_executable(shm_sem_producer shm_sem_producer.c)
target_include_directories(shm_sem_producer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(shm_sem_producer rt pthread)

add_executable(shm_sem_consumer shm_sem_consumer.c)
target_include_directories(shm_sem_consumer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(shm_sem_consumer rt pthread)
