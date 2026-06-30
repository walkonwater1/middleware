cmake_minimum_required(VERSION 3.16)
project(grpc-robot-demo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================
# 所有依赖通过 pkg-config 查找 (兼容 Ubuntu 22.04 gRPC 1.30)
# ============================================================
find_package(PkgConfig REQUIRED)

# Protobuf C++
pkg_check_modules(PROTOBUF REQUIRED IMPORTED_TARGET protobuf)

# gRPC C++
pkg_check_modules(GRPCPP REQUIRED IMPORTED_TARGET grpc++)

# gRPC C 底层
pkg_check_modules(GRPC REQUIRED IMPORTED_TARGET grpc)

# gRPC support library
pkg_check_modules(GPR REQUIRED IMPORTED_TARGET gpr)

# protoc 编译器
find_program(PROTOC_EXECUTABLE protoc REQUIRED)

# grpc_cpp_plugin
find_program(GRPC_CPP_PLUGIN grpc_cpp_plugin REQUIRED)

# ============================================================
# gRPC 代码生成
# ============================================================
set(PROTO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/proto")
set(PROTO_FILE "${PROTO_DIR}/robot.proto")
set(GEN_DIR  "${CMAKE_CURRENT_BINARY_DIR}/gen")
file(MAKE_DIRECTORY ${GEN_DIR})

add_custom_command(
    OUTPUT
        ${GEN_DIR}/robot.pb.cc
        ${GEN_DIR}/robot.pb.h
        ${GEN_DIR}/robot.grpc.pb.cc
        ${GEN_DIR}/robot.grpc.pb.h
    COMMAND ${PROTOC_EXECUTABLE}
        --proto_path=${PROTO_DIR}
        --cpp_out=${GEN_DIR}
        --grpc_out=${GEN_DIR}
        --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN}
        ${PROTO_FILE}
    DEPENDS ${PROTO_FILE}
    COMMENT "Generating gRPC C++ from robot.proto"
)

# ============================================================
# 库 & 可执行文件
# ============================================================
add_library(robot_proto
    ${GEN_DIR}/robot.pb.cc
    ${GEN_DIR}/robot.grpc.pb.cc
)
target_link_libraries(robot_proto
    PUBLIC
    PkgConfig::PROTOBUF
    PkgConfig::GRPCPP
    PkgConfig::GRPC
    PkgConfig::GPR
)
target_include_directories(robot_proto PUBLIC ${GEN_DIR})

# Server
add_executable(grpc-server server.cpp)
target_link_libraries(grpc-server robot_proto)

# Client
add_executable(grpc-client client.cpp)
target_link_libraries(grpc-client robot_proto)
