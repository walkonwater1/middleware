cmake_minimum_required(VERSION 3.8)
project(tf2_lab)

find_package(rclcpp REQUIRED)
find_package(tf2 REQUIRED)
find_package(tf2_ros REQUIRED)
find_package(tf2_geometry_msgs REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(tf2_msgs REQUIRED)

add_executable(static_broadcaster src/static_broadcaster.cpp)
ament_target_dependencies(static_broadcaster
  rclcpp tf2 tf2_ros geometry_msgs tf2_msgs)

add_executable(robot_broadcaster src/robot_broadcaster.cpp)
ament_target_dependencies(robot_broadcaster
  rclcpp tf2 tf2_ros geometry_msgs tf2_msgs)

add_executable(transform_listener src/transform_listener.cpp)
ament_target_dependencies(transform_listener
  rclcpp tf2 tf2_ros tf2_geometry_msgs geometry_msgs tf2_msgs)

install(TARGETS static_broadcaster robot_broadcaster transform_listener
  DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY launch DESTINATION share/${PROJECT_NAME})
ament_package()
