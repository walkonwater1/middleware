# FindGLib.cmake — 查找 GLib/GIO 库
#
# 查找优先级:
#   1. third-lib/glib/  (项目预置的交叉编译产物)
#   2. pkg-config        (系统安装)
#   3. GLIB_ROOT         (手动指定)
#
# 输出:
#   GLib_FOUND
#   GLIB_INCLUDE_DIRS
#   GLIB_LIBRARIES       (glib-2.0 + gobject-2.0 + gio-2.0 + gmodule-2.0)
#   GLib::glib-2.0       (IMPORTED target — 聚合)

# ---- 优先查找 third-lib ----
set(_glib_third_lib "${CMAKE_CURRENT_SOURCE_DIR}/third-lib/glib")

if(EXISTS "${_glib_third_lib}/lib/libglib-2.0.a")
    set(_glib_using "third-lib/")

    # 两个 include 目录: 主头文件 + glibconfig.h
    set(_glib_include_main "${_glib_third_lib}/include/glib-2.0")
    set(_glib_include_config "${_glib_third_lib}/include/glibconfig")
    set(GLIB_INCLUDE_DIRS "${_glib_include_main};${_glib_include_config}")

    # 静态库文件
    set(_glib_lib_dir "${_glib_third_lib}/lib")
    set(GLIB_LIB_GLIB    "${_glib_lib_dir}/libglib-2.0.a")
    set(GLIB_LIB_GOBJECT "${_glib_lib_dir}/libgobject-2.0.a")
    set(GLIB_LIB_GIO     "${_glib_lib_dir}/libgio-2.0.a")
    set(GLIB_LIB_GMODULE "${_glib_lib_dir}/libgmodule-2.0.a")
    set(GLIB_LIB_FFI     "${_glib_lib_dir}/libffi.a")
    set(GLIB_LIB_PCRE2   "${_glib_lib_dir}/libpcre2-8.a")

    # 所有 GLib 相关库的集合 (gio 依赖 gobject, gobject 依赖 glib)
    set(GLIB_LIBRARIES
        "${GLIB_LIB_GIO}"
        "${GLIB_LIB_GOBJECT}"
        "${GLIB_LIB_GMODULE}"
        "${GLIB_LIB_GLIB}"
        "${GLIB_LIB_FFI}"
        "${GLIB_LIB_PCRE2}"
    )

    # System libraries that GLib always needs
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        list(APPEND GLIB_LIBRARIES pthread dl m)
    endif()

    set(GLIB_FOUND TRUE)

else()
    # ---- 备选: pkg-config ----
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(PC_GLIB QUIET gio-2.0)
    endif()

    find_path(GLIB_INCLUDE_DIR_MAIN
        NAMES glib.h
        PATH_SUFFIXES glib-2.0
        HINTS ${PC_GLIB_INCLUDE_DIRS}
              ${GLIB_ROOT}/include/glib-2.0
              /usr/include/glib-2.0
              /usr/local/include/glib-2.0)

    find_path(GLIB_INCLUDE_DIR_CONFIG
        NAMES glibconfig.h
        PATH_SUFFIXES glib-2.0/include
        HINTS ${PC_GLIB_LIBDIR}
              ${PC_GLIB_LIBRARY_DIRS}
              /usr/lib/x86_64-linux-gnu/glib-2.0/include
              /usr/lib/aarch64-linux-gnu/glib-2.0/include
              /usr/lib/arm-linux-gnueabihf/glib-2.0/include
              /usr/lib/glib-2.0/include)

    if(GLIB_INCLUDE_DIR_MAIN AND GLIB_INCLUDE_DIR_CONFIG)
        set(GLIB_INCLUDE_DIRS "${GLIB_INCLUDE_DIR_MAIN};${GLIB_INCLUDE_DIR_CONFIG}")
    endif()

    # Use pkg-config for libraries when available
    if(PC_GLIB_FOUND)
        set(GLIB_LIBRARIES ${PC_GLIB_LIBRARIES})
        set(_glib_using "system (pkg-config)")
    else()
        find_library(GLIB_LIB_GLIB    NAMES glib-2.0)
        find_library(GLIB_LIB_GOBJECT NAMES gobject-2.0)
        find_library(GLIB_LIB_GIO     NAMES gio-2.0)
        find_library(GLIB_LIB_GMODULE NAMES gmodule-2.0)
        set(GLIB_LIBRARIES
            ${GLIB_LIB_GIO}
            ${GLIB_LIB_GOBJECT}
            ${GLIB_LIB_GMODULE}
            ${GLIB_LIB_GLIB})
        set(_glib_using "system (manual)")
    endif()

endif()

# ---- 标准查找 ----
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(GLIB
    REQUIRED_VARS GLIB_INCLUDE_DIRS GLIB_LIBRARIES)

# ---- 创建 IMPORTED target ----
if(GLIB_FOUND AND NOT TARGET GLib::glib)
    add_library(GLIB::glib INTERFACE IMPORTED)
    target_include_directories(GLIB::glib INTERFACE ${GLIB_INCLUDE_DIRS})

    if(EXISTS "${_glib_third_lib}/lib/libglib-2.0.a")
        # third-lib mode: link all static libs individually
        target_link_libraries(GLIB::glib INTERFACE
            "${GLIB_LIB_GIO}"
            "${GLIB_LIB_GOBJECT}"
            "${GLIB_LIB_GMODULE}"
            "${GLIB_LIB_GLIB}"
            "${GLIB_LIB_FFI}"
            "${GLIB_LIB_PCRE2}"
            pthread dl m
        )
    else()
        # system mode: use pkg-config flags
        target_link_libraries(GLIB::glib INTERFACE ${GLIB_LIBRARIES})
        if(PC_GLIB_FOUND)
            target_compile_options(GLIB::glib INTERFACE ${PC_GLIB_CFLAGS_OTHER})
        endif()
    endif()

    message(STATUS "  GLib/GIO       : ${_glib_using}")
endif()
