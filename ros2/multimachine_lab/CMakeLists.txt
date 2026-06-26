cmake_minimum_required(VERSION 3.8)
project(multimachine_lab)

find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)

add_executable(discovery_publisher src/discovery_publisher.cpp)
ament_target_dependencies(discovery_publisher rclcpp std_msgs)

add_executable(discovery_subscriber src/discovery_subscriber.cpp)
ament_target_dependencies(discovery_subscriber rclcpp std_msgs)

install(TARGETS discovery_publisher discovery_subscriber
  DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY scripts DESTINATION share/${PROJECT_NAME}
  USE_SOURCE_PERMISSIONS)
ament_package()
