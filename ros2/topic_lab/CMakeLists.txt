cmake_minimum_required(VERSION 3.8)
project(topic_lab)

find_package(rosidl_default_generators REQUIRED)

rosidl_generate_interfaces(${PROJECT_NAME}
  "msg/VehicleState.msg"
)

find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)

# 基础 String pub/sub
add_executable(basic_pub src/basic_pub.cpp)
ament_target_dependencies(basic_pub rclcpp std_msgs)

add_executable(basic_sub src/basic_sub.cpp)
ament_target_dependencies(basic_sub rclcpp std_msgs)

# 自定义消息 VehicleState pub/sub
add_executable(vehicle_pub src/vehicle_pub.cpp)
rosidl_target_interfaces(vehicle_pub ${PROJECT_NAME} "rosidl_typesupport_cpp")
ament_target_dependencies(vehicle_pub rclcpp std_msgs)

add_executable(vehicle_sub src/vehicle_sub.cpp)
rosidl_target_interfaces(vehicle_sub ${PROJECT_NAME} "rosidl_typesupport_cpp")
ament_target_dependencies(vehicle_sub rclcpp std_msgs)

install(TARGETS basic_pub basic_sub vehicle_pub vehicle_sub
  DESTINATION lib/${PROJECT_NAME})
ament_package()
