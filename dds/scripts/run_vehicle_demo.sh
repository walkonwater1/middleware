#!/bin/bash
# ============================================================
# run_vehicle_demo.sh — 一键启动 DDS 车载实验
#
# 用法:
#   ./scripts/run_vehicle_demo.sh                 # 交互菜单
#   ./scripts/run_vehicle_demo.sh pubsub          # 基础发布/订阅
#   ./scripts/run_vehicle_demo.sh qos             # QoS 对比实验
#   ./scripts/run_vehicle_demo.sh discovery       # 自动发现实验
#   ./scripts/run_vehicle_demo.sh listener        # Listener 回调
#   ./scripts/run_vehicle_demo.sh partition       # Partition 隔离
#   ./scripts/run_vehicle_demo.sh --no-build ...  # 跳过编译
# ============================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
DDS_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$DDS_DIR/build"

NO_BUILD=false

usage() {
    echo "用法: $0 [选项] [实验名称]"
    echo ""
    echo "实验名称:"
    echo "  pubsub       基础发布/订阅"
    echo "  qos          QoS 对比实验 (Reliable/BestEffort/TransientLocal)"
    echo "  discovery    自动发现 (多节点零配置通信)"
    echo "  listener     Listener 回调模式 (ROS2 spin 底层)"
    echo "  partition    Partition 逻辑隔离"
    echo ""
    echo "选项:"
    echo "  --no-build   跳过编译"
    echo "  -h, --help   显示帮助"
    echo ""
    echo "不带参数运行则进入交互菜单"
    exit 0
}

# ---- 解析参数 ----
DEMO=""
for arg in "$@"; do
    case $arg in
        --no-build) NO_BUILD=true ;;
        -h|--help)  usage ;;
        pubsub|qos|discovery|listener|partition) DEMO="$arg" ;;
        *) echo "未知参数: $arg"; usage ;;
    esac
done

# ---- 编译 ----
build() {
    if [ "$NO_BUILD" = true ]; then
        return
    fi
    echo "╔══════════════════════════════════════════╗"
    echo "║  🔨 编译 Vehicle Demos                  ║"
    echo "╚══════════════════════════════════════════╝"
    echo ""

    mkdir -p "$BUILD_DIR"

    if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
        echo "[1/2] cmake 配置..."
        cmake -S "$DDS_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release > /dev/null
    else
        echo "[1/2] cmake 已配置, 跳过"
    fi

    echo "[2/2] make..."
    make -C "$BUILD_DIR" -j$(nproc) 2>&1 | grep -E "(Building|Linking|Error|✅)" || true
    echo ""
    echo "✅ 编译完成"
    echo ""
}

# ---- 清理 ----
cleanup() {
    echo ""
    echo "🛑 停止所有车载实验进程..."
    pkill -f "$BUILD_DIR/publisher" 2>/dev/null || true
    pkill -f "$BUILD_DIR/subscriber" 2>/dev/null || true
    pkill -f "$BUILD_DIR/qos_lab" 2>/dev/null || true
    pkill -f "$BUILD_DIR/discovery_lab" 2>/dev/null || true
    pkill -f "$BUILD_DIR/listener_lab" 2>/dev/null || true
    pkill -f "$BUILD_DIR/partition_lab" 2>/dev/null || true
    echo "✅ 已清理"
    exit 0
}
trap cleanup SIGINT SIGTERM

# ================================================================
# 实验 1: 基础 Pub/Sub
# ================================================================
run_pubsub() {
    echo "╔══════════════════════════════════════════╗"
    echo "║  🚗 基础发布/订阅 (VehicleState)         ║"
    echo "╚══════════════════════════════════════════╝"
    echo ""
    echo "  [subscriber] 后台启动, 等待数据..."
    echo "  [publisher]  前台启动, 发布 10 条后退出"
    echo ""

    "$BUILD_DIR/subscriber" &
    SUB_PID=$!
    sleep 1

    "$BUILD_DIR/publisher"
    wait "$SUB_PID" 2>/dev/null
}

# ================================================================
# 实验 2: QoS 对比
# ================================================================
run_qos() {
    echo "╔══════════════════════════════════════════╗"
    echo "║  🚗 QoS 对比实验                         ║"
    echo "╚══════════════════════════════════════════╝"
    echo ""
    echo "  选择实验模式:"
    echo "    A) Reliable 不丢帧    (先起 sub, 再起 pub)"
    echo "    B) BestEffort 可能丢帧 (先起 sub, 再起 pub)"
    echo "    C) LateJoiner 历史数据 (先起 pub, 再起 sub)"
    echo ""
    read -r -p "  输入 A/B/C [A]: " choice
    choice="${choice:-A}"

    case "$choice" in
        [Aa])
            echo ""
            echo "  ▶ Reliable 模式 (保证送达)"
            echo "  [sub] 后台启动..."
            "$BUILD_DIR/qos_lab" sub reliable &
            SUB_PID=$!
            sleep 1
            echo "  [pub] 前台启动..."
            "$BUILD_DIR/qos_lab" pub reliable
            wait "$SUB_PID" 2>/dev/null
            ;;
        [Bb])
            echo ""
            echo "  ▶ BestEffort 模式 (不保证送达, 可能丢帧)"
            echo "  [sub] 后台启动..."
            "$BUILD_DIR/qos_lab" sub besteffort &
            SUB_PID=$!
            sleep 1
            echo "  [pub] 前台启动..."
            "$BUILD_DIR/qos_lab" pub besteffort
            wait "$SUB_PID" 2>/dev/null
            ;;
        [Cc])
            echo ""
            echo "  ▶ LateJoiner 实验 (TransientLocal 保留历史)"
            echo "  [pub] 后台启动, 先发 5 条..."
            "$BUILD_DIR/qos_lab" pub reliable transient &
            PUB_PID=$!
            sleep 6
            echo ""
            echo "  ★ 现在 [sub] 加入 — 能收到之前的 5 条历史数据!"
            echo ""
            "$BUILD_DIR/qos_lab" sub reliable transient
            wait "$PUB_PID" 2>/dev/null
            ;;
        *) echo "无效选择";;
    esac
}

