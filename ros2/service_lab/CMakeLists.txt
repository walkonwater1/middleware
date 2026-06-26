cmake_minimum_required(VERSION 3.8)
project(service_lab)

find_package(rosidl_default_generators REQUIRED)

rosidl_generate_interfaces(${PROJECT_NAME}
  "srv/BatteryQuery.srv"
)

find_package(rclcpp REQUIRED)

add_executable(battery_server src/battery_server.cpp)
rosidl_target_interfaces(battery_server ${PROJECT_NAME} "rosidl_typesupport_cpp")
ament_target_dependencies(battery_server rclcpp)

add_executable(battery_client src/battery_client.cpp)
rosidl_target_interfaces(battery_client ${PROJECT_NAME} "rosidl_typesupport_cpp")
ament_target_dependencies(battery_client rclcpp)

install(TARGETS battery_server battery_client DESTINATION lib/${PROJECT_NAME})
ament_package()
