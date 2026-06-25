#!/usr/bin/env bash
# ============================================================
#  build_deps.sh — 编译 GLib/GIO 第三方依赖库
#
#  自动下载源码交叉编译为 .a 静态库，产物放入 third-lib/
#  供 CMake 直接链接，无需在目标板端安装开发包。
#
#  用法:
#    bash scripts/build_deps.sh              # 本地编译 (x86_64)
#    bash scripts/build_deps.sh aarch64      # 交叉编译 ARM64
#    bash scripts/build_deps.sh armhf        # 交叉编译 ARM32
#
#  前置条件 (build machine):
#    sudo apt install cmake make wget build-essential meson ninja-build pkg-config
#    sudo apt install libglib2.0-dev        # 提供 glib-genmarshal 等原生工具
#
#    ARM 交叉编译额外需要:
#    sudo apt install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu    # ARM64
#    sudo apt install gcc-arm-linux-gnueabihf g++-arm-linux-gnueabihf # ARM32
#
#  产物:
#    third-lib/glib/
#    ├── include/glib-2.0/     (glib + gio 头文件)
#    ├── include/glibconfig/   (glibconfig.h — 平台生成)
#    └── lib/
#        ├── libglib-2.0.a
#        ├── libgio-2.0.a
#        ├── libgobject-2.0.a
#        ├── libgmodule-2.0.a
#        ├── libffi.a
#        └── libpcre2-8.a
# ============================================================

set -e

ARCH="${1:-x86_64}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
THIRD_LIB_DIR="$PROJECT_DIR/third-lib/glib"
BUILD_DIR="$PROJECT_DIR/build_deps"
NPROC=$(nproc 2>/dev/null || echo 4)

# ---- 版本定义 ----
LIBFFI_VERSION="3.4.6"
PCRE2_VERSION="10.43"
GLIB_VERSION="2.76.6"

# ---- 工具链配置 ----
case "$ARCH" in
    x86_64|amd64|native)
        CROSS_PREFIX=""
        TARGET_TRIPLE="x86_64-linux-gnu"
        ;;
    aarch64|arm64)
        CROSS_PREFIX="aarch64-linux-gnu"
        TARGET_TRIPLE="aarch64-linux-gnu"
        ;;
    armhf|armv7l)
        CROSS_PREFIX="arm-linux-gnueabihf"
        TARGET_TRIPLE="arm-linux-gnueabihf"
        ;;
    *)
        echo "[ERR] 未知架构: $ARCH"
        echo "      支持: x86_64 | aarch64 | armhf"
        exit 1
        ;;
esac

CC="${CROSS_PREFIX:+${CROSS_PREFIX}-}gcc"
CXX="${CROSS_PREFIX:+${CROSS_PREFIX}-}g++"
AR="${CROSS_PREFIX:+${CROSS_PREFIX}-}ar"
STRIP="${CROSS_PREFIX:+${CROSS_PREFIX}-}strip"

if [ -n "$CROSS_PREFIX" ] && ! command -v "$CC" &>/dev/null; then
    echo "[ERR] 交叉编译器 $CC 未找到!"
    echo "      安装: sudo apt install gcc-${CROSS_PREFIX} g++-${CROSS_PREFIX}"
    exit 1
fi

IS_CROSS=0
[ -n "$CROSS_PREFIX" ] && IS_CROSS=1

echo "============================================================"
echo "  GDBus 第三方依赖编译"
echo "  目标架构: ${ARCH}  (CC=$CC)"
echo "  交叉编译: $([ $IS_CROSS -eq 1 ] && echo 'YES' || echo 'NO')"
echo "  输出目录: $THIRD_LIB_DIR"
echo "============================================================"

# 清理旧构建
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

# 清理旧产物
rm -rf "$THIRD_LIB_DIR"
mkdir -p "$THIRD_LIB_DIR/include/glib-2.0"
mkdir -p "$THIRD_LIB_DIR/include/glibconfig"
mkdir -p "$THIRD_LIB_DIR/lib"
mkdir -p "$THIRD_LIB_DIR/lib/pkgconfig"

# ============================================================
# 通用编译参数
# ============================================================
CFLAGS_COMMON="-O2 -fPIC"
if [ $IS_CROSS -eq 1 ]; then
    CFLAGS_COMMON="$CFLAGS_COMMON --sysroot=/"
fi

# ============================================================
# 0. 交叉编译时创建 Meson cross-file
# ============================================================
MESON_CROSS_FILE=""
if [ $IS_CROSS -eq 1 ]; then
    MESON_CROSS_FILE="$BUILD_DIR/meson-cross.txt"
    cat > "$MESON_CROSS_FILE" << EOF
[binaries]
c = '$CC'
cpp = '$CXX'
ar = '$AR'
strip = '$STRIP'
pkgconfig = 'pkg-config'

[properties]
sys_root = '/'
pkg_config_libdir = '/usr/lib/${TARGET_TRIPLE}/pkgconfig'

