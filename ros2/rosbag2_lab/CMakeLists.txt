cmake_minimum_required(VERSION 3.8)
project(rosbag2_lab)

find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)
find_package(rosbag2_cpp REQUIRED)
find_package(rosbag2_transport REQUIRED)
find_package(rosbag2_storage REQUIRED)

add_executable(data_generator src/data_generator.cpp)
ament_target_dependencies(data_generator rclcpp std_msgs)

add_executable(bag_recorder src/bag_recorder.cpp)
ament_target_dependencies(bag_recorder rclcpp std_msgs rosbag2_cpp rosbag2_transport rosbag2_storage)

add_executable(bag_player src/bag_player.cpp)
ament_target_dependencies(bag_player rclcpp std_msgs rosbag2_cpp rosbag2_storage)

install(TARGETS data_generator bag_recorder bag_player
  DESTINATION lib/${PROJECT_NAME})
ament_package()
