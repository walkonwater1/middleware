#!/bin/bash
# multimachine_guide.sh — ROS2 多机通信交互式诊断工具
set -e

echo "╔══════════════════════════════════════════╗"
echo "║  ROS2 多机通信 — 交互式诊断              ║"
echo "╚══════════════════════════════════════════╝"
echo ""

# 1. 主机信息
echo "━━━ 本机信息 ━━━"
echo "  Hostname:  $(hostname)"
echo "  IP:        $(hostname -I 2>/dev/null | awk '{print $1}')"
echo "  PID:       $$"
echo ""

# 2. ROS 环境
echo "━━━ ROS2 环境 ━━━"
echo "  ROS_DOMAIN_ID:    ${ROS_DOMAIN_ID:-0 (默认)}"
echo "  ROS_LOCALHOST_ONLY: ${ROS_LOCALHOST_ONLY:-未设置}"
echo "  RMW_IMPLEMENTATION: ${RMW_IMPLEMENTATION:-默认 (FastDDS)}"
echo "  ROS_VERSION:      ${ROS_VERSION:-未设置}"
echo ""

# 3. 组播检查
echo "━━━ 组播检查 ━━━"
ping -c 1 -W 1 224.0.0.1 &>/dev/null && echo "  ✅ 组播 ping 224.0.0.1 成功" || echo "  ⚠️  组播 ping 失败 (检查网络/防火墙)"
echo ""

# 4. ROS2 Daemon 状态
echo "━━━ ROS2 Daemon ━━━"
if ros2 daemon status &>/dev/null; then
    echo "  ✅ ros2 daemon 运行中"
else
    echo "  ⚠️  ros2 daemon 未运行 (不影响通信, 但影响 CLI 工具)"
fi
echo ""

# 5. 活跃话题
echo "━━━ 当前发现的 Topic ━━━"
TOPICS=$(ros2 topic list 2>/dev/null)
if [ -n "$TOPICS" ]; then
    echo "$TOPICS" | while read t; do echo "  📡 $t"; done
else
    echo "  (无发现的 topic — 可能无其他节点在线)"
fi
echo ""

# 6. 活跃节点
echo "━━━ 当前发现的 Node ━━━"
NODES=$(ros2 node list 2>/dev/null)
if [ -n "$NODES" ]; then
    echo "$NODES" | while read n; do echo "  🤖 $n"; done
else
    echo "  (无发现的节点)"
fi
echo ""

# 7. Domain ID 建议
echo "━━━ 诊断建议 ━━━"
echo "  如果两台机器无法发现对方, 请依次检查:"
echo "  1. 是否在同一子网? (ping <对方IP> 验证)"
echo "  2. ROS_DOMAIN_ID 是否相同?"
echo "  3. 防火墙是否阻止了 UDP 7400-7500?"
echo "  4. 是否有多网卡选错接口?"
echo ""
echo "  调试技巧:"
echo "  - ROS_LOCALHOST_ONLY=1 限制本地通信, 快速排除网络问题"
echo "  - ros2 topic list 检查是否能看到对方 topic"
echo "  - ros2 node info <node> 查看节点详情"
echo ""
