cmake_minimum_required(VERSION 3.16)
project(mqtt_demo
    VERSION 1.0.0
    DESCRIPTION "MQTT 学习项目 — 车载 CAN 数据采集与上报 (Linux/ARM)"
    LANGUAGES CXX C
)

# ============================================================
# C++ 标准
# ============================================================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ARM 平台优化
if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm|aarch64")
    add_compile_options(-mcpu=native)
endif()

# 将配置文件拷贝到 build 目录，运行时可从工作目录读取
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/config" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")

# ============================================================
# 全局选项
# ============================================================
option(BUILD_CAN_DEMO    "构建 CAN+MQTT+Protobuf 演示程序" ON)
option(BUILD_SIMPLE_DEMO "构建简单 JSON 演示程序"            ON)

# ============================================================
# CMake 模块路径
# ============================================================
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# ============================================================
# third-lib 路径 (交叉编译产物的预置目录)
# ============================================================
set(THIRD_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third-lib")

# ============================================================
# 查找依赖 (third-lib 优先, 系统库兜底)
# ============================================================

# --- Paho MQTT C ---
find_package(PahoMqttC REQUIRED)

# --- protobuf-c 运行时库 ---
if(BUILD_CAN_DEMO)
    set(PROTOBUF_C_FOUND_IN "system")

    # 1. 优先查找 third-lib
    if(EXISTS "${THIRD_LIB_DIR}/protobuf-c/include/protobuf-c/protobuf-c.h" AND
       EXISTS "${THIRD_LIB_DIR}/protobuf-c/lib/libprotobuf-c.a")
        set(PROTOBUF_C_INCLUDE_DIRS "${THIRD_LIB_DIR}/protobuf-c/include")
        set(PROTOBUF_C_LIBRARIES    "${THIRD_LIB_DIR}/protobuf-c/lib/libprotobuf-c.a")
        set(PROTOBUF_C_LDFLAGS      "${PROTOBUF_C_LIBRARIES}")
        set(PROTOBUF_C_FOUND        TRUE)
        set(PROTOBUF_C_FOUND_IN     "third-lib/")
    else()
        # 2. 备选: pkg-config
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(PROTOBUF_C IMPORTED_TARGET libprotobuf-c)
        if(NOT PROTOBUF_C_FOUND)
            message(FATAL_ERROR
                "libprotobuf-c not found! Options:\n"
                "  1. bash scripts/build_deps.sh aarch64     (交叉编译到 third-lib/)\n"
                "  2. apt install libprotobuf-c-dev           (系统安装)")
        endif()
    endif()

    # 无论来源, 统一创建 IMPORTED target
    if(NOT TARGET PkgConfig::PROTOBUF_C)
        add_library(PkgConfig::PROTOBUF_C UNKNOWN IMPORTED)
        set_target_properties(PkgConfig::PROTOBUF_C PROPERTIES
            IMPORTED_LOCATION "${PROTOBUF_C_LIBRARIES}"
            INTERFACE_INCLUDE_DIRECTORIES "${PROTOBUF_C_INCLUDE_DIRS}"
        )
    endif()

    # protoc-c 编译器 (必须装在构建机上, 不需要交叉编译)
    find_program(PROTOC_C protoc-c)
    if(NOT PROTOC_C)
        message(FATAL_ERROR
            "protoc-c not found! Install on build machine:\n"
            "  sudo apt install protobuf-c-compiler")
    endif()

    message(STATUS "  protobuf-c      : ${PROTOBUF_C_FOUND_IN} (${PROTOBUF_C_LIBRARIES})")
endif()

# --- Threads ---
find_package(Threads REQUIRED)

# ============================================================
# Proto 文件编译为 C (protobuf-c)
# ============================================================
if(BUILD_CAN_DEMO)
    set(PROTO_FILE   "${CMAKE_CURRENT_SOURCE_DIR}/proto/vehicle_can.proto")
    set(PROTO_GEN_H  "${CMAKE_CURRENT_BINARY_DIR}/proto/vehicle_can.pb-c.h")
    set(PROTO_GEN_C  "${CMAKE_CURRENT_BINARY_DIR}/proto/vehicle_can.pb-c.c")

    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/proto")

    add_custom_command(
        OUTPUT  ${PROTO_GEN_H} ${PROTO_GEN_C}
        COMMAND ${PROTOC_C}
            --c_out="${CMAKE_CURRENT_BINARY_DIR}"
            --proto_path="${CMAKE_CURRENT_SOURCE_DIR}"
            "${PROTO_FILE}"
        DEPENDS ${PROTO_FILE}
        COMMENT "Compiling proto: vehicle_can.proto -> C (protobuf-c)"
    )

    add_library(proto_gen OBJECT ${PROTO_GEN_C})
    target_include_directories(proto_gen PUBLIC
        ${CMAKE_CURRENT_BINARY_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}/proto
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
    target_link_libraries(proto_gen PUBLIC PkgConfig::PROTOBUF_C)
endif()

# ============================================================
# 内部库: libmqtt_can  (C++ 静态库)
# ============================================================
if(BUILD_CAN_DEMO)
    add_library(mqtt_can STATIC
        src/lib/can_database.cpp
        src/lib/can_simulator.cpp
        src/lib/can_parser.cpp
        src/lib/mqtt_client.cpp
        src/lib/config_manager.cpp
        src/lib/can_hardware.cpp
        src/lib/report_scheduler.cpp
    )
    target_include_directories(mqtt_can PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    )
    target_link_libraries(mqtt_can PUBLIC
        PahoMqttC::paho-mqtt3as
        Threads::Threads
    )
endif()

# ============================================================
# 目标: mqtt_can_publisher
# ============================================================
if(BUILD_CAN_DEMO)
    add_executable(mqtt_can_publisher src/mqtt_can_publisher.cpp)
    target_include_directories(mqtt_can_publisher PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}/proto
    )
    target_link_libraries(mqtt_can_publisher PRIVATE
        mqtt_can proto_gen
        PahoMqttC::paho-mqtt3as
        PkgConfig::PROTOBUF_C
        Threads::Threads
    )
endif()

# ============================================================
# 目标: mqtt_can_publisher_v5 (MQTT v5 版本)
# ============================================================
if(BUILD_CAN_DEMO)
    add_executable(mqtt_can_publisher_v5 src/mqtt_can_publisher_v5.cpp)
    target_include_directories(mqtt_can_publisher_v5 PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}/proto
    )
    target_link_libraries(mqtt_can_publisher_v5 PRIVATE
        mqtt_can proto_gen
        PahoMqttC::paho-mqtt3as
        PkgConfig::PROTOBUF_C
        Threads::Threads
    )
