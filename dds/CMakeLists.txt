cmake_minimum_required(VERSION 3.10)
project(dds_demo C)

# ============================================================
# 查找 CycloneDDS (0.8.x)
# ============================================================
find_package(CycloneDDS REQUIRED)

# ============================================================
# IDL → C 类型代码生成
#
# 注意: CycloneDDS 0.8.x 的 idlc 去掉了 -o 标志,
#       输出文件直接写到当前工作目录, 文件名基于 .idl 文件名
#       HelloWorld.idl → HelloWorld.h / HelloWorld.c
#       robot.idl      → robot.h      / robot.c
# ============================================================
find_program(IDLC idlc)
if(NOT IDLC)
    message(FATAL_ERROR "idlc 未找到! 请安装 cyclonedds-dev")
endif()

set(IDL_DIR       ${CMAKE_CURRENT_SOURCE_DIR}/idl)
set(GEN_DIR       ${CMAKE_CURRENT_BINARY_DIR})

# --- VehicleState (来自 VehicleState.idl) ---
set(VEHICLE_GEN_H ${GEN_DIR}/VehicleState.h)
set(VEHICLE_GEN_C ${GEN_DIR}/VehicleState.c)

add_custom_command(
    OUTPUT  ${VEHICLE_GEN_H} ${VEHICLE_GEN_C}
    COMMAND ${IDLC} -l c -I ${IDL_DIR} ${IDL_DIR}/VehicleState.idl
    WORKING_DIRECTORY ${GEN_DIR}
    DEPENDS ${IDL_DIR}/VehicleState.idl
    COMMENT "idlc: VehicleState.idl → C 类型"
)

# --- Robot (来自 robot.idl) ---
set(ROBOT_GEN_H ${GEN_DIR}/robot.h)
set(ROBOT_GEN_C ${GEN_DIR}/robot.c)

add_custom_command(
    OUTPUT  ${ROBOT_GEN_H} ${ROBOT_GEN_C}
    COMMAND ${IDLC} -l c -I ${IDL_DIR} ${IDL_DIR}/robot.idl
    WORKING_DIRECTORY ${GEN_DIR}
    DEPENDS ${IDL_DIR}/robot.idl
    COMMENT "idlc: robot.idl → C 类型"
)

# --- VehicleInstance (来自 vehicle_instance.idl, 带 @key) ---
set(INSTANCE_GEN_H ${GEN_DIR}/vehicle_instance.h)
set(INSTANCE_GEN_C ${GEN_DIR}/vehicle_instance.c)

add_custom_command(
    OUTPUT  ${INSTANCE_GEN_H} ${INSTANCE_GEN_C}
    COMMAND ${IDLC} -l c -I ${IDL_DIR} ${IDL_DIR}/vehicle_instance.idl
    WORKING_DIRECTORY ${GEN_DIR}
    DEPENDS ${IDL_DIR}/vehicle_instance.idl
    COMMENT "idlc: vehicle_instance.idl → C 类型 (带 @key)"
)

# ============================================================
# 基础 Demo: Publisher + Subscriber (车辆状态)
# ============================================================
add_executable(publisher vehicle/publisher.c ${VEHICLE_GEN_C})
target_include_directories(publisher PRIVATE
    ${GEN_DIR} ${CycloneDDS_INCLUDE_DIRS})
target_link_libraries(publisher CycloneDDS::ddsc)

add_executable(subscriber vehicle/subscriber.c ${VEHICLE_GEN_C})
target_include_directories(subscriber PRIVATE
    ${GEN_DIR} ${CycloneDDS_INCLUDE_DIRS})
target_link_libraries(subscriber CycloneDDS::ddsc)

# --- QoS 实验: Reliable/BestEffort + Transient/Volatile ---
add_executable(qos_lab vehicle/qos_lab.c ${VEHICLE_GEN_C})
target_include_directories(qos_lab PRIVATE
    ${GEN_DIR} ${CycloneDDS_INCLUDE_DIRS})
