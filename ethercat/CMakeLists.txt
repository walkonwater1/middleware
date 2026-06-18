cmake_minimum_required(VERSION 3.10)
project(ethercat_demo LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# 公共库
add_library(ethercat_common STATIC ethercat.h)

# 主站
add_executable(ethercat_master ethercat_master.c)
target_link_libraries(ethercat_master rt pthread)

# 从站
add_executable(ethercat_slave ethercat_slave.c)
target_link_libraries(ethercat_slave rt pthread m)
