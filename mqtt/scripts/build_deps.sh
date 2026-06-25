#!/usr/bin/env bash
# ============================================================
#  build_deps.sh — 编译第三方依赖库 (Paho MQTT + protobuf-c)
#
#  自动下载源码编译为 .a 静态库，产物放入 third-lib/ 供 CMake 直接链接。
#
#  用法:
#    bash scripts/build_deps.sh              # 本地编译 (x86_64)
#    bash scripts/build_deps.sh x86_64       # 同上
#    bash scripts/build_deps.sh aarch64      # 交叉编译 ARM64
#    bash scripts/build_deps.sh armv7l       # 交叉编译 ARM32
#
#  前置条件:
#    sudo apt install cmake make wget build-essential
#
#    ARM 交叉编译额外需要:
#    sudo apt install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu    # ARM64
#    sudo apt install gcc-arm-linux-gnueabihf g++-arm-linux-gnueabihf # ARM32
# ============================================================

set -e

ARCH="${1:-x86_64}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
THIRD_LIB_DIR="$PROJECT_DIR/third-lib"
BUILD_DIR="$PROJECT_DIR/build_deps"
NPROC=$(nproc)

# ---- 工具链配置 ----
case "$ARCH" in
    x86_64|amd64|native)
        CROSS_PREFIX=""
        ;;
    aarch64|arm64)
        CROSS_PREFIX="aarch64-linux-gnu"
        ;;
    armv7l|armhf)
        CROSS_PREFIX="arm-linux-gnueabihf"
        ;;
    *)
        echo "[ERR] 未知架构: $ARCH"
        echo "      支持: x86_64 | aarch64 | armv7l"
        exit 1
        ;;
esac

CC="${CROSS_PREFIX}${CROSS_PREFIX:+-}gcc"
CXX="${CROSS_PREFIX}${CROSS_PREFIX:+-}g++"
AR="${CROSS_PREFIX}${CROSS_PREFIX:+-}ar"

if [ -n "$CROSS_PREFIX" ] && ! command -v "$CC" &>/dev/null; then
    echo "[ERR] 交叉编译器 $CC 未找到!"
    echo "      安装: sudo apt install gcc-${CROSS_PREFIX} g++-${CROSS_PREFIX}"
    exit 1
fi

echo "============================================================"
echo "  交叉编译第三方依赖"
echo "  目标架构: ${ARCH:-x86_64}  (CC=$CC)"
echo "  输出目录: $THIRD_LIB_DIR"
echo "============================================================"

rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
mkdir -p "$THIRD_LIB_DIR/paho-mqtt/include"
mkdir -p "$THIRD_LIB_DIR/paho-mqtt/lib"
mkdir -p "$THIRD_LIB_DIR/protobuf-c/include"
mkdir -p "$THIRD_LIB_DIR/protobuf-c/lib"

# ============================================================
# 1. 编译 Eclipse Paho MQTT C (异步版本)
# ============================================================
echo ""
echo "--- [1/2] 编译 Paho MQTT C ---"

PAHO_VERSION="1.3.13"
PAHO_SRC="$BUILD_DIR/paho.mqtt.c-${PAHO_VERSION}"

if [ ! -f "$BUILD_DIR/paho-${PAHO_VERSION}.tar.gz" ]; then
    echo "[INFO] 下载 Paho MQTT C v${PAHO_VERSION} ..."
    wget --show-progress -O "$BUILD_DIR/paho-${PAHO_VERSION}.tar.gz" \
        "https://github.com/eclipse-paho/paho.mqtt.c/archive/refs/tags/v${PAHO_VERSION}.tar.gz"
fi

echo "[INFO] 解压 ..."
tar xzf "$BUILD_DIR/paho-${PAHO_VERSION}.tar.gz" -C "$BUILD_DIR"

cd "$PAHO_SRC"

# 构建异步版本 (libpaho-mqtt3as)
PAHO_CMAKE_ARGS=(
    -DPAHO_WITH_SSL=OFF
    -DPAHO_ENABLE_TESTING=OFF
    -DPAHO_BUILD_STATIC=ON
    -DPAHO_BUILD_SHARED=OFF
    -DPAHO_BUILD_SAMPLES=OFF
    -DPAHO_HIGH_PERFORMANCE=ON
)

if [ -n "$CROSS_PREFIX" ]; then
    PAHO_CMAKE_ARGS+=(
        -DCMAKE_C_COMPILER="$CC"
        -DCMAKE_CXX_COMPILER="$CXX"
        -DCMAKE_AR="$AR"
        -DCMAKE_SYSTEM_NAME=Linux
        -DCMAKE_SYSTEM_PROCESSOR="$ARCH"
    )
fi

cmake -B build_paho "${PAHO_CMAKE_ARGS[@]}"
cmake --build build_paho -j"$NPROC"

# 拷贝产物
# 注意: Paho 源码中异步库叫 paho-mqtt3a，Debian 包名叫 libpaho-mqtt3as (s=static)
cp src/MQTTAsync.h src/MQTTProperties.h src/MQTTReasonCodes.h src/MQTTSubscribeOpts.h \
   "$THIRD_LIB_DIR/paho-mqtt/include/" 2>/dev/null || true
# MQTTExportDeclarations.h 是 cmake 生成的头文件
cp build_paho/src/MQTTExportDeclarations.h \
   "$THIRD_LIB_DIR/paho-mqtt/include/" 2>/dev/null || true

