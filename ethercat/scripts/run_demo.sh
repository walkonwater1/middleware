#!/usr/bin/env bash
# ============================================================
#  run_demo.sh — EtherCAT 主站+从站一键编译运行
#
#  用法:
#    bash scripts/run_demo.sh master   # 仅主站
#    bash scripts/run_demo.sh slave    # 仅从站
#    bash scripts/run_demo.sh both     # 从站后台 + 主站前台
#    bash scripts/run_demo.sh build    # 仅编译
#    bash scripts/run_demo.sh clean    # 清理
# ============================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build"

MASTER_BIN="$BUILD_DIR/ethercat_master"
SLAVE_BIN="$BUILD_DIR/ethercat_slave"

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; CYAN='\033[0;36m'; NC='\033[0m'
info()  { echo -e "${GREEN}[INFO]${NC}  $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }
err()   { echo -e "${RED}[ERR]${NC}  $*"; }
title() { echo -e "\n${CYAN}━━━ $* ━━━${NC}\n"; }

do_build() {
    info "编译 EtherCAT..."
    mkdir -p "$BUILD_DIR" && cd "$BUILD_DIR"
    cmake "$PROJECT_DIR" -DCMAKE_BUILD_TYPE=Debug
    make -j"$(nproc 2>/dev/null || echo 4)"
    [ -x "$MASTER_BIN" ] && [ -x "$SLAVE_BIN" ] || { err "编译失败!"; exit 1; }
    info "编译完成"; cd "$PROJECT_DIR"
}

do_clean() {
    info "清理..."
    rm -rf "$BUILD_DIR"
    # 清理残留的共享内存
    rm -f /dev/shm/ethercat_esc_shm 2>/dev/null
    info "清理完成"
}

run_slave() {
    [ -x "$SLAVE_BIN" ] || do_build
    title "EtherCAT Virtual Slave (3-axis servo)"
    "$SLAVE_BIN"
}

run_master() {
    [ -x "$MASTER_BIN" ] || do_build
    title "EtherCAT Master (SOEM-style, 1kHz)"
    "$MASTER_BIN"
}

run_both() {
    [ -x "$MASTER_BIN" ] && [ -x "$SLAVE_BIN" ] || do_build

    # 清理旧的共享内存
    rm -f /dev/shm/ethercat_esc_shm 2>/dev/null

    cleanup() {
        [ -n "$S_PID" ] && kill -0 "$S_PID" 2>/dev/null && kill "$S_PID" 2>/dev/null
        wait 2>/dev/null || true
    }
    trap cleanup EXIT INT TERM

    title "EtherCAT — 3-Axis Servo Drive Demo (1kHz)"

    info "启动 Virtual Slave (后台)..."
    "$SLAVE_BIN" &
    S_PID=$!
    sleep 0.5

    info "启动 Master (前台, 15s)..."
    echo ""
    "$MASTER_BIN"

    cleanup
}

print_usage() {
    echo "用法: bash scripts/run_demo.sh [COMMAND]"
    echo ""
    echo "Commands:"
    echo "  master   EtherCAT 主站"
    echo "  slave    EtherCAT 虚拟从站"
    echo "  both     从站后台 + 主站前台"
    echo "  build    仅编译"
    echo "  clean    清理"
    echo "  help     帮助"
    echo ""
    echo "手动:"
    echo "  终端1: ./build/ethercat_slave"
    echo "  终端2: ./build/ethercat_master"
}

cd "$PROJECT_DIR"
case "${1:-}" in
    master) run_master ;;
    slave)  run_slave ;;
    both)   run_both ;;
    build)  do_build ;;
    clean)  do_clean ;;
    help|--help|-h) print_usage ;;
    "") print_usage; info "请指定命令, 例如: bash scripts/run_demo.sh both" ;;
    *) err "未知命令: $1"; print_usage; exit 1 ;;
esac