# ================================================================
# 实验 3: 自动发现
# ================================================================
run_discovery() {
    echo "╔══════════════════════════════════════════╗"
    echo "║  🚗 自动发现实验 (3 节点零配置)          ║"
    echo "╚══════════════════════════════════════════╝"
    echo ""
    echo "  启动 3 个进程: pub1 + pub2 + sub"
    echo "  ★ 无需配置 IP/端口, 自动互相发现!"
    echo ""

    "$BUILD_DIR/discovery_lab" pub1 &
    PID1=$!
    "$BUILD_DIR/discovery_lab" pub2 &
    PID2=$!
    sleep 1

    echo "  [pub1] [pub2] 已启动 → 启动 [sub]"
    echo ""

    "$BUILD_DIR/discovery_lab" sub

    kill "$PID1" "$PID2" 2>/dev/null || true
}

# ================================================================
# 实验 4: Listener 回调
# ================================================================
run_listener() {
    echo "╔══════════════════════════════════════════╗"
    echo "║  🚗 Listener 回调模式 (Push vs Pull)     ║"
    echo "╚══════════════════════════════════════════╝"
    echo ""
    echo "  [publisher]  后台发布..."
    echo "  [listener]   前台回调接收 (数据到达自动触发)"
    echo ""
    echo "  ★ listener 用回调而非轮询, 这是 ROS2 spin() 的底层机制"
    echo ""

    "$BUILD_DIR/publisher" &
    PUB_PID=$!
    sleep 1

    "$BUILD_DIR/listener_lab"
    wait "$PUB_PID" 2>/dev/null
}

# ================================================================
# 实验 5: Partition 隔离
# ================================================================
run_partition() {
    echo "╔══════════════════════════════════════════╗"
    echo "║  🚗 Partition 隔离实验                   ║"
    echo "╚══════════════════════════════════════════╝"
    echo ""
    echo "  启动 pub A + pub B, 然后订阅 A/B 观察隔离效果"
    echo ""

    "$BUILD_DIR/partition_lab" pub A &
    PID1=$!
    "$BUILD_DIR/partition_lab" pub B &
    PID2=$!
    sleep 1

    echo "  ▶ 终端模拟: 订阅 Partition A"
    echo "  ★ 只收到 A 的数据, B 的数据不可见"
    echo ""
    timeout 5 "$BUILD_DIR/partition_lab" sub A || true

    echo ""
    echo "  ▶ 终端模拟: 订阅 Partition B"
    echo "  ★ 只收到 B 的数据, A 的数据不可见"
    echo ""
    timeout 5 "$BUILD_DIR/partition_lab" sub B || true

    echo ""
    echo "  ✅ Partition 隔离验证完成 — A/B 互相不可见"
    kill "$PID1" "$PID2" 2>/dev/null || true
}

# ================================================================
# 交互菜单
# ================================================================
show_menu() {
    echo ""
    echo "╔══════════════════════════════════════════╗"
    echo "║  🚗 DDS 车载实验 — 请选择                ║"
    echo "╠══════════════════════════════════════════╣"
    echo "║  1) pubsub       基础发布/订阅           ║"
    echo "║  2) qos          QoS 对比实验            ║"
    echo "║  3) discovery    自动发现                ║"
    echo "║  4) listener     Listener 回调           ║"
    echo "║  5) partition    Partition 隔离          ║"
    echo "║  6) all          依次运行全部            ║"
    echo "║  q) 退出                                ║"
    echo "╚══════════════════════════════════════════╝"
    echo ""
    read -r -p "  输入 [1-6/q]: " choice

    case "$choice" in
        1) run_pubsub ;;
        2) run_qos ;;
        3) run_discovery ;;
        4) run_listener ;;
        5) run_partition ;;
        6)
            echo ""; echo "═══════════════════════════════════════"
            echo "  1/5 基础 Pub/Sub"; echo "═══════════════════════════════════════"
            run_pubsub
            echo ""; echo "═══════════════════════════════════════"
            echo "  2/5 QoS 对比"; echo "═══════════════════════════════════════"
            run_qos
            echo ""; echo "═══════════════════════════════════════"
            echo "  3/5 自动发现"; echo "═══════════════════════════════════════"
            run_discovery
            echo ""; echo "═══════════════════════════════════════"
            echo "  4/5 Listener 回调"; echo "═══════════════════════════════════════"
            run_listener
            echo ""; echo "═══════════════════════════════════════"
            echo "  5/5 Partition 隔离"; echo "═══════════════════════════════════════"
            run_partition
            echo ""; echo "═══════════════════════════════════════"
            echo "  ✅ 全部实验完成!"
            ;;
        q|Q) exit 0 ;;
        *) echo "无效选择" ;;
    esac
}

# ---- 主入口 ----
build

if [ -n "$DEMO" ]; then
    case "$DEMO" in
        pubsub)    run_pubsub ;;
        qos)       run_qos ;;
        discovery) run_discovery ;;
        listener)  run_listener ;;
        partition) run_partition ;;
    esac
else
    while true; do
        show_menu
        echo ""
    done
fi