endif()

# ============================================================
# 目标: mqtt_can_publisher_gb32960 (国标上报)
# ============================================================
if(BUILD_CAN_DEMO)
    add_executable(mqtt_can_publisher_gb32960 src/mqtt_can_publisher_gb32960.cpp)
    target_include_directories(mqtt_can_publisher_gb32960 PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}/proto
    )
    target_link_libraries(mqtt_can_publisher_gb32960 PRIVATE
        mqtt_can proto_gen
        PahoMqttC::paho-mqtt3as
        PkgConfig::PROTOBUF_C
        Threads::Threads
    )
endif()

# ============================================================
# 目标: mqtt_can_subscriber
# ============================================================
if(BUILD_CAN_DEMO)
    add_executable(mqtt_can_subscriber src/mqtt_can_subscriber.cpp)
    target_include_directories(mqtt_can_subscriber PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}/proto
    )
    target_link_libraries(mqtt_can_subscriber PRIVATE
        mqtt_can proto_gen
        PahoMqttC::paho-mqtt3as
        PkgConfig::PROTOBUF_C
        Threads::Threads
    )
endif()

# ============================================================
# 目标: publisher / subscriber  (简单 JSON Demo, C)
# ============================================================
if(BUILD_SIMPLE_DEMO)
    add_executable(publisher demo/publisher.c)
    set_source_files_properties(demo/publisher.c PROPERTIES LANGUAGE C)
    target_include_directories(publisher PRIVATE ${PAHO_MQTT_C_INCLUDE_DIRS})
    target_link_libraries(publisher PRIVATE PahoMqttC::paho-mqtt3as)

    add_executable(subscriber demo/subscriber.c)
    set_source_files_properties(demo/subscriber.c PROPERTIES LANGUAGE C)
    target_include_directories(subscriber PRIVATE ${PAHO_MQTT_C_INCLUDE_DIRS})
    target_link_libraries(subscriber PRIVATE PahoMqttC::paho-mqtt3as)
endif()

# ============================================================
# 打印构建信息
# ============================================================
message(STATUS "")
message(STATUS "=== mqtt_demo 构建配置 ===")
message(STATUS "  Platform         : ${CMAKE_SYSTEM_NAME} (${CMAKE_SYSTEM_PROCESSOR})")
message(STATUS "  C++ Standard     : C++17")
message(STATUS "  BUILD_CAN_DEMO   : ${BUILD_CAN_DEMO}")
message(STATUS "  BUILD_SIMPLE_DEMO: ${BUILD_SIMPLE_DEMO}")
if(BUILD_CAN_DEMO)
    message(STATUS "  protoc-c         : ${PROTOC_C}")
endif()
message(STATUS "")
