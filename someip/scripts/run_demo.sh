#!/usr/bin/env bash
# ============================================================
#  run_demo.sh — SOME/IP 车辆服务一键编译运行
#
#  用法:
#    bash scripts/run_demo.sh service     # 仅启动服务端
#    bash scripts/run_demo.sh client      # 仅启动客户端
#    bash scripts/run_demo.sh both        # 服务端后台 + 客户端前台
#    bash scripts/run_demo.sh build       # 仅编译
#    bash scripts/run_demo.sh clean       # 清理
# ============================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build"

SERVICE_BIN="$BUILD_DIR/vehicle_service"
CLIENT_BIN="$BUILD_DIR/vehicle_client"
SERVICE_CFG="$PROJECT_DIR/vsomeip-service.json"
CLIENT_CFG="$PROJECT_DIR/vsomeip-client.json"

# ---- 颜色 ----
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

info()  { echo -e "${GREEN}[INFO]${NC}  $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }
err()   { echo -e "${RED}[ERR]${NC}  $*"; }
title() { echo -e "\n${CYAN}━━━ $* ━━━${NC}\n"; }

# ============================================================
# 依赖检查
# ============================================================
check_deps() {
    info "检查依赖..."

    if ! ldconfig -p 2>/dev/null | grep -q libvsomeip3; then
        if ! find /usr -name "libvsomeip3*" 2>/dev/null | grep -q .; then
            warn "libvsomeip3 未找到!"
            warn "请安装 vsomeip3: sudo apt install libvsomeip3-dev"
            warn "或从源码构建: https://github.com/COVESA/vsomeip"
            exit 1
        fi
    fi

    info "依赖检查通过"
}

# ============================================================
# 编译
# ============================================================
do_build() {
    check_deps

    info "开始编译..."
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    cmake "$PROJECT_DIR" -DCMAKE_BUILD_TYPE=Debug
    make -j"$(nproc 2>/dev/null || echo 4)"

    if [ ! -x "$SERVICE_BIN" ] || [ ! -x "$CLIENT_BIN" ]; then
        err "编译失败!"
        exit 1
    fi

    info "编译完成"
    cd "$PROJECT_DIR"
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
# 服务端
# ============================================================
run_service() {
    if [ ! -x "$SERVICE_BIN" ]; then
        warn "未编译, 先编译..."
        do_build
    fi

    title "SOME/IP 车辆服务端"
    info "启动服务 (Ctrl-C 退出)..."
    echo ""

    VSOMEIP_CONFIGURATION="$SERVICE_CFG" "$SERVICE_BIN"
}

# ============================================================
# 客户端
# ============================================================
run_client() {
    if [ ! -x "$CLIENT_BIN" ]; then
        warn "未编译, 先编译..."
        do_build
    fi

    title "SOME/IP 车辆客户端"
    info "启动客户端 (Ctrl-C 退出)..."
    echo ""

    VSOMEIP_CONFIGURATION="$CLIENT_CFG" "$CLIENT_BIN"
}

# ============================================================
# 同时运行 (service 后台 + client 前台)
# ============================================================
run_both() {
    if [ ! -x "$SERVICE_BIN" ] || [ ! -x "$CLIENT_BIN" ]; then
        warn "未编译, 先编译..."
        do_build
    fi

    cleanup() {
        if [ -n "$S_PID" ] && kill -0 "$S_PID" 2>/dev/null; then
            kill "$S_PID" 2>/dev/null; wait "$S_PID" 2>/dev/null || true
        fi
    }
    trap cleanup EXIT INT TERM

    title "SOME/IP 车辆服务 — 完整 Demo"

    info "启动服务端 (后台)..."
    VSOMEIP_CONFIGURATION="$SERVICE_CFG" "$SERVICE_BIN" &
    S_PID=$!
    sleep 2

    info "启动客户端 (前台, Ctrl-C 退出)..."
    echo ""
    VSOMEIP_CONFIGURATION="$CLIENT_CFG" "$CLIENT_BIN"

    cleanup
}

# ============================================================
# 用法
# ============================================================
print_usage() {
    echo "用法: bash scripts/run_demo.sh [COMMAND]"
    echo ""
    echo "Commands:"
    echo "  service    启动服务端 (Event + Method + Field)"
    echo "  client     启动客户端 (订阅 + 调用 + Get/Set)"
    echo "  both       服务端后台 + 客户端前台 (一键演示)"
    echo "  build      仅编译"
    echo "  clean      清理"
    echo "  help       帮助"
    echo ""
    echo "手动运行:"
    echo "  终端1: VSOMEIP_CONFIGURATION=vsomeip-service.json ./build/vehicle_service"
    echo "  终端2: VSOMEIP_CONFIGURATION=vsomeip-client.json ./build/vehicle_client"
}

# ============================================================
# Main
# ============================================================
cd "$PROJECT_DIR"

case "${1:-}" in
    service)
        run_service
        ;;
    client)
        run_client
        ;;
    both)
        run_both
        ;;
    build)
        do_build
        ;;
    clean)
        do_clean
        ;;
    help|--help|-h)
        print_usage
        ;;
    "")
        print_usage
        echo ""
        info "请指定命令, 例如: bash scripts/run_demo.sh both"
        ;;
    *)
        err "未知命令: $1"
        print_usage
        exit 1
        ;;
esac
