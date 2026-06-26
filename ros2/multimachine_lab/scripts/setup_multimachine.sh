#!/bin/bash
# setup_multimachine.sh — ROS2 多机通信配置参考指南
#
# 本脚本是参考文档 (不可直接执行), 逐步说明配置多机通信的步骤
# 使用方式: 阅读每个步骤, 根据实际网络环境调整

set -e

echo "╔══════════════════════════════════════════╗"
echo "║  ROS2 多机通信 — 配置指南               ║"
echo "╚══════════════════════════════════════════╝"
echo ""

# --------------------------------------------------
# Step 1: 确认网络拓扑
# --------------------------------------------------
echo "━━━ Step 1: 网络拓扑 ━━━"
echo "要求: 两台或多台机器在 同一子网, 支持组播"
echo ""
echo "当前机器信息:"
echo "  Hostname: $(hostname)"
echo "  IP 地址:  $(hostname -I | awk '{print $1}')"
echo ""

# --------------------------------------------------
# Step 2: 检查防火墙
# --------------------------------------------------
echo "━━━ Step 2: 防火墙配置 ━━━"
echo "DDS 使用 UDP 组播进行自动发现, 需要开放相应端口"
echo ""
echo "检查防火墙状态:"
if command -v ufw &>/dev/null; then
    sudo ufw status 2>/dev/null || echo "  ufw 未启用或无权限查看"
fi
echo ""
echo "如需开放端口 (DDS 默认使用 7400-7500 范围):"
echo "  sudo ufw allow 7400:7500/udp"
echo ""

# --------------------------------------------------
# Step 3: 测试组播
# --------------------------------------------------
echo "━━━ Step 3: 组播测试 ━━━"
echo "ping 224.0.0.1 测试组播可达性:"
ping -c 2 -W 1 224.0.0.1 2>/dev/null && echo "  ✅ 组播可达" || echo "  ⚠️  组播可能受限 (检查网络/防火墙)"
echo ""

# --------------------------------------------------
# Step 4: ROS_DOMAIN_ID
# --------------------------------------------------
echo "━━━ Step 4: ROS_DOMAIN_ID ━━━"
echo "所有通信的节点必须在同一 Domain ID 下"
echo ""
echo "当前 ROS_DOMAIN_ID: ${ROS_DOMAIN_ID:-<未设置, 默认 0>}"
echo ""
echo "设置 Domain ID (所有机器必须一致):"
echo "  export ROS_DOMAIN_ID=5"
echo "  或启动时指定:"
echo "  ROS_DOMAIN_ID=5 ros2 run multimachine_lab discovery_publisher"
echo ""
echo "Domain ID 范围: 0-232 (部分 ID 被保留)"
echo ""

# --------------------------------------------------
# Step 5: 网络接口选择
# --------------------------------------------------
echo "━━━ Step 5: 网络接口选择 ━━━"
echo "如果机器有多个网卡, 需要选择正确的接口"
echo ""
echo "可用接口:"
ip -br addr show 2>/dev/null | grep -v "^lo" || ifconfig | grep -E "^[a-z]"
echo ""
echo "选择特定接口 (CycloneDDS 配置):"
echo '  export CYCLONEDDS_URI="<CycloneDDS>'
echo '    <Domain><General>'
echo '      <NetworkInterfaceAddress>eth0</NetworkInterfaceAddress>'
echo '    </General></Domain>'
echo '  </CycloneDDS>"'
echo ""

# --------------------------------------------------
# Step 6: 单机测试模式
# --------------------------------------------------
echo "━━━ Step 6: 单机测试 ━━━"
echo "如果只有一台机器, 用 ROS_LOCALHOST_ONLY 限制为本地通信:"
echo "  export ROS_LOCALHOST_ONLY=1"
echo "  (两终端无需 SSH 到不同机器即可测试)"
echo ""

# --------------------------------------------------
# Step 7: 验证指令
# --------------------------------------------------
echo "━━━ Step 7: 验证指令 ━━━"
echo "跨机通信测试:"
echo ""
echo "  Machine A (IP 192.168.1.10):"
echo "    ROS_DOMAIN_ID=5 ros2 run multimachine_lab discovery_publisher"
echo ""
echo "  Machine B (IP 192.168.1.20, 同一子网):"
echo "    ROS_DOMAIN_ID=5 ros2 run multimachine_lab discovery_subscriber"
echo ""
echo "  Machine B 上验证:"
echo "    ros2 topic list       # 应看到 /discovery_channel"
echo "    ros2 node list        # 应看到 discovery_publisher_node"
echo "    ros2 topic echo /discovery_channel  # 应收到消息"
echo ""

echo "━━━ 配置参考完成 ━━━"
