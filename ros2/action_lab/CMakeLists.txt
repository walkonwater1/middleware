cmake_minimum_required(VERSION 3.8)
project(action_lab)

find_package(rosidl_default_generators REQUIRED)

rosidl_generate_interfaces(${PROJECT_NAME}
  "action/NavigateTo.action"
)

find_package(rclcpp REQUIRED)
find_package(rclcpp_action REQUIRED)

add_executable(navigate_server src/navigate_server.cpp)
rosidl_target_interfaces(navigate_server ${PROJECT_NAME} "rosidl_typesupport_cpp")
ament_target_dependencies(navigate_server rclcpp rclcpp_action)

add_executable(navigate_client src/navigate_client.cpp)
rosidl_target_interfaces(navigate_client ${PROJECT_NAME} "rosidl_typesupport_cpp")
ament_target_dependencies(navigate_client rclcpp rclcpp_action)

install(TARGETS navigate_server navigate_client DESTINATION lib/${PROJECT_NAME})
ament_package()
