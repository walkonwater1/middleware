#!/usr/bin/env bash
# ============================================================
#  run_demo.sh — ZeroMQ 5 种通信模式一键编译运行
#
#  用法:
#    bash scripts/run_demo.sh req_rep         # REQ/REP 请求-响应
#    bash scripts/run_demo.sh pub_sub          # PUB/SUB 发布-订阅
#    bash scripts/run_demo.sh push_pull        # PUSH/PULL 管道
#    bash scripts/run_demo.sh router_dealer    # ROUTER/DEALER 异步
#    bash scripts/run_demo.sh pair             # PAIR 独占对
#    bash scripts/run_demo.sh all              # 依次运行所有 5 种模式
#    bash scripts/run_demo.sh build            # 仅编译
#    bash scripts/run_demo.sh clean            # 清理
# ============================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build"

# ---- 二进制路径 ----
REQ_REP_SERVER="$BUILD_DIR/req_rep_server"
REQ_REP_CLIENT="$BUILD_DIR/req_rep_client"
PUB_SUB_PUB="$BUILD_DIR/pub_sub_publisher"
PUB_SUB_SUB="$BUILD_DIR/pub_sub_subscriber"
PUSH_PULL_PRODUCER="$BUILD_DIR/push_pull_producer"
PUSH_PULL_WORKER="$BUILD_DIR/push_pull_worker"
ROUTER_DEALER_SERVER="$BUILD_DIR/router_dealer_server"
ROUTER_DEALER_CLIENT="$BUILD_DIR/router_dealer_client"
PAIR_NODE_A="$BUILD_DIR/pair_node_a"
PAIR_NODE_B="$BUILD_DIR/pair_node_b"

ALL_BINARIES=(
    "$REQ_REP_SERVER" "$REQ_REP_CLIENT"
    "$PUB_SUB_PUB" "$PUB_SUB_SUB"
    "$PUSH_PULL_PRODUCER" "$PUSH_PULL_WORKER"
    "$ROUTER_DEALER_SERVER" "$ROUTER_DEALER_CLIENT"
    "$PAIR_NODE_A" "$PAIR_NODE_B"
)

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

    if ! pkg-config --exists libzmq 2>/dev/null; then
        err "libzmq 未安装! 请运行: sudo apt install libzmq3-dev"
        exit 1
    fi

    local cppzmq_found=0
    for dir in /usr/local/include /usr/include; do
        if [ -f "$dir/zmq.hpp" ]; then
            cppzmq_found=1
            break
        fi
    done

    if [ "$cppzmq_found" -eq 0 ]; then
        warn "cppzmq (zmq.hpp) 未找到!"
        warn "请运行: git clone https://github.com/zeromq/cppzmq.git"
        warn "        sudo cp cppzmq/zmq.hpp /usr/local/include/"
        warn "        sudo cp cppzmq/zmq_addon.hpp /usr/local/include/"
        exit 1
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

    local all_ok=1
    for bin in "${ALL_BINARIES[@]}"; do
        if [ ! -x "$bin" ]; then
            err "缺少二进制: $bin"
            all_ok=0
        fi
    done

    if [ "$all_ok" -eq 0 ]; then
        err "编译不完整!"
        exit 1
    fi

    info "编译完成 (10 个二进制)"
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
# 辅助: 先编译后运行 server+client
# ============================================================
ensure_built() {
    for bin in "$@"; do
        if [ ! -x "$bin" ]; then
            warn "未编译, 先编译..."
            do_build
            return
        fi
    done
}

# ============================================================
# REQ/REP — 请求-响应
# ============================================================
run_req_rep() {
    ensure_built "$REQ_REP_SERVER" "$REQ_REP_CLIENT"

    cleanup() {
        if [ -n "$S_PID" ] && kill -0 "$S_PID" 2>/dev/null; then
            kill "$S_PID" 2>/dev/null; wait "$S_PID" 2>/dev/null || true
        fi
    }
    trap cleanup EXIT INT TERM

    title "REQ/REP 模式 — 车辆诊断服务 (同步请求-响应)"

    info "启动诊断服务端 (后台)..."
    "$REQ_REP_SERVER" &
    S_PID=$!
    sleep 0.5

    info "启动诊断客户端 (前台)..."
    echo ""
    "$REQ_REP_CLIENT"

    cleanup
}

# ============================================================
# PUB/SUB — 发布-订阅
# ============================================================
run_pub_sub() {
    ensure_built "$PUB_SUB_PUB" "$PUB_SUB_SUB"

    cleanup() {
        if [ -n "$P_PID" ] && kill -0 "$P_PID" 2>/dev/null; then
            kill "$P_PID" 2>/dev/null; wait "$P_PID" 2>/dev/null || true
        fi
    }
    trap cleanup EXIT INT TERM

    title "PUB/SUB 模式 — 机器人传感器数据广播"

    info "启动订阅者 (后台)..."
    "$PUB_SUB_SUB" &
    S_PID=$!
    sleep 0.3

    info "启动发布者 (前台, 每秒发布一次, ~3轮)..."
    echo ""
    timeout 4 "$PUB_SUB_PUB" || true

    cleanup
}

