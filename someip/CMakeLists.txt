cmake_minimum_required(VERSION 3.10)
project(someip_demo CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找 vsomeip3
find_package(vsomeip3 REQUIRED)

# 车辆服务端
add_executable(vehicle_service vehicle_service.cpp)
target_link_libraries(vehicle_service vsomeip3::vsomeip3)

# 车辆客户端
add_executable(vehicle_client vehicle_client.cpp)
target_link_libraries(vehicle_client vsomeip3::vsomeip3)
