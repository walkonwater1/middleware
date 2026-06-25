#!/usr/bin/env bash
# ============================================================
#  run_demo.sh — 一键运行 GDBus C++17 Demo
#
#  用法:
#    bash scripts/run_demo.sh              # 自动启动 dbus + service + client
#    bash scripts/run_demo.sh service      # 仅启动服务端
#    bash scripts/run_demo.sh client       # 仅启动客户端
#    bash scripts/run_demo.sh build        # 编译后再运行
#    bash scripts/run_demo.sh clean        # 清理编译产物
#
#  环境变量:
#    BUILD_DIR   构建目录 (默认 build/)
#    DEMO_ARGS   传递给 demo 程序的额外参数
# ============================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="${BUILD_DIR:-$PROJECT_DIR/build}"

SERVICE_BIN="$BUILD_DIR/gdbus-demo-service"
CLIENT_BIN="$BUILD_DIR/gdbus-demo-client"

# 颜色
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
# 检查 & 启动 D-Bus Session Bus
# ============================================================

DBUS_PID=""
DBUS_SOCKET=""

ensure_dbus_session() {
    # 1. 检查是否已有 session bus 地址
    if [ -n "$DBUS_SESSION_BUS_ADDRESS" ]; then
        info "D-Bus session bus 已存在: $DBUS_SESSION_BUS_ADDRESS"
        return 0
    fi

    # 2. 检查是否有 dbus-daemon 进程
    if pgrep -u "$USER" dbus-daemon > /dev/null 2>&1; then
        # 尝试从进程环境获取地址
        local pid
        pid=$(pgrep -u "$USER" -n dbus-daemon)
        if [ -f "/proc/$pid/environ" ]; then
            # 尝试从 /proc 读取
            warn "D-Bus 正在运行但 DBUS_SESSION_BUS_ADDRESS 未设置"
        fi
        return 0
    fi

    # 3. 尝试用 dbus-launch 启动
    if command -v dbus-launch > /dev/null 2>&1; then
        info "启动 D-Bus session bus (dbus-launch)..."
        eval "$(dbus-launch --sh-syntax --exit-with-session)"
        DBUS_PID="$DBUS_SESSION_BUS_PID"
        info "D-Bus session bus 已启动 (pid=$DBUS_PID)"
        return 0
    fi

    # 4. 尝试用 dbus-run-session (较新版本)
    if command -v dbus-run-session > /dev/null 2>&1; then
        warn "dbus-run-session 需要包裹运行，请手动执行:"
        echo ""
        echo "  dbus-run-session -- bash scripts/run_demo.sh"
        echo ""
        exit 1
    fi

    err "无法启动 D-Bus session bus! 请安装 dbus:"
    err "  sudo apt install dbus"
    exit 1
}

cleanup_dbus() {
    if [ -n "$DBUS_PID" ] && kill -0 "$DBUS_PID" 2>/dev/null; then
        info "停止 D-Bus daemon (pid=$DBUS_PID)"
        kill "$DBUS_PID" 2>/dev/null || true
    fi
}

# ============================================================
# 编译
# ============================================================

do_build() {
    info "开始编译..."

    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"

    cmake "$PROJECT_DIR" -DCMAKE_BUILD_TYPE=Debug
    make -j"$(nproc 2>/dev/null || echo 4)"

    if [ ! -x "$SERVICE_BIN" ] || [ ! -x "$CLIENT_BIN" ]; then
        err "编译失败! 找不到可执行文件"
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
# 运行服务端
# ============================================================

run_service() {
    ensure_dbus_session

    title "============================================"
    title "  GDBus C++17 Demo — 车辆信息服务端"
    title "============================================"
    echo ""

    if [ ! -x "$SERVICE_BIN" ]; then
        err "找不到 $SERVICE_BIN, 请先编译: bash scripts/run_demo.sh build"
        exit 1
    fi

    trap cleanup_dbus EXIT
    exec "$SERVICE_BIN" $DEMO_ARGS
}

# ============================================================
# 运行客户端
# ============================================================

run_client() {
    ensure_dbus_session

    title "============================================"
    title "  GDBus C++17 Demo — 车辆信息客户端"
    title "============================================"
    echo ""

    if [ ! -x "$CLIENT_BIN" ]; then
        err "找不到 $CLIENT_BIN, 请先编译: bash scripts/run_demo.sh build"
        exit 1
    fi

    trap cleanup_dbus EXIT
    exec "$CLIENT_BIN" $DEMO_ARGS
}

# ============================================================
# 同时运行服务端和客户端 (服务端后台, 客户端前台)
# ============================================================

run_all() {
    ensure_dbus_session

    title "============================================"
    title "  GDBus C++17 Demo — 同时运行服务端 & 客户端"
    title "============================================"
    echo ""

    if [ ! -x "$SERVICE_BIN" ] || [ ! -x "$CLIENT_BIN" ]; then
        err "找不到可执行文件, 请先编译: bash scripts/run_demo.sh build"
        exit 1
    fi

    # 清理函数: 退出时停掉后台服务
    cleanup() {
        info "正在停止..."
        if [ -n "$SVC_PID" ] && kill -0 "$SVC_PID" 2>/dev/null; then
            kill "$SVC_PID" 2>/dev/null
            wait "$SVC_PID" 2>/dev/null || true
        fi
        cleanup_dbus
    }
    trap cleanup EXIT INT TERM

    # 启动服务端 (后台)
    info "启动服务端 (后台)..."
    "$SERVICE_BIN" &
    SVC_PID=$!
    sleep 1  # 等待服务端就绪

    if ! kill -0 "$SVC_PID" 2>/dev/null; then
        err "服务端启动失败!"
        exit 1
    fi
    info "服务端已启动 (pid=$SVC_PID)"

    # 启动客户端 (前台)
    echo ""
    info "启动客户端 (前台, Ctrl+C 退出)..."
    echo ""
    "$CLIENT_BIN" $DEMO_ARGS

    cleanup
}

# ============================================================
# Main
# ============================================================

print_usage() {
    echo "用法: bash scripts/run_demo.sh [COMMAND]"
    echo ""
    echo "Commands:"
    echo "  (无)      自动启动 D-Bus + 服务端(后台) + 客户端(前台)"
    echo "  service   仅启动服务端"
    echo "  client    仅启动客户端"
    echo "  build     编译项目后运行"
    echo "  clean     清理编译产物"
    echo "  help      显示此帮助"
    echo ""
    echo "环境变量:"
    echo "  BUILD_DIR    构建目录 (默认: build/)"
    echo "  DEMO_ARGS    传递给 demo 程序的参数"
    echo ""
    echo "首次使用:"
    echo "  bash scripts/run_demo.sh build    # 编译并运行"
    echo ""
    echo "手动分步:"
    echo "  终端1: bash scripts/run_demo.sh service"
    echo "  终端2: bash scripts/run_demo.sh client"
}

cd "$PROJECT_DIR"

case "${1:-}" in
    service)
        shift
        DEMO_ARGS="$*"
        run_service
        ;;
    client)
        shift
        DEMO_ARGS="$*"
        run_client
        ;;
    build)
        do_build
        shift
        DEMO_ARGS="$*"
        run_all
        ;;
    clean)
        do_clean
        ;;
    help|--help|-h)
        print_usage
        ;;
    "")
        run_all
        ;;
    *)
        err "未知命令: $1"
        print_usage
        exit 1
        ;;
esac
