cmake_minimum_required(VERSION 3.8)
project(dds_vendor_lab)

find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)
find_package(rmw_implementation REQUIRED)

add_executable(vendor_test_node src/vendor_test_node.cpp)
ament_target_dependencies(vendor_test_node rclcpp std_msgs rmw_implementation)

install(TARGETS vendor_test_node DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY scripts DESTINATION share/${PROJECT_NAME}
  USE_SOURCE_PERMISSIONS)
ament_package()
