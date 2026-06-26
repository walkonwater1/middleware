cmake_minimum_required(VERSION 3.8)
project(custom_msg)

find_package(rosidl_default_generators REQUIRED)

# 自定义消息定义
rosidl_generate_interfaces(${PROJECT_NAME}
  "msg/VehicleState.msg"
)

find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)

add_executable(vehicle_publisher src/vehicle_publisher.cpp)
target_include_directories(vehicle_publisher PRIVATE include)
rosidl_target_interfaces(vehicle_publisher ${PROJECT_NAME} "rosidl_typesupport_cpp")
ament_target_dependencies(vehicle_publisher rclcpp std_msgs)

add_executable(vehicle_subscriber src/vehicle_subscriber.cpp)
target_include_directories(vehicle_subscriber PRIVATE include)
rosidl_target_interfaces(vehicle_subscriber ${PROJECT_NAME} "rosidl_typesupport_cpp")
ament_target_dependencies(vehicle_subscriber rclcpp std_msgs)

install(TARGETS vehicle_publisher vehicle_subscriber DESTINATION lib/${PROJECT_NAME})
ament_package()
