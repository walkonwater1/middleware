cmake_minimum_required(VERSION 3.10)
project(zmq_demo CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找 libzmq
find_package(PkgConfig REQUIRED)
pkg_check_modules(ZMQ REQUIRED libzmq)

# cppzmq 是 header-only, 需要手动指定或使用 find_package
# 如果在标准路径, 直接可用; 否则设置 CMAKE_PREFIX_PATH
find_path(CPPZMQ_INCLUDE_DIR zmq.hpp
  PATHS /usr/include /usr/local/include
        ${CMAKE_SOURCE_DIR}/../third_party/cppzmq
)

if(NOT CPPZMQ_INCLUDE_DIR)
  message(FATAL_ERROR "cppzmq (zmq.hpp) 未找到! 请 clone https://github.com/zeromq/cppzmq")
endif()

# === REQ/REP 模式 ===
add_executable(req_rep_server  req_rep/server.cpp)
target_include_directories(req_rep_server PRIVATE ${ZMQ_INCLUDE_DIRS} ${CPPZMQ_INCLUDE_DIR})
target_link_libraries(req_rep_server ${ZMQ_LDFLAGS})

add_executable(req_rep_client  req_rep/client.cpp)
target_include_directories(req_rep_client PRIVATE ${ZMQ_INCLUDE_DIRS} ${CPPZMQ_INCLUDE_DIR})
target_link_libraries(req_rep_client ${ZMQ_LDFLAGS})

# === PUB/SUB 模式 ===
add_executable(pub_sub_publisher  pub_sub/publisher.cpp)
target_include_directories(pub_sub_publisher PRIVATE ${ZMQ_INCLUDE_DIRS} ${CPPZMQ_INCLUDE_DIR})
target_link_libraries(pub_sub_publisher ${ZMQ_LDFLAGS})

add_executable(pub_sub_subscriber pub_sub/subscriber.cpp)
target_include_directories(pub_sub_subscriber PRIVATE ${ZMQ_INCLUDE_DIRS} ${CPPZMQ_INCLUDE_DIR})
target_link_libraries(pub_sub_subscriber ${ZMQ_LDFLAGS})
