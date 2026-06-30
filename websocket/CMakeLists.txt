cmake_minimum_required(VERSION 3.16)
project(websocket-robot-demo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================
# websocketpp — header-only 库, 无 pkg-config/CMake config
#   头文件在 /usr/include/ (默认搜索路径，无需额外 include dirs)
#   只检查关键头文件是否存在
# ============================================================
find_file(WEBSOCKETPP_SERVER websocketpp/server.hpp)
if(NOT WEBSOCKETPP_SERVER)
    message(FATAL_ERROR
        "未找到 websocketpp/server.hpp!\n"
        "请安装: sudo apt-get install libwebsocketpp-dev")
endif()

# 依赖 Boost.Asio (system)
find_package(Boost REQUIRED COMPONENTS system)
find_package(Threads REQUIRED)

# (不需要 OpenSSL — 使用 ws:// asio_no_tls config)

# ============================================================
# Server
# ============================================================
add_executable(ws-server server.cpp)
target_link_libraries(ws-server
    PRIVATE
    Boost::system
    Threads::Threads
)

# ============================================================
# Client
# ============================================================
add_executable(ws-client client.cpp)
target_link_libraries(ws-client
    PRIVATE
    Boost::system
    Threads::Threads
)
