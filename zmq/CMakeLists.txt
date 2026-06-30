cmake_minimum_required(VERSION 3.10)
project(zmq_demo CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找 libzmq — 优先 pkg-config, 回退到手动查找
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(ZMQ libzmq)
endif()

if(NOT ZMQ_FOUND)
    # 手动查找 zmq.h 和 libzmq
    find_path(ZMQ_INCLUDE_DIR zmq.h
        PATHS /usr/include /usr/local/include /tmp
    )
    find_library(ZMQ_LIBRARY zmq
        PATHS /usr/lib/x86_64-linux-gnu /usr/lib /tmp
    )
    if(ZMQ_INCLUDE_DIR AND ZMQ_LIBRARY)
        set(ZMQ_INCLUDE_DIRS ${ZMQ_INCLUDE_DIR})
        set(ZMQ_LDFLAGS ${ZMQ_LIBRARY})
        set(ZMQ_FOUND TRUE)
    endif()
endif()

if(NOT ZMQ_FOUND)
    message(FATAL_ERROR "libzmq 未找到! 请安装: sudo apt install libzmq3-dev")
endif()

# cppzmq 是 header-only, 需要手动指定
find_path(CPPZMQ_INCLUDE_DIR zmq.hpp
    PATHS /usr/include /usr/local/include /tmp
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

# === PUSH/PULL 模式 ===
add_executable(push_pull_producer  push_pull/producer.cpp)
target_include_directories(push_pull_producer PRIVATE ${ZMQ_INCLUDE_DIRS} ${CPPZMQ_INCLUDE_DIR})
target_link_libraries(push_pull_producer ${ZMQ_LDFLAGS})

add_executable(push_pull_worker  push_pull/worker.cpp)
target_include_directories(push_pull_worker PRIVATE ${ZMQ_INCLUDE_DIRS} ${CPPZMQ_INCLUDE_DIR})
target_link_libraries(push_pull_worker ${ZMQ_LDFLAGS})

# === ROUTER/DEALER 模式 ===
add_executable(router_dealer_server  router_dealer/server.cpp)
target_include_directories(router_dealer_server PRIVATE ${ZMQ_INCLUDE_DIRS} ${CPPZMQ_INCLUDE_DIR})
target_link_libraries(router_dealer_server ${ZMQ_LDFLAGS})

add_executable(router_dealer_client  router_dealer/client.cpp)
target_include_directories(router_dealer_client PRIVATE ${ZMQ_INCLUDE_DIRS} ${CPPZMQ_INCLUDE_DIR})
target_link_libraries(router_dealer_client ${ZMQ_LDFLAGS})

# === PAIR 模式 ===
add_executable(pair_node_a  pair/pair_node_a.cpp)
target_include_directories(pair_node_a PRIVATE ${ZMQ_INCLUDE_DIRS} ${CPPZMQ_INCLUDE_DIR})
target_link_libraries(pair_node_a ${ZMQ_LDFLAGS})

add_executable(pair_node_b  pair/pair_node_b.cpp)
target_include_directories(pair_node_b PRIVATE ${ZMQ_INCLUDE_DIRS} ${CPPZMQ_INCLUDE_DIR})
target_link_libraries(pair_node_b ${ZMQ_LDFLAGS})