target_link_libraries(qos_lab CycloneDDS::ddsc)

# --- 自动发现实验: 多节点无需配 IP 端口 ---
add_executable(discovery_lab vehicle/discovery_lab.c ${VEHICLE_GEN_C})
target_include_directories(discovery_lab PRIVATE
    ${GEN_DIR} ${CycloneDDS_INCLUDE_DIRS})
target_link_libraries(discovery_lab CycloneDDS::ddsc)

# --- Listener 回调模式 (Push, 非轮询 Pull) ---
add_executable(listener_lab vehicle/listener_lab.c ${VEHICLE_GEN_C})
target_include_directories(listener_lab PRIVATE
    ${GEN_DIR} ${CycloneDDS_INCLUDE_DIRS})
target_link_libraries(listener_lab CycloneDDS::ddsc)

# --- Partition 隔离 ---
add_executable(partition_lab vehicle/partition_lab.c ${VEHICLE_GEN_C})
target_include_directories(partition_lab PRIVATE
    ${GEN_DIR} ${CycloneDDS_INCLUDE_DIRS})
target_link_libraries(partition_lab CycloneDDS::ddsc)

# ============================================================
# 进阶实验: Content Filter / Instance / Security / Benchmark
# ============================================================

# --- Content Filter: Topic 级内容过滤 ---
add_executable(content_filter_lab vehicle/content_filter_lab.c ${VEHICLE_GEN_C})
target_include_directories(content_filter_lab PRIVATE
    ${GEN_DIR} ${CycloneDDS_INCLUDE_DIRS})
target_link_libraries(content_filter_lab CycloneDDS::ddsc)

# --- Instance 管理: @key + 实例生命周期 ---
add_executable(instance_lab vehicle/instance_lab.c ${INSTANCE_GEN_C})
target_include_directories(instance_lab PRIVATE
    ${GEN_DIR} ${CycloneDDS_INCLUDE_DIRS})
target_link_libraries(instance_lab CycloneDDS::ddsc)

# --- Security 安全机制 ---
add_executable(security_lab vehicle/security_lab.c ${VEHICLE_GEN_C})
target_include_directories(security_lab PRIVATE
    ${GEN_DIR} ${CycloneDDS_INCLUDE_DIRS})
target_link_libraries(security_lab CycloneDDS::ddsc)

# --- Benchmark 性能测试 ---
add_executable(bench_lab vehicle/bench_lab.c ${VEHICLE_GEN_C})
target_include_directories(bench_lab PRIVATE
    ${GEN_DIR} ${CycloneDDS_INCLUDE_DIRS})
target_link_libraries(bench_lab CycloneDDS::ddsc)

# ============================================================
# Robot Demo: DDS 机器人系统 (对标 EHR 架构)
#
#   robot_dds_service — 服务端 (状态机 + 任务调度 + DDS 发布)
#   robot_dds_monitor — 监控端 (DDS 订阅 + 远程指令)
# ============================================================
add_executable(robot_dds_service robot/robot_dds_service.c ${ROBOT_GEN_C})
target_include_directories(robot_dds_service PRIVATE
    ${GEN_DIR} ${CycloneDDS_INCLUDE_DIRS})
target_link_libraries(robot_dds_service CycloneDDS::ddsc)

add_executable(robot_dds_monitor robot/robot_dds_monitor.c ${ROBOT_GEN_C})
target_include_directories(robot_dds_monitor PRIVATE
    ${GEN_DIR} ${CycloneDDS_INCLUDE_DIRS})
target_link_libraries(robot_dds_monitor CycloneDDS::ddsc)

# ============================================================
# 聚合 target: 一次性生成所有 IDL 类型
# ============================================================
add_custom_target(dds_gen_types DEPENDS
    ${VEHICLE_GEN_H} ${VEHICLE_GEN_C}
    ${ROBOT_GEN_H}   ${ROBOT_GEN_C}
    ${INSTANCE_GEN_H} ${INSTANCE_GEN_C})
