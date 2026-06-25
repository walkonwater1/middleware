cmake_minimum_required(VERSION 3.10)
project(gdbus_demo LANGUAGES CXX C)

# C++17 required for structured bindings, string_view, optional, etc.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================================
# CMake 模块路径 (供 toolchain 交叉编译使用)
# ============================================================
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# ============================================================
# 查找 GLib/GIO
#   1. third-lib/glib/ 优先 (交叉编译静态库)
#   2. pkg-config 兜底 (系统开发包)
# ============================================================
set(_glib_third_lib "${CMAKE_CURRENT_SOURCE_DIR}/third-lib/glib")

if(EXISTS "${_glib_third_lib}/lib/libglib-2.0.a")
    # --- third-lib 模式: 使用预编译的静态库 ---
    message(STATUS "  GLib/GIO       : third-lib/")

    set(GLIB_INCLUDE_DIRS
        "${_glib_third_lib}/include/glib-2.0"
        "${_glib_third_lib}/include/glibconfig"
    )
    set(GLIB_LINK_LIBS
        "${_glib_third_lib}/lib/libgio-2.0.a"
        "${_glib_third_lib}/lib/libgobject-2.0.a"
        "${_glib_third_lib}/lib/libgmodule-2.0.a"
        "${_glib_third_lib}/lib/libglib-2.0.a"
        "${_glib_third_lib}/lib/libffi.a"
        "${_glib_third_lib}/lib/libpcre2-8.a"
        pthread dl m
    )

    add_library(GLIB_IMPORTED INTERFACE IMPORTED)
    target_include_directories(GLIB_IMPORTED INTERFACE ${GLIB_INCLUDE_DIRS})
    target_link_libraries(GLIB_IMPORTED INTERFACE ${GLIB_LINK_LIBS})

else()
    # --- 系统模式: 使用 pkg-config ---
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GLIB REQUIRED gio-2.0)
    message(STATUS "  GLib/GIO       : system (pkg-config)")

    add_library(GLIB_IMPORTED INTERFACE IMPORTED)
    target_include_directories(GLIB_IMPORTED INTERFACE ${GLIB_INCLUDE_DIRS})
    target_link_libraries(GLIB_IMPORTED INTERFACE ${GLIB_LIBRARIES})
    target_compile_options(GLIB_IMPORTED INTERFACE ${GLIB_CFLAGS_OTHER})
endif()

# ============================================================
# Library: gdbus_cxx (RAII wrappers for GLib/GDBus)
# ============================================================
add_library(gdbus_cxx STATIC
    src/gdbus_cxx.cpp
)
target_include_directories(gdbus_cxx PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(gdbus_cxx PUBLIC GLIB_IMPORTED)

# ============================================================
# Shared source files (VehicleService + VehicleClient)
# ============================================================
set(SHARED_SOURCES
    src/vehicle_service.cpp
    src/vehicle_client.cpp
)

# ============================================================
# Executable: Service
# ============================================================
add_executable(gdbus-demo-service
    src/demo_service.cpp
    ${SHARED_SOURCES}
)
target_link_libraries(gdbus-demo-service PRIVATE gdbus_cxx)
target_include_directories(gdbus-demo-service PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# ============================================================
# Executable: Client
# ============================================================
add_executable(gdbus-demo-client
    src/demo_client.cpp
    ${SHARED_SOURCES}
)
target_link_libraries(gdbus-demo-client PRIVATE gdbus_cxx)
target_include_directories(gdbus-demo-client PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
