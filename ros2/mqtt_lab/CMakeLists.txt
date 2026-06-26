cmake_minimum_required(VERSION 3.8)
project(mqtt_lab)

find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)

# 查找 mosquitto 系统库 (非 ROS2 包)
find_library(MOSQUITTO_LIB mosquitto REQUIRED)
find_path(MOSQUITTO_INCLUDE mosquitto.h REQUIRED)

add_executable(ros2_mqtt_bridge src/ros2_mqtt_bridge.cpp)
ament_target_dependencies(ros2_mqtt_bridge rclcpp std_msgs)
target_include_directories(ros2_mqtt_bridge PRIVATE ${MOSQUITTO_INCLUDE})
target_link_libraries(ros2_mqtt_bridge ${MOSQUITTO_LIB})

install(TARGETS ros2_mqtt_bridge DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY scripts DESTINATION share/${PROJECT_NAME}
  USE_SOURCE_PERMISSIONS)
ament_package()
