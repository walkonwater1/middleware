cmake_minimum_required(VERSION 3.10)
project(dds_demo C)

# ============================================================
# 查找 CycloneDDS
# ============================================================
find_package(CycloneDDS REQUIRED)

# ============================================================
# IDL → C 类型代码生成
# ============================================================
set(IDL_FILE  ${CMAKE_CURRENT_SOURCE_DIR}/idl/HelloWorld.idl)
set(GEN_H     ${CMAKE_CURRENT_BINARY_DIR}/VehicleState.h)
set(GEN_C     ${CMAKE_CURRENT_BINARY_DIR}/VehicleState.c)

find_program(IDLC idlc)
if(NOT IDLC)
    message(FATAL_ERROR "idlc 未找到! 请安装 cyclonedds-dev")
endif()

add_custom_command(
    OUTPUT ${GEN_H} ${GEN_C}
    COMMAND ${IDLC} -l c
            -I ${CMAKE_CURRENT_SOURCE_DIR}/idl
            -o ${CMAKE_CURRENT_BINARY_DIR}
            ${IDL_FILE}
    DEPENDS ${IDL_FILE}
    COMMENT "从 IDL 生成 C 类型: VehicleState"
)

# ============================================================
# 目标编译
# ============================================================
add_executable(publisher publisher.c ${GEN_C})
add_dependencies(publisher dds_gen_types)
target_include_directories(publisher PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CycloneDDS_INCLUDE_DIRS}
)
target_link_libraries(publisher CycloneDDS::ddsc)

add_executable(subscriber subscriber.c ${GEN_C})
add_dependencies(subscriber dds_gen_types)
target_include_directories(subscriber PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CycloneDDS_INCLUDE_DIRS}
)
target_link_libraries(subscriber CycloneDDS::ddsc)

# 添加一个方便的自定义目标
add_custom_target(dds_gen_types DEPENDS ${GEN_H} ${GEN_C})