# 查找编译产物 (异步静态库: libpaho-mqtt3a.a)
PAHO_LIB=$(find build_paho/src -maxdepth 1 -name "libpaho-mqtt3a*.a" ! -name "*.so" | head -1)
if [ -z "$PAHO_LIB" ]; then
    echo "[ERR] 找不到 libpaho-mqtt3a.a!"
    exit 1
fi
# 统一命名为 libpaho-mqtt3as.a 与 CMake Find 模块一致
cp "$PAHO_LIB" "$THIRD_LIB_DIR/paho-mqtt/lib/libpaho-mqtt3as.a"

echo "[OK] Paho MQTT C 编译完成"
echo "     源文件: $PAHO_LIB"
echo "     拷贝为: $THIRD_LIB_DIR/paho-mqtt/lib/libpaho-mqtt3as.a"

echo "[OK] Paho MQTT C 编译完成"
echo "     头文件: $THIRD_LIB_DIR/paho-mqtt/include/"
echo "     库文件: $THIRD_LIB_DIR/paho-mqtt/lib/libpaho-mqtt3as.a"

# ============================================================
# 2. 编译 protobuf-c (运行时库)
# ============================================================
echo ""
echo "--- [2/2] 编译 protobuf-c ---"

PROTOBUF_C_VERSION="1.5.0"
PROTOBUF_C_SRC="$BUILD_DIR/protobuf-c-${PROTOBUF_C_VERSION}"

# 需要依赖 protobuf 开发包 (提供 protoc 和头文件)
if ! pkg-config --exists protobuf || \
   ! [ -f "/usr/include/google/protobuf/compiler/command_line_interface.h" ]; then
    echo "[INFO] 安装 protobuf 开发包 + 编译器头文件..."
    sudo apt install -y libprotobuf-dev protobuf-compiler libprotoc-dev
fi

if [ ! -f "$BUILD_DIR/protobuf-c-${PROTOBUF_C_VERSION}.tar.gz" ]; then
    echo "[INFO] 下载 protobuf-c v${PROTOBUF_C_VERSION} ..."
    wget --show-progress -O "$BUILD_DIR/protobuf-c-${PROTOBUF_C_VERSION}.tar.gz" \
        "https://github.com/protobuf-c/protobuf-c/releases/download/v${PROTOBUF_C_VERSION}/protobuf-c-${PROTOBUF_C_VERSION}.tar.gz"
fi

echo "[INFO] 解压 ..."
tar xzf "$BUILD_DIR/protobuf-c-${PROTOBUF_C_VERSION}.tar.gz" -C "$BUILD_DIR"

cd "$PROTOBUF_C_SRC"

# 查找 protobuf 头文件路径 (Ubuntu 22.04 可能不在默认搜索路径)
PROTOBUF_PREFIX=$(pkg-config --variable=prefix protobuf 2>/dev/null || echo "/usr")
PROTOBUF_INCLUDE="$PROTOBUF_PREFIX/include"
if ! [ -f "$PROTOBUF_INCLUDE/google/protobuf/compiler/command_line_interface.h" ]; then
    # Ubuntu 22.04 有些版本的头文件在 /usr/include 但 pkg-config 指错了
    PROTOBUF_INCLUDE="/usr/include"
fi
echo "[INFO] protobuf include: $PROTOBUF_INCLUDE"

CONFIGURE_ARGS=(
    --prefix="$BUILD_DIR/protobuf-c-install"
    --disable-shared
    --enable-static
    CFLAGS="-I${PROTOBUF_INCLUDE}"
    CXXFLAGS="-I${PROTOBUF_INCLUDE}"
)

if [ -n "$CROSS_PREFIX" ]; then
    CONFIGURE_ARGS+=(
        --host="$CROSS_PREFIX"
        CC="$CC"
        CXX="$CXX"
        AR="$AR"
    )
fi

./configure "${CONFIGURE_ARGS[@]}"

make -j"$NPROC"
make install

# 拷贝产物 (保持 protobuf-c/ 子目录结构, pb-c.h 需要 #include <protobuf-c/...>)
mkdir -p "$THIRD_LIB_DIR/protobuf-c/include/protobuf-c"
cp "$BUILD_DIR/protobuf-c-install/include/protobuf-c/"*.h \
   "$THIRD_LIB_DIR/protobuf-c/include/protobuf-c/"
cp "$BUILD_DIR/protobuf-c-install/lib/libprotobuf-c.a" \
   "$THIRD_LIB_DIR/protobuf-c/lib/"

echo "[OK] protobuf-c 编译完成"
echo "     头文件: $THIRD_LIB_DIR/protobuf-c/include/"
echo "     库文件: $THIRD_LIB_DIR/protobuf-c/lib/libprotobuf-c.a"

# ============================================================
# 清理
# ============================================================
rm -rf "$BUILD_DIR"

echo ""
echo "============================================================"
echo "  全部完成!  目标架构: ${ARCH}"
echo ""
echo "  third-lib/"
echo "  ├── paho-mqtt/"
echo "  │   ├── include/    (.h)"
echo "  │   └── lib/        (libpaho-mqtt3as.a)"
echo "  └── protobuf-c/"
echo "      ├── include/    (.h)"
echo "      └── lib/        (libprotobuf-c.a)"
echo ""
echo "  构建项目:"
if [ -z "$CROSS_PREFIX" ]; then
    echo "    mkdir build && cd build && cmake .. && make -j\$(nproc)"
else
    echo "    mkdir build && cd build"
    echo "    cmake .. -DCMAKE_C_COMPILER=${CC} -DCMAKE_CXX_COMPILER=${CXX}"
    echo "    make -j\$(nproc)"
fi
echo "============================================================"