[host_machine]
system = 'linux'
cpu_family = '$(echo $ARCH | sed s/armhf/arm/ | sed s/armv7l/arm/ | sed s/aarch64/aarch64/)'
cpu = '$(echo $ARCH | sed s/armhf/arm/ | sed s/armv7l/arm/ | sed s/aarch64/aarch64/)'
endian = 'little'
EOF
    echo "[INFO] Meson cross-file: $MESON_CROSS_FILE"
fi

# ============================================================
# 1. 编译 libffi
# ============================================================
echo ""
echo "--- [1/3] 编译 libffi v${LIBFFI_VERSION} ---"

LIBFFI_SRC="$BUILD_DIR/libffi-${LIBFFI_VERSION}"

if [ ! -f "$BUILD_DIR/libffi-${LIBFFI_VERSION}.tar.gz" ]; then
    echo "[INFO] 下载 libffi ..."
    wget -q --show-progress -O "$BUILD_DIR/libffi-${LIBFFI_VERSION}.tar.gz" \
        "https://github.com/libffi/libffi/releases/download/v${LIBFFI_VERSION}/libffi-${LIBFFI_VERSION}.tar.gz"
fi

tar xzf "$BUILD_DIR/libffi-${LIBFFI_VERSION}.tar.gz" -C "$BUILD_DIR"
cd "$LIBFFI_SRC"

CONFIGURE_ARGS=(
    --prefix="$BUILD_DIR/staging"
    --disable-shared
    --enable-static
    --disable-docs
)

if [ $IS_CROSS -eq 1 ]; then
    CONFIGURE_ARGS+=(--host="$TARGET_TRIPLE" CC="$CC" AR="$AR")
fi

./configure "${CONFIGURE_ARGS[@]}"
make -j"$NPROC"
make install

cp "$BUILD_DIR/staging/lib/libffi.a" "$THIRD_LIB_DIR/lib/"
cp -r "$BUILD_DIR/staging/include/"* "$THIRD_LIB_DIR/include/" 2>/dev/null || true

echo "[OK] libffi 完成"

# ============================================================
# 2. 编译 pcre2
# ============================================================
echo ""
echo "--- [2/3] 编译 pcre2 v${PCRE2_VERSION} ---"

PCRE2_SRC="$BUILD_DIR/pcre2-${PCRE2_VERSION}"

if [ ! -f "$BUILD_DIR/pcre2-${PCRE2_VERSION}.tar.bz2" ]; then
    echo "[INFO] 下载 pcre2 ..."
    wget -q --show-progress -O "$BUILD_DIR/pcre2-${PCRE2_VERSION}.tar.bz2" \
        "https://github.com/PCRE2Project/pcre2/releases/download/pcre2-${PCRE2_VERSION}/pcre2-${PCRE2_VERSION}.tar.bz2"
fi

tar xjf "$BUILD_DIR/pcre2-${PCRE2_VERSION}.tar.bz2" -C "$BUILD_DIR"
cd "$PCRE2_SRC"

PCRE2_CMAKE_ARGS=(
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    -DBUILD_SHARED_LIBS=OFF
    -DBUILD_STATIC_LIBS=ON
    -DPCRE2_BUILD_PCRE2_16=OFF
    -DPCRE2_BUILD_PCRE2_32=OFF
    -DPCRE2_BUILD_TESTS=OFF
    -DPCRE2_SUPPORT_UNICODE=ON
    -DPCRE2_SUPPORT_JIT=OFF
    -DCMAKE_INSTALL_PREFIX="$BUILD_DIR/staging"
)

if [ $IS_CROSS -eq 1 ]; then
    PCRE2_CMAKE_ARGS+=(
        -DCMAKE_C_COMPILER="$CC"
        -DCMAKE_CXX_COMPILER="$CXX"
        -DCMAKE_SYSTEM_NAME=Linux
        -DCMAKE_SYSTEM_PROCESSOR="$ARCH"
        -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER
        -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY
        -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY
    )
fi

cmake -B build_pcre2 "${PCRE2_CMAKE_ARGS[@]}"
cmake --build build_pcre2 -j"$NPROC"
cmake --install build_pcre2

cp "$BUILD_DIR/staging/lib/libpcre2-8.a" "$THIRD_LIB_DIR/lib/"
cp -r "$BUILD_DIR/staging/include/"* "$THIRD_LIB_DIR/include/" 2>/dev/null || true

echo "[OK] pcre2 完成"

# ============================================================
# 3. 编译 GLib
# ============================================================
echo ""
echo "--- [3/3] 编译 GLib v${GLIB_VERSION} ---"

GLIB_SRC="$BUILD_DIR/glib-${GLIB_VERSION}"

# GLib 主版本号 (如 2.76.6 → 2.76)
GLIB_MAJOR_MINOR="$(echo $GLIB_VERSION | cut -d. -f1,2)"

if [ ! -f "$BUILD_DIR/glib-${GLIB_VERSION}.tar.xz" ]; then
    echo "[INFO] 下载 GLib ..."
    wget -q --show-progress -O "$BUILD_DIR/glib-${GLIB_VERSION}.tar.xz" \
        "https://download.gnome.org/sources/glib/${GLIB_MAJOR_MINOR}/glib-${GLIB_VERSION}.tar.xz"
