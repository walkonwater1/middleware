cmake_minimum_required(VERSION 3.10)
project(canopen_demo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# CANopen 核心库
add_library(canopen_core STATIC
    canopen.cpp
)

target_include_directories(canopen_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# 从站
add_executable(canopen_node canopen_node.cpp)
target_link_libraries(canopen_node canopen_core pthread)

# 主站
add_executable(canopen_master canopen_master.cpp)
target_link_libraries(canopen_master canopen_core pthread)