# ============================================================
# PUSH/PULL — 管道 (负载均衡)
# ============================================================
run_push_pull() {
    ensure_built "$PUSH_PULL_PRODUCER" "$PUSH_PULL_WORKER"

    cleanup() {
        if [ -n "$P_PID" ] && kill -0 "$P_PID" 2>/dev/null; then
            kill "$P_PID" 2>/dev/null; wait "$P_PID" 2>/dev/null || true
        fi
        if [ -n "$W1_PID" ] && kill -0 "$W1_PID" 2>/dev/null; then
            kill "$W1_PID" 2>/dev/null; wait "$W1_PID" 2>/dev/null || true
        fi
        if [ -n "$W2_PID" ] && kill -0 "$W2_PID" 2>/dev/null; then
            kill "$W2_PID" 2>/dev/null; wait "$W2_PID" 2>/dev/null || true
        fi
    }
    trap cleanup EXIT INT TERM

    title "PUSH/PULL 模式 — 传感器数据管道 (round-robin 负载均衡)"

    info "启动 2 个 Worker (后台)..."
    "$PUSH_PULL_WORKER" --id=worker-A &
    W1_PID=$!
    "$PUSH_PULL_WORKER" --id=worker-B &
    W2_PID=$!
    sleep 0.3

    info "启动 Producer (前台, ~3s)..."
    echo ""
    timeout 3 "$PUSH_PULL_PRODUCER" || true

    cleanup
}

# ============================================================
# ROUTER/DEALER — 异步请求-响应
# ============================================================
run_router_dealer() {
    ensure_built "$ROUTER_DEALER_SERVER" "$ROUTER_DEALER_CLIENT"

    cleanup() {
        if [ -n "$S_PID" ] && kill -0 "$S_PID" 2>/dev/null; then
            kill "$S_PID" 2>/dev/null; wait "$S_PID" 2>/dev/null || true
        fi
    }
    trap cleanup EXIT INT TERM

    title "ROUTER/DEALER 模式 — 异步车辆诊断 (无锁步限制)"

    info "启动 ROUTER 诊断服务端 (后台)..."
    "$ROUTER_DEALER_SERVER" &
    S_PID=$!
    sleep 0.5

    info "启动 DEALER 客户端 (前台) — 批量异步查询..."
    echo ""
    "$ROUTER_DEALER_CLIENT"

    cleanup
}

# ============================================================
# PAIR — 独占对
# ============================================================
run_pair() {
    ensure_built "$PAIR_NODE_A" "$PAIR_NODE_B"

    cleanup() {
        if [ -n "$A_PID" ] && kill -0 "$A_PID" 2>/dev/null; then
            kill "$A_PID" 2>/dev/null; wait "$A_PID" 2>/dev/null || true
        fi
    }
    trap cleanup EXIT INT TERM

    title "PAIR 模式 — 控制节点双向状态同步"

    info "启动 Node B (后台)..."
    "$PAIR_NODE_B" &
    B_PID=$!
    sleep 0.3

    info "启动 Node A (前台, ~3s)..."
    echo ""
    timeout 3 "$PAIR_NODE_A" || true

    cleanup
}

# ============================================================
# all — 依次运行所有 5 种模式
# ============================================================
run_all() {
    ensure_built "${ALL_BINARIES[@]}"

    info "依次运行所有 5 种 ZMQ 通信模式..."
    echo ""

    run_req_rep
    sleep 1

    run_pub_sub
    sleep 1

    run_push_pull
    sleep 1

    run_router_dealer
    sleep 1

    run_pair

    echo ""
    info "全部 5 种模式运行完成!"
}

# ============================================================
# 用法
# ============================================================
print_usage() {
    echo "用法: bash scripts/run_demo.sh [COMMAND]"
    echo ""
    echo "Commands:"
    echo "  req_rep         REQ/REP 模式 — 同步请求-响应 (车辆诊断)"
    echo "  pub_sub         PUB/SUB 模式 — 发布-订阅 (传感器广播)"
    echo "  push_pull       PUSH/PULL 模式 — 管道/负载均衡 (任务分发)"
    echo "  router_dealer   ROUTER/DEALER 模式 — 异步请求-响应"
    echo "  pair            PAIR 模式 — 独占对 (状态同步)"
    echo "  all             依次运行以上全部 5 种模式"
    echo "  build           仅编译"
    echo "  clean           清理编译产物"
    echo "  help            显示此帮助"
    echo ""
    echo "手动分步 (多终端):"
    echo ""
    echo "  # REQ/REP:"
    echo "  ./build/req_rep_server &    ./build/req_rep_client"
    echo ""
    echo "  # PUB/SUB:"
    echo "  ./build/pub_sub_subscriber &    ./build/pub_sub_publisher"
    echo ""
    echo "  # PUSH/PULL (可多 worker):"
    echo "  ./build/push_pull_producer &"
    echo "  ./build/push_pull_worker --id=worker-A &"
    echo "  ./build/push_pull_worker --id=worker-B &"
    echo ""
    echo "  # ROUTER/DEALER:"
    echo "  ./build/router_dealer_server &    ./build/router_dealer_client"
    echo ""
    echo "  # PAIR:"
    echo "  ./build/pair_node_a &    ./build/pair_node_b"
}

# ============================================================
# Main
# ============================================================
cd "$PROJECT_DIR"

case "${1:-}" in
    req_rep)
        run_req_rep
        ;;
    pub_sub)
        run_pub_sub
        ;;
    push_pull)
        run_push_pull
        ;;
    router_dealer)
        run_router_dealer
        ;;
    pair)
        run_pair
        ;;
    all)
        run_all
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
        info "请指定命令, 例如: bash scripts/run_demo.sh all"
        ;;
    *)
        err "未知命令: $1"
        print_usage
        exit 1
        ;;
esac
