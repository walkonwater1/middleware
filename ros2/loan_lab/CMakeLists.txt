cmake_minimum_required(VERSION 3.8)
project(loan_lab)

find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)

add_executable(loan_publisher src/loan_publisher.cpp)
ament_target_dependencies(loan_publisher rclcpp std_msgs)

add_executable(loan_subscriber src/loan_subscriber.cpp)
ament_target_dependencies(loan_subscriber rclcpp std_msgs)

add_executable(loan_comparison src/loan_comparison.cpp)
ament_target_dependencies(loan_comparison rclcpp std_msgs)

install(TARGETS loan_publisher loan_subscriber loan_comparison
  DESTINATION lib/${PROJECT_NAME})
ament_package()
