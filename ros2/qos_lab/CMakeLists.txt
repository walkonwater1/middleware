cmake_minimum_required(VERSION 3.8)
project(qos_lab)

find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)

add_executable(qos_publisher src/qos_publisher.cpp)
ament_target_dependencies(qos_publisher rclcpp std_msgs)

add_executable(qos_subscriber src/qos_subscriber.cpp)
ament_target_dependencies(qos_subscriber rclcpp std_msgs)

install(TARGETS qos_publisher qos_subscriber DESTINATION lib/${PROJECT_NAME})
ament_package()
