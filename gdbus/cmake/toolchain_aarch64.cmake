# CMake toolchain file for ARM64 (aarch64) cross-compilation
#
# Usage:
#   cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/toolchain_aarch64.cmake

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# Toolchain binaries
set(CMAKE_C_COMPILER   aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
set(CMAKE_AR           aarch64-linux-gnu-ar)
set(CMAKE_STRIP        aarch64-linux-gnu-strip)

# Search for libraries only in the target sysroot
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
