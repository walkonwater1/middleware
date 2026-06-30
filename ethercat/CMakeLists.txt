cmake_minimum_required(VERSION 3.10)
project(ethercat_demo LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# EtherCAT 核心库 (datagram processor + SDO)
add_library(ethercat_core STATIC ethercat.c)
target_include_directories(ethercat_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# 主站
add_executable(ethercat_master ethercat_master.c)
target_include_directories(ethercat_master PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ethercat_master ethercat_core rt pthread)

# 从站
add_executable(ethercat_slave ethercat_slave.c)
target_include_directories(ethercat_slave PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ethercat_slave ethercat_core rt pthread m)
