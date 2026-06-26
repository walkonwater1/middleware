cmake_minimum_required(VERSION 3.10)
project(gdbus_demo LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================================
# 查找 GLib/GIO
# ============================================================
set(_glib_third_lib "${CMAKE_CURRENT_SOURCE_DIR}/third-lib/glib")

if(EXISTS "${_glib_third_lib}/lib/libglib-2.0.a")
    message(STATUS "  GLib/GIO       : third-lib/")
    set(GLIB_INCLUDE_DIRS
        "${_glib_third_lib}/include/glib-2.0"
        "${_glib_third_lib}/include/glibconfig")
    set(GLIB_LINK_LIBS
        "${_glib_third_lib}/lib/libgio-2.0.a"
        "${_glib_third_lib}/lib/libgobject-2.0.a"
        "${_glib_third_lib}/lib/libgmodule-2.0.a"
        "${_glib_third_lib}/lib/libglib-2.0.a"
        "${_glib_third_lib}/lib/libffi.a"
        "${_glib_third_lib}/lib/libpcre2-8.a"
        pthread dl m)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GLIB REQUIRED gio-2.0 gio-unix-2.0)
    message(STATUS "  GLib/GIO       : system (pkg-config)")
    set(GLIB_INCLUDE_DIRS ${GLIB_INCLUDE_DIRS})
    set(GLIB_LINK_LIBS   ${GLIB_LIBRARIES})
endif()

# ============================================================
# gdbus-codegen — XML → C skeleton/proxy
# ============================================================
find_program(GDBUS_CODEGEN gdbus-codegen REQUIRED)
find_program(PYTHON3 python3 REQUIRED)

set(CODEGEN_DIR      "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(CODEGEN_C_H      "${CODEGEN_DIR}/robot-dbus.h")
set(CODEGEN_C_C      "${CODEGEN_DIR}/robot-dbus.c")
set(CODEGEN_CXX_H    "${CODEGEN_DIR}/robot_bindings.hpp")
set(CODEGEN_CXX_C    "${CODEGEN_DIR}/robot_bindings.cpp")
set(INTF_XML         "${CMAKE_CURRENT_SOURCE_DIR}/interfaces/com.example.Robot.xml")
set(GEN_SCRIPT       "${CMAKE_CURRENT_SOURCE_DIR}/scripts/gen_cpp_bindings.py")

# Step 1: gdbus-codegen → C code
add_custom_command(
    OUTPUT  ${CODEGEN_C_H} ${CODEGEN_C_C}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CODEGEN_DIR}
    COMMAND ${GDBUS_CODEGEN}
        --generate-c-code ${CODEGEN_DIR}/robot-dbus
        --c-namespace Demo
        --interface-prefix com.example.
        ${INTF_XML}
    DEPENDS ${INTF_XML}
    COMMENT "gdbus-codegen: XML → C skeleton/proxy"
)

# Step 2: gen_cpp_bindings.py → C++ wrappers
add_custom_command(
    OUTPUT  ${CODEGEN_CXX_H} ${CODEGEN_CXX_C}
    COMMAND ${PYTHON3} ${GEN_SCRIPT} ${INTF_XML}
        --namespace Demo
        --interface-prefix com.example.
        --c-header robot-dbus.h
        --output-header ${CODEGEN_CXX_H}
        --output-impl ${CODEGEN_CXX_C}
    DEPENDS ${INTF_XML} ${GEN_SCRIPT}
    COMMENT "gen_cpp_bindings: XML → C++ wrapper classes"
)

# ============================================================
# Library: gdbus_cxx (RAII wrappers — MainLoop, BusName)
# ============================================================
add_library(gdbus_cxx STATIC
    src/gdbus_cxx.cpp
)
target_include_directories(gdbus_cxx PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${GLIB_INCLUDE_DIRS}
)
target_link_libraries(gdbus_cxx PUBLIC ${GLIB_LINK_LIBS})
if(DEFINED GLIB_CFLAGS_OTHER)
    target_compile_options(gdbus_cxx PRIVATE ${GLIB_CFLAGS_OTHER})
endif()

# ============================================================
# Library: gdbus_gen — glib-codegen C output + Python C++ output
# ============================================================
add_library(gdbus_gen STATIC
    ${CODEGEN_C_C}
    ${CODEGEN_CXX_C}
)
target_include_directories(gdbus_gen PUBLIC
    ${CODEGEN_DIR}
    ${GLIB_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/src   # for any shared types
)
target_link_libraries(gdbus_gen PUBLIC ${GLIB_LINK_LIBS})
if(DEFINED GLIB_CFLAGS_OTHER)
    target_compile_options(gdbus_gen PRIVATE ${GLIB_CFLAGS_OTHER})
endif()

# ============================================================
# Executable: Service
# ============================================================
add_executable(gdbus-demo-service
    src/demo_service.cpp
)
target_include_directories(gdbus-demo-service PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CODEGEN_DIR}
)
target_link_libraries(gdbus-demo-service PRIVATE gdbus_cxx gdbus_gen)
add_dependencies(gdbus-demo-service gdbus_gen)

# ============================================================
# Executable: Client
# ============================================================
add_executable(gdbus-demo-client
    src/demo_client.cpp
)
target_include_directories(gdbus-demo-client PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CODEGEN_DIR}
)
target_link_libraries(gdbus-demo-client PRIVATE gdbus_cxx gdbus_gen)
add_dependencies(gdbus-demo-client gdbus_gen)
