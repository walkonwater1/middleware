#!/usr/bin/env bash
# ============================================================
#  build.sh — 编译 gRPC Demo
#
#  用法:
#    bash scripts/build.sh              # Debug 编译
#    bash scripts/build.sh release      # Release 编译
#    bash scripts/build.sh clean        # 清理编译产物
#
#  依赖:
#    sudo apt-get install -y libgrpc++-dev protobuf-compiler-grpc
# ============================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build"
BUILD_TYPE="${1:-Debug}"

# ---- 颜色 ----
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

info()  { echo -e "${GREEN}[INFO]${NC}  $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }
err()   { echo -e "${RED}[ERR]${NC}  $*"; }
title() { echo -e "${CYAN}$*${NC}"; }

# ============================================================
# 检查依赖
# ============================================================
check_deps() {
    local missing=()

    # 检查 protoc
    if ! command -v protoc &>/dev/null; then
        missing+=("protobuf-compiler")
    fi

    # 检查 gRPC 头文件
    if ! dpkg -s libgrpc++-dev &>/dev/null 2>&1; then
        missing+=("libgrpc++-dev")
    fi

    # 检查 grpc_cpp_plugin
    if ! command -v grpc_cpp_plugin &>/dev/null; then
        missing+=("protobuf-compiler-grpc")
    fi

    if [ ${#missing[@]} -gt 0 ]; then
        err "缺少依赖: ${missing[*]}"
        echo ""
        echo "  安装命令:"
        echo "    sudo apt-get install -y ${missing[*]}"
        exit 1
    fi

    info "依赖检查通过"
}

# ============================================================
# 编译
# ============================================================
do_build() {
    info "开始编译 (Build Type: ${BUILD_TYPE})..."
    echo ""

    check_deps

    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"

    cmake "$PROJECT_DIR" \
        -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

    make -j"$(nproc 2>/dev/null || echo 4)"

    echo ""
    if [ -x "$BUILD_DIR/grpc-server" ] && [ -x "$BUILD_DIR/grpc-client" ]; then
        info "编译成功!"
        echo ""
        echo "  运行:"
        echo "    终端1: ./build/grpc-server"
        echo "    终端2: ./build/grpc-client"
    else
        err "编译失败! 找不到可执行文件"
        exit 1
    fi
}

# ============================================================
# Release 编译 (优化)
# ============================================================
do_release() {
    BUILD_TYPE="Release"
    do_build
}

# ============================================================
# 清理
# ============================================================
do_clean() {
    info "清理编译产物..."
    rm -rf "$BUILD_DIR"
    info "清理完成"
}

# ============================================================
# Main
# ============================================================

cd "$PROJECT_DIR"

case "$BUILD_TYPE" in
    release|Release)
        do_release
        ;;
    clean)
        do_clean
        ;;
    debug|Debug)
        do_build
        ;;
    *)
        do_build
        ;;
esac
