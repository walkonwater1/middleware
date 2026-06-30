#!/usr/bin/env bash
# ============================================================
#  run_demo.sh — 一键运行 POSIX 共享内存 C++17 Demo
#
#  用法:
#    bash scripts/run_demo.sh                # 自动编译 + basic demo
#    bash scripts/run_demo.sh basic          # 基本读写 (车辆传感器)
#    bash scripts/run_demo.sh ringbuf        # 环形缓冲区 (机器人关节指令)
#    bash scripts/run_demo.sh build          # 仅编译
#    bash scripts/run_demo.sh clean          # 清理编译产物
# ============================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build"

BASIC_BIN="$BUILD_DIR/demo_basic"
RINGBUF_BIN="$BUILD_DIR/demo_ringbuf"
LOCKFREE_BIN="$BUILD_DIR/demo_lockfree_queue"

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
# 编译
# ============================================================
do_build() {
    info "开始编译..."
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    cmake "$PROJECT_DIR" -DCMAKE_BUILD_TYPE=Debug
    make -j"$(nproc 2>/dev/null || echo 4)"

    if [ ! -x "$BASIC_BIN" ] || [ ! -x "$RINGBUF_BIN" ] || [ ! -x "$LOCKFREE_BIN" ]; then
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
# 基本读写 Demo
# ============================================================
run_basic() {
    if [ ! -x "$BASIC_BIN" ]; then
        warn "未编译, 先编译..."
        do_build
    fi

    cleanup() {
        if [ -n "$W_PID" ] && kill -0 "$W_PID" 2>/dev/null; then
            kill "$W_PID" 2>/dev/null; wait "$W_PID" 2>/dev/null || true
        fi
    }
    trap cleanup EXIT INT TERM

    title "Demo 1: 基本读写 — 车辆传感器数据共享"

    info "启动 Writer (后台)..."
    "$BASIC_BIN" writer &
    W_PID=$!
    sleep 1

    info "启动 Reader (前台, Ctrl-C 退出)..."
    echo ""
    "$BASIC_BIN" reader

    cleanup
}

# ============================================================
# 环形缓冲区 Demo
# ============================================================
run_ringbuf() {
    if [ ! -x "$RINGBUF_BIN" ]; then
        warn "未编译, 先编译..."
        do_build
    fi

    cleanup() {
        if [ -n "$P_PID" ] && kill -0 "$P_PID" 2>/dev/null; then
            kill "$P_PID" 2>/dev/null; wait "$P_PID" 2>/dev/null || true
        fi
    }
    trap cleanup EXIT INT TERM

    title "Demo 2: 无锁环形缓冲区 — 机器人关节指令队列"

    info "启动 Producer (后台, 20Hz)..."
    "$RINGBUF_BIN" producer &
    P_PID=$!
    sleep 0.5

    info "启动 Consumer (前台, Ctrl-C 退出)..."
    echo ""
    "$RINGBUF_BIN" consumer

    cleanup
}

# ============================================================
# 无锁 MPMC 队列 Demo
# ============================================================
run_lockfree() {
    if [ ! -x "$LOCKFREE_BIN" ]; then
        warn "未编译, 先编译..."
        do_build
    fi

    N_PROD="${1:-2}"
    N_CONS="${2:-2}"

    cleanup() {
        if [ -n "$P_PID" ] && kill -0 "$P_PID" 2>/dev/null; then
            kill "$P_PID" 2>/dev/null; wait "$P_PID" 2>/dev/null || true
        fi
    }
    trap cleanup EXIT INT TERM

    title "Demo 3: 无锁 MPMC 队列 — ${N_PROD} producers + ${N_CONS} consumers"

    info "启动 Producer (后台, ${N_PROD} 线程)..."
    "$LOCKFREE_BIN" producer --threads="$N_PROD" &
    P_PID=$!
    sleep 0.5

    info "启动 Consumer (前台, ${N_CONS} 线程, Ctrl-C 退出)..."
    echo ""
    "$LOCKFREE_BIN" consumer --threads="$N_CONS"

    cleanup
}

# ============================================================
# 用法
# ============================================================
print_usage() {
    echo "用法: bash scripts/run_demo.sh [COMMAND]"
    echo ""
    echo "Commands:"
    echo "  (无)      自动编译 + 运行 basic demo"
    echo "  basic     基本读写 — 车辆传感器数据共享"
    echo "  ringbuf   无锁环形缓冲区 — 机器人关节指令队列"
    echo "  lockfree  无锁 MPMC 队列 — 多生产者多消费者"
    echo "  build     仅编译"
    echo "  clean     清理编译产物"
    echo "  help      显示此帮助"
    echo ""
    echo "手动分步:"
    echo "  终端1: ./build/demo_basic writer"
    echo "  终端2: ./build/demo_basic reader"
    echo ""
    echo "  终端1: ./build/demo_ringbuf producer"
    echo "  终端2: ./build/demo_ringbuf consumer"
    echo ""
    echo "  终端1: ./build/demo_lockfree_queue producer --threads=4"
    echo "  终端2: ./build/demo_lockfree_queue consumer --threads=2"
    echo ""
    echo "  单进程: ./build/demo_lockfree_queue both --producers=4 --consumers=2"
}

# ============================================================
# Main
# ============================================================
cd "$PROJECT_DIR"

case "${1:-}" in
    basic)
        run_basic
        ;;
    ringbuf)
        run_ringbuf
        ;;
    lockfree)
        run_lockfree "$2" "$3"
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
        run_basic
        ;;
    *)
        err "未知命令: $1"
        print_usage
        exit 1
        ;;
esac
