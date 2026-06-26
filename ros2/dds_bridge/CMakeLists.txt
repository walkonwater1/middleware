cmake_minimum_required(VERSION 3.8)
project(dds_bridge)

# ============================================================
# 依赖
# ============================================================

# ROS2
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rosidl_default_generators REQUIRED)
find_package(std_msgs REQUIRED)

# CycloneDDS (原生 C API — ROS2 底层就是它)
find_package(CycloneDDS REQUIRED)

# ============================================================
# ROS2 自定义消息 (匹配 DDS IDL)
# ============================================================
rosidl_generate_interfaces(${PROJECT_NAME}
    "msg/VehicleState.msg"
    DEPENDENCIES std_msgs
)

# ============================================================
# DDS → ROS2 桥接节点
# ============================================================
add_executable(dds_ros2_bridge src/dds_ros2_bridge.cpp)

# 需要 DDS IDL 生成的头文件路径 (假设 dds/build 已由 idlc 生成)
set(DDS_GEN_DIR "${CMAKE_SOURCE_DIR}/../dds/build" CACHE PATH
    "DDS 代码生成输出目录 (idlc -l c)")

target_include_directories(dds_ros2_bridge PRIVATE
    ${DDS_GEN_DIR}
    ${CycloneDDS_INCLUDE_DIRS}
)

ament_target_dependencies(dds_ros2_bridge
    rclcpp
    rosidl_default_runtime
)

target_link_libraries(dds_ros2_bridge
    CycloneDDS::ddsc
)

rosidl_get_typesupport_target(cpp_typesupport_target
    ${PROJECT_NAME} "rosidl_typesupport_cpp")

target_link_libraries(dds_ros2_bridge
    ${cpp_typesupport_target}
)

# ============================================================
# 安装
# ============================================================
install(TARGETS dds_ros2_bridge
    DESTINATION lib/${PROJECT_NAME})

ament_package()
