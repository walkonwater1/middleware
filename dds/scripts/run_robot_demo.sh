#!/bin/bash
# ============================================================
# run_robot_demo.sh — 一键启动 DDS 机器人 Demo
#
# 用法:
#   ./scripts/run_robot_demo.sh              # 自动编译+启动
#   ./scripts/run_robot_demo.sh --no-build   # 跳过编译, 直接启动
#   ./scripts/run_robot_demo.sh --service    # 只启动服务端
#   ./scripts/run_robot_demo.sh --monitor    # 只启动监控端
# ============================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
DDS_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$DDS_DIR/build"

SERVICE_BIN="$BUILD_DIR/robot_dds_service"
MONITOR_BIN="$BUILD_DIR/robot_dds_monitor"

NO_BUILD=false
SERVICE_ONLY=false
MONITOR_ONLY=false

usage() {
    echo "用法: $0 [选项]"
    echo ""
    echo "选项:"
    echo "  --no-build    跳过编译, 直接启动"
    echo "  --service     只启动服务端"
    echo "  --monitor     只启动监控端"
    echo "  -h, --help    显示帮助"
    exit 0
}

for arg in "$@"; do
    case $arg in
        --no-build) NO_BUILD=true ;;
        --service)  SERVICE_ONLY=true ;;
        --monitor)  MONITOR_ONLY=true ;;
        -h|--help)  usage ;;
    esac
done

# ---- 编译 ----
if [ "$NO_BUILD" = false ]; then
    echo "╔══════════════════════════════════════════╗"
    echo "║  🔨 编译 Robot Demo                     ║"
    echo "╚══════════════════════════════════════════╝"
    echo ""

    mkdir -p "$BUILD_DIR"

    if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
        echo "[1/2] cmake 配置..."
        cmake -S "$DDS_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release > /dev/null
    else
        echo "[1/2] cmake 已配置, 跳过"
    fi

    echo "[2/2] make robot_dds_service robot_dds_monitor..."
    make -C "$BUILD_DIR" robot_dds_service robot_dds_monitor -j$(nproc)

    echo ""
    echo "✅ 编译完成"
    echo ""
fi

# ---- 检查二进制 ----
if [ ! -x "$SERVICE_BIN" ]; then
    echo "❌ 找不到 $SERVICE_BIN, 请先编译"
    exit 1
fi
if [ ! -x "$MONITOR_BIN" ]; then
    echo "❌ 找不到 $MONITOR_BIN, 请先编译"
    exit 1
fi

# ---- 清理函数 ----
cleanup() {
    echo ""
    echo "╔══════════════════════════════════════════╗"
    echo "║  🛑 正在停止...                         ║"
    echo "╚══════════════════════════════════════════╝"
    if [ -n "$SERVICE_PID" ] && kill -0 "$SERVICE_PID" 2>/dev/null; then
        kill "$SERVICE_PID" 2>/dev/null
        wait "$SERVICE_PID" 2>/dev/null
        echo "  ✅ robot_dds_service 已停止"
    fi
    pkill -f robot_dds_monitor 2>/dev/null || true
    echo "  ✅ robot_dds_monitor 已停止"
    exit 0
}
trap cleanup SIGINT SIGTERM

# ---- 只启动服务端 ----
if [ "$SERVICE_ONLY" = true ]; then
    echo "╔══════════════════════════════════════════╗"
    echo "║  🤖 启动 Robot DDS Service              ║"
    echo "╚══════════════════════════════════════════╝"
    echo ""
    echo "  Domain: 0"
    echo "  Topics: RobotStatus / TaskProgress / RobotAlarm / TaskCommand"
    echo "  QoS:    Reliable + TransientLocal + KeepLast(10)"
    echo ""
    echo "  按 Ctrl+C 停止..."
    echo ""
    exec "$SERVICE_BIN"
fi

# ---- 只启动监控端 ----
if [ "$MONITOR_ONLY" = true ]; then
    echo "╔══════════════════════════════════════════╗"
    echo "║  📡 启动 Robot DDS Monitor              ║"
    echo "╚══════════════════════════════════════════╝"
    echo ""
    echo "  Domain: 0"
    echo "  Topics: RobotStatus / TaskProgress / RobotAlarm / TaskCommand"
    echo "  QoS:    Reliable + TransientLocal + KeepLast(10)"
    echo ""
    exec "$MONITOR_BIN"
fi

# ---- 完整 Demo: 服务端 + 监控端 ----
echo "╔══════════════════════════════════════════╗"
echo "║  🤖 DDS 机器人 Demo (完整模式)          ║"
echo "╚══════════════════════════════════════════╝"
echo ""
echo "  ┌─────────────────────────────────────┐"
echo "  │  robot_dds_service  ──DDS──►  monitor │"
echo "  │  (状态机+任务调度)          (远程监控) │"
echo "  └─────────────────────────────────────┘"
echo ""
echo "  Domain: 0 | QoS: Reliable+TransientLocal+KeepLast(10)"
echo ""

# 1. 启动服务端
echo "[1/2] 启动 robot_dds_service..."
"$SERVICE_BIN" &
SERVICE_PID=$!
sleep 1

if ! kill -0 "$SERVICE_PID" 2>/dev/null; then
    echo "❌ 服务端启动失败"
    exit 1
fi
echo "  ✅ 服务端 PID=$SERVICE_PID"
echo ""

# 2. 启动监控端 (前台运行, Ctrl+C 退出)
echo "[2/2] 启动 robot_dds_monitor..."
echo "  按 Ctrl+C 停止全部..."
echo ""
sleep 1

"$MONITOR_BIN"

# 3. 监控端退出后, 停止服务端
cleanup
