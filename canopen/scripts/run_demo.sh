#!/usr/bin/env bash
# ============================================================
#  run_demo.sh — CANopen 主站+从站一键编译运行
#
#  用法:
#    bash scripts/run_demo.sh master    # 仅主站
#    bash scripts/run_demo.sh node      # 仅从站
#    bash scripts/run_demo.sh both      # 从站后台 + 主站前台
#    bash scripts/run_demo.sh vcan      # 创建 vcan0 + 运行
#    bash scripts/run_demo.sh build     # 仅编译
#    bash scripts/run_demo.sh clean     # 清理
# ============================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build"

MASTER_BIN="$BUILD_DIR/canopen_master"
NODE_BIN="$BUILD_DIR/canopen_node"

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; CYAN='\033[0;36m'; NC='\033[0m'
info()  { echo -e "${GREEN}[INFO]${NC}  $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }
err()   { echo -e "${RED}[ERR]${NC}  $*"; }
title() { echo -e "\n${CYAN}━━━ $* ━━━${NC}\n"; }

do_build() {
    info "编译 CANopen..."
    mkdir -p "$BUILD_DIR" && cd "$BUILD_DIR"
    cmake "$PROJECT_DIR" -DCMAKE_BUILD_TYPE=Debug
    make -j"$(nproc 2>/dev/null || echo 4)"
    [ -x "$MASTER_BIN" ] && [ -x "$NODE_BIN" ] || { err "编译失败!"; exit 1; }
    info "编译完成"; cd "$PROJECT_DIR"
}

do_clean() { info "清理..."; rm -rf "$BUILD_DIR"; info "清理完成"; }

run_master() {
    [ -x "$MASTER_BIN" ] || do_build
    title "CANopen Master (主站)"
    "$MASTER_BIN"
}

run_node() {
    [ -x "$NODE_BIN" ] || do_build
    title "CANopen Node (从站 #1)"
    "$NODE_BIN" 1
}

run_both() {
    [ -x "$MASTER_BIN" ] && [ -x "$NODE_BIN" ] || do_build

    cleanup() {
        [ -n "$N_PID" ] && kill -0 "$N_PID" 2>/dev/null && kill "$N_PID" 2>/dev/null; wait 2>/dev/null || true
    }
    trap cleanup EXIT INT TERM

    title "CANopen — CiA 402 伺服驱动 Demo"
    info "启动从站 #1 (后台)..."
    "$NODE_BIN" 1 &
    N_PID=$!
    sleep 0.3
    info "启动主站 (前台, ~30s)..."
    echo ""
    "$MASTER_BIN"
    cleanup
}

run_vcan() {
    info "设置 vcan0..."
    sudo ip link add dev vcan0 type vcan 2>/dev/null || true
    sudo ip link set vcan0 up 2>/dev/null || true
    info "vcan0 就绪, 运行 demo..."
    run_both
}

print_usage() {
    echo "用法: bash scripts/run_demo.sh [COMMAND]"
    echo ""
    echo "Commands:"
    echo "  master   CANopen 主站"
    echo "  node     CANopen 从站 (node_id=1)"
    echo "  both     从站后台 + 主站前台"
    echo "  vcan     设置 vcan0 + 运行 demo"
    echo "  build    仅编译"
    echo "  clean    清理"
    echo "  help     帮助"
}

cd "$PROJECT_DIR"
case "${1:-}" in
    master) run_master ;;
    node)   run_node ;;
    both)   run_both ;;
    vcan)   run_vcan ;;
    build)  do_build ;;
    clean)  do_clean ;;
    help|--help|-h) print_usage ;;
    "") print_usage; info "请指定命令, 例如: bash scripts/run_demo.sh both" ;;
    *) err "未知命令: $1"; print_usage; exit 1 ;;
esac
