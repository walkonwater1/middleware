cmake_minimum_required(VERSION 3.16)
project(websocket-robot-demo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- 查找 websocketpp (header-only) ----
find_package(websocketpp QUIET)

if(NOT websocketpp_FOUND)
    # 尝试 pkg-config
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(WEBSOCKETPP websocketpp)
    endif()
endif()

# websocketpp 依赖 Boost (asio, system)
find_package(Boost REQUIRED COMPONENTS system)
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

# ---- Server ----
add_executable(ws-server server.cpp)
target_link_libraries(ws-server
    PRIVATE
    Boost::system
    Threads::Threads
    OpenSSL::SSL
    OpenSSL::Crypto
)
target_include_directories(ws-server PRIVATE ${WEBSOCKETPP_INCLUDE_DIRS})

# ---- Client ----
add_executable(ws-client client.cpp)
target_link_libraries(ws-client
    PRIVATE
    Boost::system
    Threads::Threads
    OpenSSL::SSL
    OpenSSL::Crypto
)
target_include_directories(ws-client PRIVATE ${WEBSOCKETPP_INCLUDE_DIRS})
