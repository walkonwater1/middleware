# CMake toolchain file for ARM32 (armhf) cross-compilation
#
# Usage:
#   cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/toolchain_armhf.cmake

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

# Toolchain binaries
set(CMAKE_C_COMPILER   arm-linux-gnueabihf-gcc)
set(CMAKE_CXX_COMPILER arm-linux-gnueabihf-g++)
set(CMAKE_AR           arm-linux-gnueabihf-ar)
set(CMAKE_STRIP        arm-linux-gnueabihf-strip)

# Search for libraries only in the target sysroot
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
