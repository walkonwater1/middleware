cmake_minimum_required(VERSION 3.10)
project(mqtt_demo C)

# 查找 paho-mqtt C 库
find_package(PkgConfig REQUIRED)
pkg_check_modules(PAHO_MQTT REQUIRED libpaho-mqtt3as)

# 发布者
add_executable(publisher publisher.c)
target_include_directories(publisher PRIVATE ${PAHO_MQTT_INCLUDE_DIRS})
target_compile_options(publisher PRIVATE ${PAHO_MQTT_CFLAGS_OTHER})
target_link_libraries(publisher ${PAHO_MQTT_LDFLAGS})

# 订阅者
add_executable(subscriber subscriber.c)
target_include_directories(subscriber PRIVATE ${PAHO_MQTT_INCLUDE_DIRS})
target_compile_options(subscriber PRIVATE ${PAHO_MQTT_CFLAGS_OTHER})
target_link_libraries(subscriber ${PAHO_MQTT_LDFLAGS})
