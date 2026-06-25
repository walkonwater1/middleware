# FindPahoMqttC.cmake — 查找 Eclipse Paho MQTT C 库
#
# 查找优先级:
#   1. third-lib/paho-mqtt/  (项目预置的交叉编译产物)
#   2. pkg-config             (系统安装)
#   3. PAHO_MQTT_C_ROOT       (手动指定)
#
# 输出:
#   PahoMqttC_FOUND
#   PAHO_MQTT_C_INCLUDE_DIRS
#   PAHO_MQTT_C_LIBRARIES
#   PahoMqttC::paho-mqtt3as  (IMPORTED target)

# ---- 优先查找 third-lib ----
set(_paho_third_lib "${CMAKE_CURRENT_SOURCE_DIR}/third-lib/paho-mqtt")
if(EXISTS "${_paho_third_lib}/include/MQTTAsync.h" AND
   EXISTS "${_paho_third_lib}/lib/libpaho-mqtt3as.a")
    set(PAHO_MQTT_C_INCLUDE_DIR "${_paho_third_lib}/include")
    set(PAHO_MQTT_C_LIBRARY     "${_paho_third_lib}/lib/libpaho-mqtt3as.a")
    set(_paho_found_in "third-lib/")
else()
    # ---- 备选: pkg-config ----
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(PC_PAHO_MQTT QUIET libpaho-mqtt3as)
    endif()

    find_path(PAHO_MQTT_C_INCLUDE_DIR
        NAMES MQTTAsync.h
        HINTS ${PC_PAHO_MQTT_INCLUDE_DIRS}
              ${PAHO_MQTT_C_ROOT}/include
              /usr/include /usr/local/include)

    find_library(PAHO_MQTT_C_LIBRARY
        NAMES paho-mqtt3as libpaho-mqtt3as
        HINTS ${PC_PAHO_MQTT_LIBRARY_DIRS}
              ${PAHO_MQTT_C_ROOT}/lib
              /usr/lib /usr/local/lib)

    set(_paho_found_in "system")
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(PahoMqttC
    REQUIRED_VARS PAHO_MQTT_C_LIBRARY PAHO_MQTT_C_INCLUDE_DIR)

if(PahoMqttC_FOUND)
    set(PAHO_MQTT_C_INCLUDE_DIRS "${PAHO_MQTT_C_INCLUDE_DIR}")
    set(PAHO_MQTT_C_LIBRARIES    "${PAHO_MQTT_C_LIBRARY}")

    if(NOT TARGET PahoMqttC::paho-mqtt3as)
        add_library(PahoMqttC::paho-mqtt3as UNKNOWN IMPORTED)
        set_target_properties(PahoMqttC::paho-mqtt3as PROPERTIES
            IMPORTED_LOCATION "${PAHO_MQTT_C_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${PAHO_MQTT_C_INCLUDE_DIR}"
        )
    endif()

    message(STATUS "  Paho MQTT C    : ${_paho_found_in} (${PAHO_MQTT_C_LIBRARY})")
endif()
