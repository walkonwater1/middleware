cmake_minimum_required(VERSION 3.8)
project(lifecycle_lab)

find_package(rclcpp REQUIRED)
find_package(rclcpp_lifecycle REQUIRED)
find_package(std_msgs REQUIRED)

add_executable(lifecycle_demo src/lifecycle_demo.cpp)
ament_target_dependencies(lifecycle_demo rclcpp rclcpp_lifecycle std_msgs)

install(TARGETS lifecycle_demo DESTINATION lib/${PROJECT_NAME})
ament_package()