fi

tar xJf "$BUILD_DIR/glib-${GLIB_VERSION}.tar.xz" -C "$BUILD_DIR"
cd "$GLIB_SRC"

# 交叉编译时需要设置 PKG_CONFIG_PATH 指向我们的 staging lib
export PKG_CONFIG_PATH="$BUILD_DIR/staging/lib/pkgconfig:$PKG_CONFIG_PATH"

# GLib Meson 配置
GLIB_MESON_ARGS=(
    --prefix="$BUILD_DIR/staging"
    --libdir=lib
    --buildtype=release
    -Ddefault_library=static
    # 禁用不必要的功能, 减少编译时间
    -Dtests=false
    -Dinstalled_tests=false
    -Dnls=disabled
    -Dselinux=disabled
    -Dlibmount=disabled
    -Dxattr=false
    -Dlibelf=disabled
    -Dglib_debug=disabled
    -Dglib_assert=false
    -Dglib_checks=false
    -Dman-pages=disabled
    -Ddocumentation=false
    -Dintrospection=disabled
    # 我们只需要 GIO (含 GDBus)
    -Doss_fuzz=disabled
)

if [ $IS_CROSS -eq 1 ]; then
    GLIB_MESON_ARGS+=(--cross-file="$MESON_CROSS_FILE")
fi

# 设置 CFLAGS 让编译器找到我们的 libffi 和 pcre2
export CFLAGS="-I$BUILD_DIR/staging/include"
export LDFLAGS="-L$BUILD_DIR/staging/lib"

meson setup build_glib "${GLIB_MESON_ARGS[@]}"
meson compile -C build_glib -j"$NPROC"
DESTDIR="$BUILD_DIR/staging" meson install -C build_glib --no-rebuild

# ---- 拷贝头文件 ----
# GLib 头文件结构:
#   安装目录/include/glib-2.0/   → glib/*.h, gio/*.h 等
#   安装目录/lib/glib-2.0/include/ → glibconfig.h (平台生成)

STAGING="$BUILD_DIR/staging"

# 主头文件 (glib + gio + gobject + gmodule)
if [ -d "$STAGING/include/glib-2.0" ]; then
    cp -r "$STAGING/include/glib-2.0/"* "$THIRD_LIB_DIR/include/glib-2.0/"
fi

# glibconfig.h — 平台生成的关键头文件
GLIBCONFIG_DIRS=(
    "$STAGING/lib/glib-2.0/include"
    "$STAGING/lib/${TARGET_TRIPLE}/glib-2.0/include"
    "$STAGING/lib/x86_64-linux-gnu/glib-2.0/include"
)
for dir in "${GLIBCONFIG_DIRS[@]}"; do
    if [ -d "$dir" ]; then
        cp -r "$dir/"* "$THIRD_LIB_DIR/include/glibconfig/"
        echo "[INFO] glibconfig.h from: $dir"
        break
    fi
done

# ---- 拷贝静态库 ----
cp "$STAGING/lib/libglib-2.0.a"     "$THIRD_LIB_DIR/lib/" 2>/dev/null || true
cp "$STAGING/lib/libgio-2.0.a"       "$THIRD_LIB_DIR/lib/" 2>/dev/null || true
cp "$STAGING/lib/libgobject-2.0.a"   "$THIRD_LIB_DIR/lib/" 2>/dev/null || true
cp "$STAGING/lib/libgmodule-2.0.a"   "$THIRD_LIB_DIR/lib/" 2>/dev/null || true

# ---- 拷贝 pkg-config 文件 ----
cp "$STAGING/lib/pkgconfig/"*.pc "$THIRD_LIB_DIR/lib/pkgconfig/" 2>/dev/null || true

echo "[OK] GLib 完成"

# ============================================================
# 清理
# ============================================================
rm -rf "$BUILD_DIR"

echo ""
echo "============================================================"
echo "  全部完成!  目标架构: ${ARCH}"
echo ""
echo "  third-lib/glib/"
echo "  ├── include/"
echo "  │   ├── glib-2.0/     (GLib + GIO + GObject 头文件)"
echo "  │   └── glibconfig/   (glibconfig.h)"
echo "  └── lib/"
echo "      ├── libglib-2.0.a"
echo "      ├── libgio-2.0.a"
echo "      ├── libgobject-2.0.a"
echo "      ├── libgmodule-2.0.a"
echo "      ├── libffi.a"
echo "      └── libpcre2-8.a"
echo ""
echo "  构建项目 (使用 third-lib):"
if [ $IS_CROSS -eq 1 ]; then
    echo "    mkdir build && cd build"
    echo "    cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/toolchain_${ARCH}.cmake"
    echo "    make -j\$(nproc)"
else
    echo "    mkdir build && cd build && cmake .. && make -j\$(nproc)"
fi
echo "============================================================"
